/* SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 ZioZoni95
 * SPDX-FileCopyrightText: 2002 Pete Bernert and the PCSX-Redux authors
 *
 * Part of ZoniStation One, a PlayStation 1 emulator.
 * See LICENSE for the full licence text and THIRD-PARTY.md for the
 * components of this project that have other authors.
 */
/*
 * SPU ADSR envelope — ported 1:1 from pcsx-redux (Pete Bernert / PCSX-Redux authors)
 * Original: pcsx-redux/src/spu/adsr.cc  (GPL-2.0+)
 * Adapted to C for ZonistationOne.
 */

#include "spu.h"
#include <stdint.h>

/* =========================================================================
 * ADSR envelope tables (128 entries each)
 * denominator[n]   = (n<48) ? 1 : (1 << ((n>>2)-11))
 * numerator_inc[n] = (n<48) ? (7-(n&3))<<(11-(n>>2)) : (7-(n&3))
 * numerator_dec[n] = (n<48) ? (-8+(n&3))<<(11-(n>>2)) : (-8+(n&3))
 * ========================================================================= */

static int32_t adsr_denominator[128];
static int32_t adsr_numerator_inc[128];
static int32_t adsr_numerator_dec[128];
static int adsr_tables_ready = 0;

static void adsr_init_tables(void) {
    if (adsr_tables_ready) return;
    for (int n = 0; n < 128; n++) {
        adsr_denominator[n]   = (n < 48) ? 1 : (1 << ((n >> 2) - 11));
        adsr_numerator_inc[n] = (n < 48) ? (7 - (n & 3)) << (11 - (n >> 2)) : (7 - (n & 3));
        adsr_numerator_dec[n] = (n < 48) ? (-8 + (n & 3)) << (11 - (n >> 2)) : (-8 + (n & 3));
    }
    adsr_tables_ready = 1;
}

static inline int clamp_rate(int rate) {
    return (rate > 127) ? 127 : rate;
}

/* --- Attack --- */
static int adsr_attack(SpuVoice* voice) {
    int rate            = voice->attack_rate;
    int32_t EnvelopeVol  = voice->EnvelopeVol;
    int32_t EnvelopeVolF = voice->EnvelopeVolF;

    if (voice->attack_mode_exp && EnvelopeVol >= 0x6000)
        rate += 8;
    rate = clamp_rate(rate);

    EnvelopeVolF++;
    if (EnvelopeVolF >= adsr_denominator[rate]) {
        EnvelopeVolF = 0;
        EnvelopeVol += adsr_numerator_inc[rate];
    }

    if (EnvelopeVol >= 32767) {
        EnvelopeVol = 32767;
        voice->adsr_state = ADSR_STATE_DECAY;
    }

    voice->EnvelopeVol  = EnvelopeVol;
    voice->EnvelopeVolF = EnvelopeVolF;
    voice->adsr_volume  = (int16_t)EnvelopeVol;
    return EnvelopeVol;
}

/* --- Decay (always exponential per PSX hardware) --- */
static int adsr_decay(SpuVoice* voice) {
    const int rate       = clamp_rate(voice->decay_rate * 4);
    int32_t EnvelopeVol  = voice->EnvelopeVol;
    int32_t EnvelopeVolF = voice->EnvelopeVolF;

    EnvelopeVolF++;
    if (EnvelopeVolF >= adsr_denominator[rate]) {
        EnvelopeVolF = 0;
        EnvelopeVol += (adsr_numerator_dec[rate] * EnvelopeVol) >> 15;
    }

    if (EnvelopeVol < 0) EnvelopeVol = 0;

    if (((EnvelopeVol >> 11) & 0xF) <= voice->sustain_level)
        voice->adsr_state = ADSR_STATE_SUSTAIN;

    voice->EnvelopeVol  = EnvelopeVol;
    voice->EnvelopeVolF = EnvelopeVolF;
    voice->adsr_volume  = (int16_t)EnvelopeVol;
    return EnvelopeVol;
}

/* --- Sustain --- */
static int adsr_sustain(SpuVoice* voice) {
    int rate            = voice->sustain_rate;
    int32_t EnvelopeVol  = voice->EnvelopeVol;
    int32_t EnvelopeVolF = voice->EnvelopeVolF;

    if (voice->sustain_increase) {
        if (voice->sustain_mode_exp && EnvelopeVol >= 0x6000)
            rate += 8;
        rate = clamp_rate(rate);

        EnvelopeVolF++;
        if (EnvelopeVolF >= adsr_denominator[rate]) {
            EnvelopeVolF = 0;
            EnvelopeVol += adsr_numerator_inc[rate];
        }
        if (EnvelopeVol > 32767) EnvelopeVol = 32767;
    } else {
        rate = clamp_rate(rate);
        EnvelopeVolF++;
        if (EnvelopeVolF >= adsr_denominator[rate]) {
            EnvelopeVolF = 0;
            if (voice->sustain_mode_exp)
                EnvelopeVol += (adsr_numerator_dec[rate] * EnvelopeVol) >> 15;
            else
                EnvelopeVol += adsr_numerator_dec[rate];
        }
        if (EnvelopeVol < 0) EnvelopeVol = 0;
    }

    voice->EnvelopeVol  = EnvelopeVol;
    voice->EnvelopeVolF = EnvelopeVolF;
    voice->adsr_volume  = (int16_t)EnvelopeVol;
    return EnvelopeVol;
}

/* --- Release --- */
static int adsr_release(SpuVoice* voice) {
    const int rate       = clamp_rate(voice->release_rate * 4);
    int32_t EnvelopeVol  = voice->EnvelopeVol;
    int32_t EnvelopeVolF = voice->EnvelopeVolF;

    EnvelopeVolF++;
    if (EnvelopeVolF >= adsr_denominator[rate]) {
        EnvelopeVolF = 0;
        if (voice->release_mode_exp)
            EnvelopeVol += (adsr_numerator_dec[rate] * EnvelopeVol) >> 15;
        else
            EnvelopeVol += adsr_numerator_dec[rate];
    }

    if (EnvelopeVol < 0) {
        voice->adsr_state = ADSR_STATE_STOPPED;
        EnvelopeVol = 0;
        voice->on   = false;
    }

    voice->EnvelopeVol  = EnvelopeVol;
    voice->EnvelopeVolF = EnvelopeVolF;
    voice->adsr_volume  = (int16_t)EnvelopeVol;
    return EnvelopeVol;
}

/* =========================================================================
 * Public: spu_adsr_mix — called once per sample from spu_voice_get_sample
 * Returns mixing volume 0-32767 (15-bit, DuckStation-compatible precision)
 * ========================================================================= */
int spu_adsr_mix(SpuVoice* voice) {
    adsr_init_tables();

    switch (voice->adsr_state) {
        case ADSR_STATE_ATTACK:  return adsr_attack(voice);
        case ADSR_STATE_DECAY:   return adsr_decay(voice);
        case ADSR_STATE_SUSTAIN: return adsr_sustain(voice);
        case ADSR_STATE_RELEASE: return adsr_release(voice);
        default:                 return 0;
    }
}
