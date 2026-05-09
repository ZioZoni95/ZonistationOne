#include "spu.h"
#include "interconnect.h"
#include "log.h"
#include <string.h>
#include <stdlib.h>

/* Forward declarations from other modules */
extern void spu_reverb_init(Spu* spu);
extern void spu_voice_generate_sample(Spu* spu, int voice_idx, int16_t* left_out, int16_t* right_out);
extern void spu_adsr_process(SpuVoice* voice);

/* =========================================================================
 * Helpers
 * ========================================================================= */

static int spu_offset_for_addr(uint32_t addr) {
    if (addr < SPU_START || addr > SPU_END) return -1;
    return (int)(addr - SPU_START);
}

static int offset_to_reg_idx(int offset) {
    return offset / 2;
}

/* =========================================================================
 * Init / Reset
 * ========================================================================= */

void spu_init(Spu* spu) {
    if (!spu) return;
    memset(spu, 0, sizeof(Spu));
    spu_reverb_init(spu);
    LOG_SPU_DEBUG("[SPU] SPU initialized (MMIO %08x-%08x, RAM %u bytes)",
                  SPU_START, SPU_END, SPU_RAM_SIZE);
}

void spu_reset(Spu* spu) {
    if (!spu) return;
    uint16_t* ram_copy = (uint16_t*)malloc(SPU_RAM_SIZE);
    memcpy(ram_copy, spu->ram, SPU_RAM_SIZE);
    memset(spu, 0, sizeof(Spu));
    memcpy(spu->ram, ram_copy, SPU_RAM_SIZE);
    free(ram_copy);
    spu_reverb_init(spu);
    LOG_SPU_INFO("[SPU] SPU reset (RAM preserved)");
}

/* =========================================================================
 * Key On/Off Processing
 * ========================================================================= */

void spu_process_key_on_off(Spu* spu) {
    /* Process key off first */
    for (int v = 0; v < NUM_VOICES; v++) {
        if (spu->key_off & (1u << v)) {
            SpuVoice* voice = &spu->voices[v];
            if (voice->adsr_phase != ADSR_PHASE_OFF &&
                voice->adsr_phase != ADSR_PHASE_RELEASE) {
                voice->adsr_phase = ADSR_PHASE_RELEASE;
                voice->adsr_counter = 0;
                LOG_SPU_TRACE("[SPU] Voice %d Key Off", v);
            }
        }
    }

    /* Process key on */
    for (int v = 0; v < NUM_VOICES; v++) {
        if (spu->key_on & (1u << v)) {
            SpuVoice* voice = &spu->voices[v];
            spu->endx &= ~(1u << v);
            voice->current_address = voice->start_address & ~1;
            voice->counter_index = 0;
            voice->counter_sample = 0;
            voice->adsr_volume = 0;
            voice->adpcm_last_samples[0] = 0;
            voice->adpcm_last_samples[1] = 0;
            memset(voice->block_samples, 0, sizeof(voice->block_samples));
            voice->has_samples = false;
            voice->is_first_block = true;
            voice->ignore_loop_address = false;
            voice->endx_mask = false;
            voice->adsr_phase = ADSR_PHASE_ATTACK;
            voice->adsr_counter = 0;
            voice->adsr_target = 32767;
            spu->total_key_on_events++;
            voice->left_sweep_counter = 0;
            voice->right_sweep_counter = 0;
            LOG_SPU_TRACE("[SPU] Voice %d Key On (start=0x%04X, pitch=%u)",
                          v, voice->start_address, voice->pitch);
        }
    }

    spu->key_on = 0;
    spu->key_off = 0;
}

/* =========================================================================
 * Control Register
 * ========================================================================= */

void spu_set_control(Spu* spu, uint16_t value) {
    uint16_t old = spu->control;
    spu->control = value;

    /* Mute */
    spu->muted = !(value & SPU_CTRL_MUTE);

    /* IRQ enable change */
    if ((old & SPU_CTRL_IRQ9_ENABLE) && !(value & SPU_CTRL_IRQ9_ENABLE)) {
        spu->irq9_flag = false;
        spu->status &= ~SPU_STATUS_IRQ9_FLAG;
    }

    /* SPU disable: force all voices off */
    if (!(value & SPU_CTRL_ENABLE)) {
        for (int v = 0; v < NUM_VOICES; v++) {
            if (spu->voices[v].adsr_phase != ADSR_PHASE_OFF) {
                spu->voices[v].adsr_phase = ADSR_PHASE_OFF;
                spu->voices[v].adsr_volume = 0;
            }
        }
    }

    /* Update status mode bits */
    spu->status = (spu->status & ~SPU_STATUS_MODE) | (value & SPU_CTRL_TRANSFER_MODE);

    LOG_SPU_DEBUG("[SPU] Control=0x%04X (enable=%d, mute=%d, irq=%d, mode=%d)",
                  value,
                  (value & SPU_CTRL_ENABLE) ? 1 : 0,
                  (value & SPU_CTRL_MUTE) ? 0 : 1,
                  (value & SPU_CTRL_IRQ9_ENABLE) ? 1 : 0,
                  (value >> 4) & 0x03);
}

/* =========================================================================
 * MMIO Read
 * ========================================================================= */

uint16_t spu_read16(struct Interconnect* inter, uint32_t addr) {
    Spu* spu = &inter->spu;
    int off = spu_offset_for_addr(addr);
    if (off < 0) return 0;

    uint32_t reg = (uint32_t)off;

    /* Extended read-only: current voice volume (0x1E00-0x1E5F) */
    if (reg >= 0x200 && reg < 0x260) {
        int voice = (reg - 0x200) / 4;
        int sub = (reg - 0x200) % 4;
        if (voice >= 0 && voice < NUM_VOICES) {
            if (sub == 0) return (uint16_t)spu->voices[voice].left_sweep_level;
            if (sub == 2) return (uint16_t)spu->voices[voice].right_sweep_level;
        }
        return 0;
    }

    /* Voice registers (0x000-0x17F) */
    if (reg < 0x180) {
        int voice = (reg >> 4) & 0x1F;
        int sub = reg & 0x0F;
        if (voice < NUM_VOICES) {
            switch (sub) {
                case 0x00: return spu->voices[voice].volume_left;
                case 0x02: return spu->voices[voice].volume_right;
                case 0x04: return spu->voices[voice].pitch;
                case 0x06: return spu->voices[voice].start_address;
                case 0x08: return spu->voices[voice].adsr_low;
                case 0x0A: return spu->voices[voice].adsr_high;
                case 0x0C: return (uint16_t)spu->voices[voice].adsr_volume;
                case 0x0E: return spu->voices[voice].repeat_address;
            }
        }
        return 0;
    }

    /* Control registers */
    switch (reg) {
        case SPU_REG_MVOL_L: return spu->main_vol_left;
        case SPU_REG_MVOL_R: return spu->main_vol_right;
        case SPU_REG_RVOL_L: return (uint16_t)spu->reverb_vol_left;
        case SPU_REG_RVOL_R: return (uint16_t)spu->reverb_vol_right;
        case SPU_REG_KEY_ON_L: return spu->key_on & 0xFFFF;
        case SPU_REG_KEY_ON_H: return (spu->key_on >> 16) & 0xFFFF;
        case SPU_REG_KEY_OFF_L: return spu->key_off & 0xFFFF;
        case SPU_REG_KEY_OFF_H: return (spu->key_off >> 16) & 0xFFFF;
        case SPU_REG_FMOD_L: return spu->pitch_mod & 0xFFFF;
        case SPU_REG_FMOD_H: return (spu->pitch_mod >> 16) & 0xFFFF;
        case SPU_REG_NOISE_L: return spu->noise_mode & 0xFFFF;
        case SPU_REG_NOISE_H: return (spu->noise_mode >> 16) & 0xFFFF;
        case SPU_REG_REVERB_L: return spu->reverb_on & 0xFFFF;
        case SPU_REG_REVERB_H: return (spu->reverb_on >> 16) & 0xFFFF;
        case SPU_REG_ENDX_L: return spu->endx & 0xFFFF;
        case SPU_REG_ENDX_H: return (spu->endx >> 16) & 0xFFFF;
        case SPU_REG_REVERB_BASE: return spu->reverb_base;
        case SPU_REG_IRQ_ADDR: return spu->irq_addr;
        case SPU_REG_TRANSFER_ADDR: return spu->transfer_addr_reg;
        case SPU_REG_CONTROL: return spu->control;
        case SPU_REG_STATUS: return spu->status;
        case SPU_REG_CD_VOL_L: return (uint16_t)spu->cd_vol_left;
        case SPU_REG_CD_VOL_R: return (uint16_t)spu->cd_vol_right;
        case SPU_REG_EXT_VOL_L: return (uint16_t)spu->ext_vol_left;
        case SPU_REG_EXT_VOL_R: return (uint16_t)spu->ext_vol_right;
        case SPU_REG_MVOL_CUR_L: return (uint16_t)spu->main_vol_left_cur;
        case SPU_REG_MVOL_CUR_R: return (uint16_t)spu->main_vol_right_cur;
        default:
            if (reg >= 0x1C0 && reg < 0x200) {
                return spu->reverb_regs[(reg - 0x1C0) / 2];
            }
            break;
    }

    LOG_SPU_DEBUG("[SPU] Read16 unknown reg 0x%04X", reg);
    return 0;
}

uint32_t spu_read32(struct Interconnect* inter, uint32_t addr) {
    uint16_t lo = spu_read16(inter, addr);
    uint16_t hi = spu_read16(inter, addr + 2);
    return (uint32_t)lo | ((uint32_t)hi << 16);
}

/* =========================================================================
 * MMIO Write
 * ========================================================================= */

static void voice_write_reg(Spu* spu, int voice, int sub, uint16_t value) {
    if (voice < 0 || voice >= NUM_VOICES) return;
    SpuVoice* v = &spu->voices[voice];

    switch (sub) {
        case 0x00: v->volume_left = value; break;
        case 0x02: v->volume_right = value; break;
        case 0x04: v->pitch = value & 0x3FFF; break;
        case 0x06: v->start_address = value; break;
        case 0x08: v->adsr_low = value; break;
        case 0x0A: v->adsr_high = value; break;
        case 0x0C: v->adsr_volume = (int16_t)value; break;
        case 0x0E: v->repeat_address = value; break;
    }
}

void spu_write16(struct Interconnect* inter, uint32_t addr, uint16_t value) {
    Spu* spu = &inter->spu;
    int off = spu_offset_for_addr(addr);
    if (off < 0) return;

    uint32_t reg = (uint32_t)off;

    /* Voice registers (0x000-0x17F) */
    if (reg < 0x180) {
        int voice = (reg >> 4) & 0x1F;
        int sub = reg & 0x0F;
        voice_write_reg(spu, voice, sub, value);
        return;
    }

    /* Control registers */
    switch (reg) {
        case SPU_REG_MVOL_L: spu->main_vol_left = value; break;
        case SPU_REG_MVOL_R: spu->main_vol_right = value; break;
        case SPU_REG_RVOL_L: spu->reverb_vol_left = (int16_t)value; break;
        case SPU_REG_RVOL_R: spu->reverb_vol_right = (int16_t)value; break;
        case SPU_REG_KEY_ON_L: spu->key_on |= value; break;
        case SPU_REG_KEY_ON_H: spu->key_on |= ((uint32_t)value << 16); break;
        case SPU_REG_KEY_OFF_L: spu->key_off |= value; break;
        case SPU_REG_KEY_OFF_H: spu->key_off |= ((uint32_t)value << 16); break;
        case SPU_REG_FMOD_L: spu->pitch_mod = (spu->pitch_mod & ~0xFFFF) | value; break;
        case SPU_REG_FMOD_H: spu->pitch_mod = (spu->pitch_mod & ~0xFFFF0000) | ((uint32_t)value << 16); break;
        case SPU_REG_NOISE_L: spu->noise_mode = (spu->noise_mode & ~0xFFFF) | value; break;
        case SPU_REG_NOISE_H: spu->noise_mode = (spu->noise_mode & ~0xFFFF0000) | ((uint32_t)value << 16); break;
        case SPU_REG_REVERB_L: spu->reverb_on = (spu->reverb_on & ~0xFFFF) | value; break;
        case SPU_REG_REVERB_H: spu->reverb_on = (spu->reverb_on & ~0xFFFF0000) | ((uint32_t)value << 16); break;
        case SPU_REG_REVERB_BASE: spu->reverb_base = value & 0x3FFF; break;
        case SPU_REG_IRQ_ADDR:
            spu->irq_addr = value;
            spu_update_irq_addr(spu);
            break;
        case SPU_REG_TRANSFER_ADDR:
            spu->transfer_addr_reg = value;
            spu->transfer_addr = (uint32_t)value * 8;
            spu_check_irq(spu, spu->transfer_addr);
            break;
        case SPU_REG_TRANSFER_DATA: {
            int mode = (spu->control >> 4) & 0x03;
            if (mode == TRANSFER_MANUAL_WRITE || mode == TRANSFER_DMA_WRITE) {
                spu_transfer_write(spu, value);
            } else if (mode == TRANSFER_DMA_READ) {
                /* Write to FIFO during DMA read */
            }
            break;
        }
        case SPU_REG_CONTROL:
            spu_set_control(spu, value);
            break;
        case SPU_REG_CD_VOL_L: spu->cd_vol_left = (int16_t)value; break;
        case SPU_REG_CD_VOL_R: spu->cd_vol_right = (int16_t)value; break;
        case SPU_REG_EXT_VOL_L: spu->ext_vol_left = (int16_t)value; break;
        case SPU_REG_EXT_VOL_R: spu->ext_vol_right = (int16_t)value; break;
        default:
            if (reg >= 0x1C0 && reg < 0x200) {
                spu->reverb_regs[(reg - 0x1C0) / 2] = value;
            } else {
                LOG_SPU_DEBUG("[SPU] Write16 unknown reg 0x%04X val=0x%04X", reg, value);
            }
            break;
    }
}

void spu_write32(struct Interconnect* inter, uint32_t addr, uint32_t value) {
    spu_write16(inter, addr, (uint16_t)(value & 0xFFFF));
    spu_write16(inter, addr + 2, (uint16_t)((value >> 16) & 0xFFFF));
}

void spu_write8(struct Interconnect* inter, uint32_t addr, uint8_t value) {
    if (addr & 1) return;
    uint16_t prev = spu_read16(inter, addr);
    uint16_t newv = (prev & 0xFF00) | value;
    spu_write16(inter, addr, newv);
}
