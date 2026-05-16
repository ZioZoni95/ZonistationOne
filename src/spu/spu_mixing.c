#include "spu.h"
#include "interconnect.h"
#include "log.h"
#include <string.h>

/* Forward declarations from other modules */
extern void spu_reverb_init(Spu* spu);
extern void spu_adsr_process(SpuVoice* voice);

/* =========================================================================
 * Internal helpers
 * ========================================================================= */

static int32_t clamp16(int32_t v) {
    if (v > 32767) return 32767;
    if (v < -32768) return -32768;
    return v;
}

/* =========================================================================
 * Noise Generator (Dr. Hell's algorithm)
 * ========================================================================= */

static const uint8_t noise_wave_add[64] = {
    1,0,0,1,0,1,1,0, 1,0,0,1,0,1,1,0, 1,0,0,1,0,1,1,0, 1,0,0,1,0,1,1,0,
    1,0,0,1,0,1,1,0, 1,0,0,1,0,1,1,0, 1,0,0,1,0,1,1,0, 1,0,0,1,0,1,1,0,
};

static const uint8_t noise_freq_add[5] = { 0, 84, 140, 180, 210 };

static void noise_tick(Spu* spu) {
    uint32_t clock = (spu->control >> 8) & 0x3F;
    uint32_t level = (0x8000 >> (clock >> 2)) << 16;

    spu->noise_count += 0x10000 + noise_freq_add[clock & 3];
    if ((spu->noise_count & 0xFFFF) >= noise_freq_add[4]) {
        spu->noise_count += 0x10000 - noise_freq_add[clock & 3];
    }

    if (spu->noise_count >= level) {
        spu->noise_count %= level;
        spu->noise_level = (spu->noise_level << 1) |
            noise_wave_add[(spu->noise_level >> 10) & 63];
    }
}

/* =========================================================================
 * Capture Buffer
 * ========================================================================= */

static void capture_write(Spu* spu, int channel, int16_t value) {
    spu->capture_buffer[channel][spu->capture_pos] = value;
}

static void capture_increment(Spu* spu) {
    spu->capture_pos += 2;
    spu->capture_pos %= CAPTURE_BUFFER_SIZE;
    spu->status = (spu->status & ~SPU_STATUS_CB_HALF) |
        ((spu->capture_pos >= 0x200) ? SPU_STATUS_CB_HALF : 0);
}

/* =========================================================================
 * Reverb (simplified but functional)
 * ========================================================================= */

static uint32_t reverb_mem_addr(Spu* spu, uint32_t offset) {
    uint32_t mask = (SPU_RAM_SIZE - 1) / 2;
    uint32_t addr = spu->reverb_current_addr + (offset & mask);
    addr += ((uint32_t)spu->reverb_base) & ((addr << 13) >> 31);
    return (addr & mask) * 2;
}

static int16_t reverb_read(Spu* spu, uint32_t offset) {
    uint32_t addr = reverb_mem_addr(spu, offset) / 2;
    return spu->ram[addr];
}

static void reverb_write(Spu* spu, uint32_t offset, int16_t value) {
    uint32_t addr = reverb_mem_addr(spu, offset) / 2;
    spu->ram[addr] = (uint16_t)value;
}

static void iiasm(int16_t alpha, int32_t* out, int32_t insamp) {
    if (alpha == -32768) {
        *out = (insamp == -32768) ? 0 : insamp * (int64_t)(-65536);
    } else {
        *out = insamp * (32768 - alpha);
    }
}

static void reverb_process(Spu* spu, int32_t input_l, int32_t input_r,
                           int32_t* out_l, int32_t* out_r) {
    if (!(spu->control & SPU_CTRL_REVERB_ENABLE)) {
        *out_l = 0;
        *out_r = 0;
        return;
    }

    int16_t in_coef_l = (int16_t)spu->reverb_regs[REVERB_REG_IN_COEF_L / 2];
    int16_t in_coef_r = (int16_t)spu->reverb_regs[REVERB_REG_IN_COEF_R / 2];
    int16_t iir_coef  = (int16_t)spu->reverb_regs[REVERB_REG_IIR_COEF / 2];
    int16_t iir_alpha = (int16_t)spu->reverb_regs[REVERB_REG_IIR_ALPHA / 2];
    int16_t fb_alpha  = (int16_t)spu->reverb_regs[REVERB_REG_FB_ALPHA / 2];
    int16_t fb_x      = (int16_t)spu->reverb_regs[REVERB_REG_FB_X / 2];

    int16_t acc_a = (int16_t)spu->reverb_regs[REVERB_REG_ACC_COEF_A / 2];
    int16_t acc_b = (int16_t)spu->reverb_regs[REVERB_REG_ACC_COEF_B / 2];
    int16_t acc_c = (int16_t)spu->reverb_regs[REVERB_REG_ACC_COEF_C / 2];
    int16_t acc_d = (int16_t)spu->reverb_regs[REVERB_REG_ACC_COEF_D / 2];

    int32_t iir_in_a = ((int32_t)reverb_read(spu, spu->reverb_regs[REVERB_REG_IIR_SRC_A0 / 2]) * iir_coef) >> 14;
    iir_in_a += ((input_l * in_coef_l) >> 14);
    iir_in_a >>= 1;

    int32_t iir_in_b = ((int32_t)reverb_read(spu, spu->reverb_regs[REVERB_REG_IIR_SRC_B0 / 2]) * iir_coef) >> 14;
    iir_in_b += ((input_r * in_coef_r) >> 14);
    iir_in_b >>= 1;

    int32_t iir_dest_a = reverb_read(spu, spu->reverb_regs[REVERB_REG_IIR_DEST_A0 / 2]);
    int32_t iir_val_a;
    iiasm(iir_alpha, &iir_val_a, iir_dest_a);
    int32_t iir_a = ((iir_in_a * iir_alpha) >> 14) + (iir_val_a >> 14);
    iir_a >>= 1;

    int32_t iir_dest_b = reverb_read(spu, spu->reverb_regs[REVERB_REG_IIR_DEST_B0 / 2]);
    int32_t iir_val_b;
    iiasm(iir_alpha, &iir_val_b, iir_dest_b);
    int32_t iir_b = ((iir_in_b * iir_alpha) >> 14) + (iir_val_b >> 14);
    iir_b >>= 1;

    reverb_write(spu, spu->reverb_regs[REVERB_REG_IIR_DEST_A0 / 2] + 1, (int16_t)iir_a);
    reverb_write(spu, spu->reverb_regs[REVERB_REG_IIR_DEST_B0 / 2] + 1, (int16_t)iir_b);

    int32_t acc_l = 0;
    acc_l += (int32_t)reverb_read(spu, spu->reverb_regs[REVERB_REG_ACC_SRC_A0 / 2]) * acc_a;
    acc_l += (int32_t)reverb_read(spu, spu->reverb_regs[REVERB_REG_ACC_SRC_B0 / 2]) * acc_b;
    acc_l += (int32_t)reverb_read(spu, spu->reverb_regs[REVERB_REG_ACC_SRC_C0 / 2]) * acc_c;
    acc_l += (int32_t)reverb_read(spu, spu->reverb_regs[REVERB_REG_ACC_SRC_D0 / 2]) * acc_d;
    acc_l >>= 14;

    uint32_t fb_src_a = spu->reverb_regs[REVERB_REG_FB_SRC_A / 2];
    uint32_t fb_src_b = spu->reverb_regs[REVERB_REG_FB_SRC_B / 2];
    uint32_t mix_a0   = spu->reverb_regs[REVERB_REG_MIX_DEST_A0 / 2];
    uint32_t mix_b0   = spu->reverb_regs[REVERB_REG_MIX_DEST_B0 / 2];

    int32_t fb_a = reverb_read(spu, mix_a0 - fb_src_a);
    int32_t fb_b = reverb_read(spu, mix_b0 - fb_src_b);

    int32_t mda = (acc_l + ((fb_a * (int32_t)(-fb_alpha)) >> 14)) >> 1;
    int32_t mdb = fb_a + (((mda * fb_alpha) >> 14) + ((fb_b * (int32_t)(-fb_x)) >> 14));

    int32_t out = fb_b + ((mdb * fb_x) >> 15);

    reverb_write(spu, mix_a0, (int16_t)mda);
    reverb_write(spu, mix_b0, (int16_t)mdb);

    int32_t clamped = out;
    if (clamped > 32767) clamped = 32767;
    if (clamped < -32768) clamped = -32768;

    int32_t vol_l = (int32_t)(int16_t)spu->reverb_vol_left;
    int32_t vol_r = (int32_t)(int16_t)spu->reverb_vol_right;

    *out_l = (clamped * vol_l) >> 15;
    *out_r = (clamped * vol_r) >> 15;

    spu->reverb_current_addr++;
    uint32_t reverb_mask = (SPU_RAM_SIZE / 2) - 1;
    if (spu->reverb_current_addr > reverb_mask) {
        spu->reverb_current_addr = (uint32_t)spu->reverb_base;
    }
}

void spu_reverb_init(Spu* spu) {
    spu->reverb_current_addr = 0;
    memset(spu->reverb_regs, 0, sizeof(spu->reverb_regs));
}

/* =========================================================================
 * Single Sample Generation
 * ========================================================================= */

static void spu_generate_one_sample(Spu* spu, struct Interconnect* inter, int16_t* left_out, int16_t* right_out) {
    /* Process key on/off at start of batch */
    spu_process_key_on_off(spu);

    /* Mix all 24 voices */
    int32_t mix_l = 0;
    int32_t mix_r = 0;

    for (int v = 0; v < NUM_VOICES; v++) {
        int16_t vl, vr;
        spu_voice_generate_sample(spu, inter, v, &vl, &vr);
        mix_l += vl;
        mix_r += vr;
    }

    /* Clamp voice sum */
    mix_l = clamp16(mix_l);
    mix_r = clamp16(mix_r);

    /* CD audio mixing through SPU */
    if (spu->control & SPU_CTRL_CD_AUDIO_EN) {
        mix_l += (int32_t)spu->cd_audio_left;
        mix_r += (int32_t)spu->cd_audio_right;
        mix_l = clamp16(mix_l);
        mix_r = clamp16(mix_r);
    }

    /* Write capture buffer */
    capture_write(spu, 0, spu->cd_audio_left);
    capture_write(spu, 1, spu->cd_audio_right);
    capture_write(spu, 2, (int16_t)spu->voices[1].last_volume);
    capture_write(spu, 3, (int16_t)spu->voices[3].last_volume);
    capture_increment(spu);

    /* Noise tick */
    noise_tick(spu);

    /* Reverb */
    int32_t rev_l, rev_r;
    reverb_process(spu, mix_l, mix_r, &rev_l, &rev_r);

    if (spu->control & SPU_CTRL_REVERB_ENABLE) {
        mix_l = clamp16(mix_l + (rev_l >> 2));
        mix_r = clamp16(mix_r + (rev_r >> 2));
    }

    /* Apply main volume */
    int32_t mv_l = (spu->muted) ? 0 : (int32_t)(int16_t)spu->main_vol_left;
    int32_t mv_r = (spu->muted) ? 0 : (int32_t)(int16_t)spu->main_vol_right;

    int32_t final_l = clamp16((mix_l * mv_l) >> 15);
    int32_t final_r = clamp16((mix_r * mv_r) >> 15);

    /* One-shot diagnostics */
    static bool mix_logged = false;
    if (!mix_logged && (mix_l != 0 || mix_r != 0)) {
        mix_logged = true;
        LOG_SPU_INFO("[SPU] First non-zero voice mix: mix=(%d,%d) mvol=(%d,%d) muted=%d final=(%d,%d)",
                     mix_l, mix_r, mv_l, mv_r, spu->muted ? 1 : 0, final_l, final_r);
    }
    static bool silent_warn_logged = false;
    if (!silent_warn_logged && spu->total_samples_generated > 88200 && mix_l == 0 && mix_r == 0) {
        silent_warn_logged = true;
        LOG_SPU_WARN("[SPU] 2s of silence: mvol=(%d,%d) muted=%d total_keys=%u",
                     mv_l, mv_r, spu->muted ? 1 : 0, spu->total_key_on_events);
    }

    *left_out = (int16_t)final_l;
    *right_out = (int16_t)final_r;

    /* Update peak levels for audio meter */
    int32_t abs_l = final_l < 0 ? -final_l : final_l;
    int32_t abs_r = final_r < 0 ? -final_r : final_r;
    if (abs_l > spu->peak_level_left) spu->peak_level_left = abs_l;
    if (abs_r > spu->peak_level_right) spu->peak_level_right = abs_r;
}

/* =========================================================================
 * SPU Step (called from main loop with CPU cycles)
 * ========================================================================= */

void spu_step(struct Interconnect* inter, uint32_t cpu_cycles) {
    Spu* spu = &inter->spu;
    spu->spu_tick_counter += cpu_cycles;

    while (spu->spu_tick_counter >= CPU_TICKS_PER_SPU_TICK) {
        spu->spu_tick_counter -= CPU_TICKS_PER_SPU_TICK;

        /* Generate one stereo sample */
        int16_t l, r;
        spu_generate_one_sample(spu, inter, &l, &r);

        /* Push to circular buffer */
        if (spu->sample_buf_count < SPU_SAMPLE_BUFFER_SIZE) {
            int tail = spu->sample_buf_tail * 2;
            spu->sample_buffer[tail] = l;
            spu->sample_buffer[tail + 1] = r;
            spu->sample_buf_tail = (spu->sample_buf_tail + 1) % SPU_SAMPLE_BUFFER_SIZE;
            spu->sample_buf_count++;
            spu->total_samples_generated++;
        }
    }

    /* Decay peak levels */
    spu->peak_level_left = (spu->peak_level_left * 15) >> 4;
    spu->peak_level_right = (spu->peak_level_right * 15) >> 4;
}

/* =========================================================================
 * SPU Get Samples (for SDL callback or other consumers)
 * ========================================================================= */

int spu_get_samples(Spu* spu, int16_t* buffer, int max_samples) {
    int count = 0;
    while (count < max_samples && spu->sample_buf_count > 0) {
        int head = spu->sample_buf_head * 2;
        buffer[count * 2]     = spu->sample_buffer[head];
        buffer[count * 2 + 1] = spu->sample_buffer[head + 1];
        spu->sample_buf_head = (spu->sample_buf_head + 1) % SPU_SAMPLE_BUFFER_SIZE;
        spu->sample_buf_count--;
        count++;
    }
    return count;
}

/* =========================================================================
 * SPU Fill Audio (SDL callback helper)
 * ========================================================================= */

void spu_fill_audio(Spu* spu, int16_t* stream, int num_stereo_samples) {
    int filled = spu_get_samples(spu, stream, num_stereo_samples);

    /* Fill remaining with silence if buffer underrun */
    for (int i = filled; i < num_stereo_samples; i++) {
        stream[i * 2] = 0;
        stream[i * 2 + 1] = 0;
    }
}

/* =========================================================================
 * Legacy spu_generate_samples (kept for compatibility)
 * ========================================================================= */

void spu_generate_samples(Spu* spu, int16_t* buffer, int num_samples) {
    for (int s = 0; s < num_samples; s++) {
        int16_t l, r;
        spu_generate_one_sample(spu, NULL, &l, &r);
        buffer[s * 2] = l;
        buffer[s * 2 + 1] = r;
    }
}
