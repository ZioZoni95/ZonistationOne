#include "spu.h"
#include "log.h"

/* =========================================================================
 * ADSR Envelope Implementation
 * ========================================================================= */

static int32_t clamp16(int32_t v) {
    if (v > 32767) return 32767;
    if (v < -32768) return -32768;
    return v;
}

static bool adsr_tick_step(SpuVoice* voice, bool decreasing, bool exponential, uint8_t rate,
                           int32_t* current) {
    if (rate >= 0x7F) return true;

    int rate_shift = rate >> 2;
    int rate_mode = rate & 0x03;

    int32_t increment;
    if (rate_shift < 0x10) {
        increment = 1 << (11 - rate_shift + 3);
    } else {
        increment = 0x8000 >> (rate_shift - 11);
    }

    voice->adsr_counter += increment;
    if (!(voice->adsr_counter & 0x8000)) {
        return false;
    }
    voice->adsr_counter = 0;

    int shift_base = 11 - (rate_shift > 10 ? 10 : rate_shift);
    int32_t step = (int32_t)(7 - rate_mode) << (shift_base + 3);

    int32_t new_level;
    if (decreasing) {
        if (exponential) {
            new_level = *current - (((*current) * (int64_t)step) >> 15);
        } else {
            new_level = *current - step;
        }
        if (new_level < 0) new_level = 0;
    } else {
        if (exponential && *current > 0x6000) {
            step >>= 2;
        }
        new_level = *current + step;
        if (new_level > 32767) new_level = 32767;
    }

    *current = new_level;
    voice->adsr_volume = (int16_t)new_level;
    return true;
}

static void adsr_update_target(SpuVoice* voice) {
    switch (voice->adsr_phase) {
        case ADSR_PHASE_ATTACK:
            voice->adsr_target = 32767;
            break;
        case ADSR_PHASE_DECAY: {
            int sustain_level = voice->adsr_low & 0x0F;
            voice->adsr_target = (sustain_level + 1) * 0x800;
            break;
        }
        case ADSR_PHASE_SUSTAIN:
            voice->adsr_target = 0;
            break;
        case ADSR_PHASE_RELEASE:
            voice->adsr_target = 0;
            break;
        case ADSR_PHASE_OFF:
        default:
            voice->adsr_target = 0;
            break;
    }
}

void spu_adsr_process(SpuVoice* voice) {
    if (voice->adsr_phase == ADSR_PHASE_OFF) {
        voice->adsr_volume = 0;
        return;
    }

    int32_t current = voice->adsr_volume;

    switch (voice->adsr_phase) {
        case ADSR_PHASE_ATTACK: {
            bool exp = !!(voice->adsr_low & (1 << 7));
            uint8_t rate = (voice->adsr_low >> 8) & 0x7F;
            adsr_tick_step(voice, false, exp, rate, &current);
            if (voice->adsr_volume >= 32767) {
                voice->adsr_volume = 32767;
                voice->adsr_phase = ADSR_PHASE_DECAY;
                adsr_update_target(voice);
                voice->adsr_counter = 0;
            }
            break;
        }
        case ADSR_PHASE_DECAY: {
            uint8_t rate = ((voice->adsr_low >> 4) & 0x0F) << 2;
            adsr_tick_step(voice, true, true, rate, &current);
            if (voice->adsr_volume <= voice->adsr_target) {
                voice->adsr_phase = ADSR_PHASE_SUSTAIN;
                adsr_update_target(voice);
                voice->adsr_counter = 0;
            }
            break;
        }
        case ADSR_PHASE_SUSTAIN: {
            bool dec = !!(voice->adsr_high & (1 << 6));
            bool exp = !!(voice->adsr_high & (1 << 7));
            uint8_t rate = voice->adsr_high & 0x7F;
            adsr_tick_step(voice, dec, exp, rate, &current);
            if (voice->adsr_volume <= 0) {
                voice->adsr_phase = ADSR_PHASE_RELEASE;
                adsr_update_target(voice);
                voice->adsr_counter = 0;
            }
            break;
        }
        case ADSR_PHASE_RELEASE: {
            bool exp = !!(voice->adsr_high & (1 << 5));
            uint8_t rate = ((voice->adsr_high >> 16) & 0x1F) << 2;
            adsr_tick_step(voice, true, exp, rate, &current);
            break;
        }
        case ADSR_PHASE_OFF:
            voice->adsr_volume = 0;
            break;
    }
}
