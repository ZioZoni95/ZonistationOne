#include "spu.h"
#include "log.h"
#include <string.h>

/**
 * @brief Initialize the SPU (Sound Processing Unit)
 * Based on PSX-SPX specifications: https://psx-spx.consoledev.net/soundprocessingunitspu/
 */
void spu_init(Spu* spu) {
    memset(spu, 0, sizeof(Spu));
    
    // Initialize SPU to default state
    spu->enabled = false;
    spu->irq_enabled = false;
    spu->dma_busy = false;
    
    // Default volumes (based on PSX-SPX documentation)
    spu->main_vol_left = 0x3FFF;   // Default main volume
    spu->main_vol_right = 0x3FFF;
    spu->cd_vol_left = 0x0000;     // CD audio starts muted
    spu->cd_vol_right = 0x0000;
    spu->ext_vol_left = 0x0000;    // External audio starts muted  
    spu->ext_vol_right = 0x0000;
    
    // Initialize voice channels to silent state
    for (int i = 0; i < SPU_VOICE_COUNT; i++) {
        spu->voices[i].vol_left = 0;
        spu->voices[i].vol_right = 0;
        spu->voices[i].sample_rate = 0;
        spu->voices[i].start_addr = 0;
        spu->voices[i].adsr_settings = 0;
        spu->voices[i].adsr_volume = 0;
        spu->voices[i].repeat_addr = 0;
    }
    
    // Clear SPU RAM
    memset(spu->ram, 0, SPU_RAM_SIZE);
    
    LOG_INFO("SPU initialized (512KB RAM, 24 voices, stubbed implementation)");
}

/**
 * @brief Reset SPU to power-on state
 */
void spu_reset(Spu* spu) {
    LOG_INFO("SPU reset requested");
    spu_init(spu); // Reset is same as init for now
}

/**
 * @brief Read 16-bit value from SPU register
 */
uint16_t spu_load16(Spu* spu, uint32_t offset) {
    // Voice registers (0x000-0x17F, 16 bytes per voice)
    if (offset < 0x180) {
        int voice = offset / 16;
        int reg = (offset % 16) / 2;
        
        if (voice >= SPU_VOICE_COUNT) {
            LOG_WARN("SPU: Invalid voice number %d in load16", voice);
            return 0;
        }
        
        switch (reg) {
            case 0: return spu->voices[voice].vol_left;
            case 1: return spu->voices[voice].vol_right;
            case 2: return spu->voices[voice].sample_rate;
            case 3: return spu->voices[voice].start_addr;
            case 4: return spu->voices[voice].adsr_settings;
            case 5: return spu->voices[voice].adsr_volume;
            case 6: return spu->voices[voice].repeat_addr;
            case 7: return spu->voices[voice].reserved;
            default: 
                LOG_WARN("SPU: Unknown voice register %d for voice %d", reg, voice);
                return 0;
        }
    }
    
    // Main SPU registers
    switch (offset) {
        case SPU_MAIN_VOL_L: return spu->main_vol_left;
        case SPU_MAIN_VOL_R: return spu->main_vol_right;
        case SPU_REVERB_VOL_L: return spu->reverb_vol_left;
        case SPU_REVERB_VOL_R: return spu->reverb_vol_right;
        case SPU_KEY_ON_LOW: return (uint16_t)(spu->key_on & 0xFFFF);
        case SPU_KEY_ON_HIGH: return (uint16_t)((spu->key_on >> 16) & 0xFF);
        case SPU_KEY_OFF_LOW: return (uint16_t)(spu->key_off & 0xFFFF);
        case SPU_KEY_OFF_HIGH: return (uint16_t)((spu->key_off >> 16) & 0xFF);
        case SPU_NOISE_MODE_LOW: return (uint16_t)(spu->noise_mode & 0xFFFF);
        case SPU_NOISE_MODE_HIGH: return (uint16_t)((spu->noise_mode >> 16) & 0xFF);
        case SPU_REVERB_ON_LOW: return (uint16_t)(spu->reverb_on & 0xFFFF);
        case SPU_REVERB_ON_HIGH: return (uint16_t)((spu->reverb_on >> 16) & 0xFF);
        case SPU_VOICE_STATUS: return 0; // TODO: Implement voice status
        case SPU_REVERB_WORK_ADDR: return spu->reverb_work_addr;
        case SPU_IRQ_ADDR: return spu->irq_addr;
        case SPU_DATA_TRANSFER_ADDR: return spu->transfer_addr;
        case SPU_DATA_TRANSFER_FIFO: return 0; // TODO: Implement FIFO
        case SPU_CONTROL: return spu->control;
        case SPU_DATA_TRANSFER_CTRL: return spu->transfer_control;
        case SPU_STATUS: return spu->status;
        case SPU_CD_VOL_L: return spu->cd_vol_left;
        case SPU_CD_VOL_R: return spu->cd_vol_right;
        case SPU_EXT_VOL_L: return spu->ext_vol_left;
        case SPU_EXT_VOL_R: return spu->ext_vol_right;
        default:
            LOG_WARN("SPU: Unhandled read16 from offset 0x%03X", offset);
            return 0;
    }
}

/**
 * @brief Read 32-bit value from SPU register
 */
uint32_t spu_load32(Spu* spu, uint32_t offset) {
    // Combine two 16-bit reads for 32-bit access
    uint16_t low = spu_load16(spu, offset);
    uint16_t high = spu_load16(spu, offset + 2);
    return ((uint32_t)high << 16) | low;
}

/**
 * @brief Write 16-bit value to SPU register
 */
void spu_store16(Spu* spu, uint32_t offset, uint16_t value) {
    // Voice registers (0x000-0x17F, 16 bytes per voice)
    if (offset < 0x180) {
        int voice = offset / 16;
        int reg = (offset % 16) / 2;
        
        if (voice >= SPU_VOICE_COUNT) {
            LOG_WARN("SPU: Invalid voice number %d in store16", voice);
            return;
        }
        
        switch (reg) {
            case 0: spu->voices[voice].vol_left = value; break;
            case 1: spu->voices[voice].vol_right = value; break;
            case 2: spu->voices[voice].sample_rate = value; break;
            case 3: spu->voices[voice].start_addr = value; break;
            case 4: spu->voices[voice].adsr_settings = value; break;
            case 5: spu->voices[voice].adsr_volume = value; break;
            case 6: spu->voices[voice].repeat_addr = value; break;
            case 7: spu->voices[voice].reserved = value; break;
            default: 
                LOG_WARN("SPU: Unknown voice register %d for voice %d = 0x%04X", reg, voice, value);
                break;
        }
        return;
    }
    
    // Main SPU registers
    switch (offset) {
        case SPU_MAIN_VOL_L: spu->main_vol_left = value; break;
        case SPU_MAIN_VOL_R: spu->main_vol_right = value; break;
        case SPU_REVERB_VOL_L: spu->reverb_vol_left = value; break;
        case SPU_REVERB_VOL_R: spu->reverb_vol_right = value; break;
        case SPU_KEY_ON_LOW: 
            spu->key_on = (spu->key_on & 0xFF0000) | value;
            // TODO: Trigger key-on for affected voices
            break;
        case SPU_KEY_ON_HIGH: 
            spu->key_on = (spu->key_on & 0x00FFFF) | ((uint32_t)(value & 0xFF) << 16);
            // TODO: Trigger key-on for affected voices
            break;
        case SPU_KEY_OFF_LOW: 
            spu->key_off = (spu->key_off & 0xFF0000) | value;
            // TODO: Trigger key-off for affected voices
            break;
        case SPU_KEY_OFF_HIGH: 
            spu->key_off = (spu->key_off & 0x00FFFF) | ((uint32_t)(value & 0xFF) << 16);
            // TODO: Trigger key-off for affected voices
            break;
        case SPU_NOISE_MODE_LOW: spu->noise_mode = (spu->noise_mode & 0xFF0000) | value; break;
        case SPU_NOISE_MODE_HIGH: spu->noise_mode = (spu->noise_mode & 0x00FFFF) | ((uint32_t)(value & 0xFF) << 16); break;
        case SPU_REVERB_ON_LOW: spu->reverb_on = (spu->reverb_on & 0xFF0000) | value; break;
        case SPU_REVERB_ON_HIGH: spu->reverb_on = (spu->reverb_on & 0x00FFFF) | ((uint32_t)(value & 0xFF) << 16); break;
        case SPU_REVERB_WORK_ADDR: spu->reverb_work_addr = value; break;
        case SPU_IRQ_ADDR: spu->irq_addr = value; break;
        case SPU_DATA_TRANSFER_ADDR: spu->transfer_addr = value; break;
        case SPU_DATA_TRANSFER_FIFO: 
            // TODO: Implement FIFO data transfer
            LOG_WARN("SPU: Data transfer FIFO write 0x%04X (not implemented)", value);
            break;
        case SPU_CONTROL: 
            spu->control = value;
            spu->enabled = (value & 0x8000) != 0;
            spu->irq_enabled = (value & 0x0040) != 0;
            if (value & 0x0010) spu_reset(spu); // Reset bit
            break;
        case SPU_DATA_TRANSFER_CTRL: spu->transfer_control = value; break;
        case SPU_CD_VOL_L: spu->cd_vol_left = value; break;
        case SPU_CD_VOL_R: spu->cd_vol_right = value; break;
        case SPU_EXT_VOL_L: spu->ext_vol_left = value; break;
        case SPU_EXT_VOL_R: spu->ext_vol_right = value; break;
        default:
            LOG_WARN("SPU: Unhandled write16 to offset 0x%03X = 0x%04X", offset, value);
            break;
    }
}

/**
 * @brief Write 32-bit value to SPU register
 */
void spu_store32(Spu* spu, uint32_t offset, uint32_t value) {
    // Split into two 16-bit writes
    spu_store16(spu, offset, (uint16_t)(value & 0xFFFF));
    spu_store16(spu, offset + 2, (uint16_t)((value >> 16) & 0xFFFF));
}

// Voice Control Functions (stubs for now)
void spu_key_on_voice(Spu* spu, int voice_num) {
    if (voice_num < 0 || voice_num >= SPU_VOICE_COUNT) return;
    spu->key_on |= (1 << voice_num);
    LOG_TRACE("SPU: Key-on voice %d (stub)", voice_num);
}

void spu_key_off_voice(Spu* spu, int voice_num) {
    if (voice_num < 0 || voice_num >= SPU_VOICE_COUNT) return;
    spu->key_off |= (1 << voice_num);
    LOG_TRACE("SPU: Key-off voice %d (stub)", voice_num);
}

void spu_set_voice_volume(Spu* spu, int voice_num, uint16_t left, uint16_t right) {
    if (voice_num < 0 || voice_num >= SPU_VOICE_COUNT) return;
    spu->voices[voice_num].vol_left = left;
    spu->voices[voice_num].vol_right = right;
}

void spu_set_voice_sample_rate(Spu* spu, int voice_num, uint16_t rate) {
    if (voice_num < 0 || voice_num >= SPU_VOICE_COUNT) return;
    spu->voices[voice_num].sample_rate = rate;
}

// DMA Functions (stubs for now)
void spu_dma_write(Spu* spu, uint16_t* data, uint32_t size) {
    LOG_WARN("SPU: DMA write of %u words to address 0x%04X (stub)", size, spu->transfer_addr);
    // TODO: Implement SPU DMA write
    spu->dma_busy = false;
}

void spu_dma_read(Spu* spu, uint16_t* data, uint32_t size) {
    LOG_WARN("SPU: DMA read of %u words from address 0x%04X (stub)", size, spu->transfer_addr);
    // TODO: Implement SPU DMA read
    spu->dma_busy = false;
}

// Audio Processing (stubs for now)
void spu_update(Spu* spu) {
    // TODO: Update voice playback, ADSR envelopes, etc.
    (void)spu; // Suppress unused parameter warning
}

void spu_generate_samples(Spu* spu, int16_t* output, int sample_count) {
    // TODO: Generate actual audio samples
    // For now, just output silence
    memset(output, 0, sample_count * sizeof(int16_t) * 2); // Stereo
    (void)spu; // Suppress unused parameter warning
}