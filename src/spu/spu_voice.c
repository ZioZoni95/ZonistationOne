/*
 * SPU voice decode & mixing — ported 1:1 from pcsx-redux (Pete Bernert / PCSX-Redux authors)
 * Original: pcsx-redux/src/spu/spu.cc  (GPL-2.0+)
 * Adapted to C for ZonistationOne.
 */

#include "spu.h"
#include "interconnect.h"
#include "log.h"
#include <string.h>

extern int spu_adsr_mix(SpuVoice* voice);

/* =========================================================================
 * ADPCM filter coefficients (same as pcsx-redux f[5][2])
 * ========================================================================= */
static inline int adpcm_clamp16(int v) {
    if (v >  32767) return  32767;
    if (v < -32768) return -32768;
    return v;
}

static const int adpcm_f[5][2] = {
    {   0,    0 },
    {  60,    0 },
    { 115,  -52 },
    {  98,  -55 },
    { 122,  -60 },
};

/* =========================================================================
 * 4-point Gaussian interpolation — the hardware table and formula
 *
 * Transcribed from `DOCS/soundprocessingunitspu.md:225-291` ("The Gauss table
 * contains the following values") and applied with the interpolation given at
 * `:215-224`. 512 entries, indexed four at a time by the 8-bit index taken from
 * the pitch counter (`:201`, "Counter.Bit4..11 are used as 8bit gaussian
 * interpolation index" — bits 8..15 of our 16-bit fractional `spos`).
 *
 * Self-checking: the documentation notes at `:295-299` that the real table is
 * slightly bugged, each group of four summing to 7F7Fh..7F81h rather than the
 * theoretical 8000h. The transcription reproduces exactly that range, which is
 * a strong check that it was copied down correctly.
 *
 * This replaces a 1024-entry table taken from pcsx-redux (Chris Moeller), which
 * is a differently normalised variant used with a >>11 accumulator. The table
 * below is the console's own, and it comes from the hardware documentation.
 * ========================================================================= */
static const int gauss_table[512] = {
        -1,     -1,     -1,     -1,     -1,     -1,     -1,     -1,
        -1,     -1,     -1,     -1,     -1,     -1,     -1,     -1,
         0,      0,      0,      0,      0,      0,      0,      1,
         1,      1,      1,      2,      2,      2,      3,      3,
         3,      4,      4,      5,      5,      6,      7,      7,
         8,      9,      9,     10,     11,     12,     13,     14,
        15,     16,     17,     18,     19,     21,     22,     24,
        25,     27,     28,     30,     32,     33,     35,     37,
        39,     41,     44,     46,     48,     51,     53,     56,
        58,     61,     64,     67,     70,     73,     77,     80,
        84,     87,     91,     95,     99,    103,    107,    111,
       116,    120,    125,    130,    135,    140,    145,    150,
       156,    161,    167,    173,    179,    186,    192,    199,
       205,    212,    219,    227,    234,    242,    250,    257,
       266,    274,    283,    291,    300,    309,    319,    328,
       338,    348,    358,    369,    379,    390,    401,    412,
       424,    436,    448,    460,    473,    485,    498,    512,
       525,    539,    553,    567,    582,    597,    612,    627,
       643,    659,    675,    692,    708,    726,    743,    761,
       779,    797,    816,    835,    854,    874,    894,    914,
       935,    956,    977,    999,   1020,   1043,   1066,   1089,
      1112,   1136,   1160,   1184,   1209,   1234,   1260,   1286,
      1312,   1339,   1366,   1394,   1422,   1450,   1479,   1508,
      1537,   1567,   1598,   1628,   1660,   1691,   1723,   1756,
      1789,   1822,   1856,   1890,   1924,   1959,   1995,   2031,
      2067,   2104,   2141,   2179,   2217,   2256,   2295,   2334,
      2374,   2415,   2456,   2497,   2539,   2582,   2624,   2668,
      2712,   2756,   2801,   2846,   2892,   2938,   2985,   3032,
      3079,   3128,   3176,   3225,   3275,   3325,   3376,   3427,
      3479,   3531,   3584,   3637,   3691,   3745,   3799,   3855,
      3910,   3967,   4023,   4081,   4138,   4197,   4255,   4315,
      4374,   4435,   4495,   4557,   4619,   4681,   4744,   4807,
      4871,   4935,   5000,   5065,   5131,   5197,   5264,   5332,
      5399,   5468,   5536,   5606,   5676,   5746,   5817,   5888,
      5959,   6032,   6104,   6177,   6251,   6325,   6400,   6475,
      6550,   6626,   6702,   6779,   6856,   6934,   7012,   7091,
      7170,   7249,   7329,   7409,   7490,   7571,   7653,   7735,
      7817,   7900,   7983,   8066,   8150,   8234,   8319,   8404,
      8489,   8575,   8661,   8748,   8834,   8922,   9009,   9097,
      9185,   9273,   9362,   9451,   9541,   9630,   9720,   9811,
      9901,   9992,  10083,  10174,  10266,  10358,  10450,  10542,
     10635,  10727,  10820,  10913,  11007,  11100,  11194,  11288,
     11382,  11476,  11571,  11665,  11760,  11855,  11950,  12045,
     12140,  12236,  12331,  12427,  12522,  12618,  12714,  12809,
     12905,  13001,  13097,  13193,  13289,  13385,  13481,  13577,
     13673,  13769,  13865,  13961,  14056,  14152,  14248,  14343,
     14439,  14534,  14630,  14725,  14820,  14915,  15010,  15104,
     15199,  15293,  15387,  15481,  15575,  15669,  15762,  15855,
     15948,  16041,  16133,  16226,  16317,  16409,  16500,  16592,
     16682,  16773,  16863,  16953,  17042,  17131,  17220,  17308,
     17396,  17484,  17571,  17658,  17744,  17830,  17916,  18001,
     18086,  18170,  18254,  18337,  18420,  18502,  18584,  18665,
     18746,  18826,  18905,  18985,  19063,  19141,  19219,  19295,
     19372,  19447,  19522,  19597,  19671,  19744,  19816,  19888,
     19959,  20030,  20100,  20169,  20238,  20306,  20373,  20439,
     20505,  20570,  20634,  20698,  20760,  20822,  20884,  20944,
     21004,  21063,  21121,  21178,  21235,  21290,  21345,  21399,
     21452,  21505,  21556,  21607,  21657,  21706,  21754,  21801,
     21848,  21893,  21938,  21982,  22025,  22066,  22107,  22148,
     22187,  22225,  22262,  22299,  22334,  22369,  22402,  22435,
     22467,  22498,  22527,  22556,  22584,  22611,  22637,  22662,
     22686,  22709,  22731,  22752,  22772,  22791,  22809,  22826,
     22842,  22857,  22872,  22885,  22897,  22908,  22918,  22927,
     22935,  22942,  22948,  22953,  22957,  22960,  22962,  22963,
};

/* =========================================================================
 * Gauss interpolation — pcsx-redux 1:1
 * ========================================================================= */

static int voice_interpolate(SpuVoice* voice) {
    /* DOCS:215-224 —
     *   out  = (gauss[0FFh-i] * oldest) SAR 15
     *   out += (gauss[1FFh-i] * older ) SAR 15
     *   out += (gauss[100h+i] * old   ) SAR 15
     *   out += (gauss[000h+i] * new   ) SAR 15
     * The ring holds the four most recent samples, oldest first at gpos. */
    const int i = (voice->spos >> 8) & 0xFF;
    const int gpos = voice->gpos;
    const int oldest = (int)voice->gauss_ring[gpos];
    const int older  = (int)voice->gauss_ring[(gpos + 1) & 3];
    const int old    = (int)voice->gauss_ring[(gpos + 2) & 3];
    const int newest = (int)voice->gauss_ring[(gpos + 3) & 3];

    int out  = (gauss_table[0x0FF - i] * oldest) >> 15;
    out     += (gauss_table[0x1FF - i] * older ) >> 15;
    out     += (gauss_table[0x100 + i] * old   ) >> 15;
    out     += (gauss_table[0x000 + i] * newest) >> 15;
    return out;
}

static void store_interp(SpuVoice* voice, int fa) {
    if (fa > 32767)  fa =  32767;
    if (fa < -32767) fa = -32767;
    voice->gauss_ring[voice->gpos] = (int16_t)fa;
    voice->gpos = (voice->gpos + 1) & 3;
}

/* =========================================================================
 * ADPCM block decode — pcsx-redux MainThread decode loop, 1:1
 * ========================================================================= */

static void voice_decode_block(Spu* spu, struct Interconnect* inter, SpuVoice* voice) {
    uint8_t* start = (uint8_t*)spu->ram + voice->curr_addr;

    int predict_nr = (int)*start++;
    int shift_factor = predict_nr & 0xF;
    predict_nr >>= 4;
    int flags = (int)*start++;

    if (predict_nr > 4) predict_nr = 4;
    if (shift_factor > 12) shift_factor = 9;

    int s_1 = voice->s_1;
    int s_2 = voice->s_2;
    unsigned int nSample = 0;

    /* The decoded sample is saturated to 16 bits *before* it becomes filter
     * state. The SPU's datapath is 16-bit, and the equivalent CD-XA decoder is
     * explicit about it (`DOCS/cdromformat.md:836-837`, already applied in
     * `cdrom_audio.c`); DuckStation saturates here too (`spu.cpp:1930`).
     * Feeding the raw prediction back lets one overflowing nibble poison the
     * remaining 27 samples of the block, and leaves out-of-range values in SB[]
     * that are only clamped much later, after the envelope and volume have
     * already scaled them. */
    for (; nSample < 28; start++) {
        int d = (int)*start;
        int s = (d & 0x0F) << 12;
        if (s & 0x8000) s |= 0xFFFF0000;
        int fa = (s >> shift_factor) + ((s_1 * adpcm_f[predict_nr][0]) >> 6) + ((s_2 * adpcm_f[predict_nr][1]) >> 6);
        fa = adpcm_clamp16(fa);
        s_2 = s_1; s_1 = fa;
        voice->SB[nSample++] = fa;

        s = (d & 0xF0) << 8;
        if (s & 0x8000) s |= 0xFFFF0000;
        fa = (s >> shift_factor) + ((s_1 * adpcm_f[predict_nr][0]) >> 6) + ((s_2 * adpcm_f[predict_nr][1]) >> 6);
        fa = adpcm_clamp16(fa);
        s_2 = s_1; s_1 = fa;
        voice->SB[nSample++] = fa;
    }

    voice->s_1 = s_1;
    voice->s_2 = s_2;

    /* IRQ check on decoded block */
    if (spu->control & SPU_CTRL_IRQ9_ENABLE) {
        uint32_t block_end = voice->curr_addr + 16;
        spu_check_irq(spu, inter, block_end);
    }

    /* Flag 4: set loop point to start of this block */
    if ((flags & 4) && !voice->ignore_loop) {
        voice->loop_addr = voice->curr_addr;
        voice->repeat_address = (uint16_t)(voice->curr_addr >> 3);
        voice->loop_addr_set = true;
    }

    /* Advance current address */
    if (flags & 1) {
        /* End-of-sample: loop if flags==3 and loop set, else stop */
        if ((flags & 3) == 3 && voice->loop_addr_set) {
            voice->curr_addr = voice->loop_addr;
        } else {
            voice->curr_addr = 0xFFFFFFFF;  /* one-shot stop sentinel */
        }
        voice->endx_mask = true;
    } else {
        voice->curr_addr += 16;
        voice->endx_mask = false;
    }

    voice->SBPos = 0;

    LOG_SPU_TRACE("[SPU] Voice decode: flags=0x%02X loop=%d endx=%d curr=0x%06X",
                  flags, voice->loop_addr_set, voice->endx_mask, voice->curr_addr);
}

/* =========================================================================
 * Main voice sample generator — pcsx-redux MainThread inner loop, 1:1
 * Returns ADSR-mixed sample (before L/R volume).  Caller accumulates.
 * ========================================================================= */

int32_t spu_voice_get_sample(Spu* spu, struct Interconnect* inter, int voice_idx) {
    SpuVoice* voice = &spu->voices[voice_idx];

    if (!voice->on) {
        voice->sval = 0;
        return 0;
    }

    /* Key-off: transition ADSR to Release phase */
    if (voice->stop) {
        voice->adsr_state = ADSR_STATE_RELEASE;
        voice->stop = false;
    }

    /* Compute sinc for this sample (with optional pitch modulation) */
    int sinc = voice->sinc;
    if (voice_idx > 0 && (spu->pitch_mod >> voice_idx) & 1) {
        /* Frequency modulation from previous voice's sval */
        int prev = spu->voices[voice_idx - 1].sval;
        sinc = (int)(((int64_t)(32768 + prev) * sinc) >> 15);
        if (sinc < 1)        sinc = 1;
        if (sinc > 0x3FFF0)  sinc = 0x3FFF0;
    }

    /* Advance spos: decode samples into gauss ring until spos < 0x10000 */
    while (voice->spos >= 0x10000) {
        if (voice->SBPos == 28) {
            if (voice->curr_addr == 0xFFFFFFFF) {
                /* One-shot end: silence voice */
                voice->on = false;
                voice->EnvelopeVol = 0;
                voice->adsr_volume = 0;
                voice->sval = 0;
                return 0;
            }
            voice_decode_block(spu, inter, voice);
        }

        int fa = voice->SB[voice->SBPos++];
        store_interp(voice, fa);
        voice->spos -= 0x10000;
    }

    /* Get interpolated sample */
    int fa;
    if (spu->noise_mode & (1u << voice_idx)) {
        fa = (int)(int16_t)spu->noise_level;
    } else {
        fa = voice_interpolate(voice);
    }

    /* ADSR envelope mix — returns 0-32767 (15-bit, matches DuckStation precision) */
    int32_t adsr_vol = spu_adsr_mix(voice);
    int32_t mixed = ((int32_t)fa * adsr_vol) >> 15;

    /* Clamp (pcsx-redux clamps to ±0xFFFF for capture/fmod) */
    if (mixed >  0xFFFF) mixed =  0xFFFF;
    if (mixed < -0xFFFF) mixed = -0xFFFF;

    voice->sval = mixed;

    /* Advance spos for next call */
    voice->spos += sinc;

    /* Update ENDX status */
    if (voice->endx_mask) {
        spu->endx |= (1u << voice_idx);
        voice->endx_mask = false;
    }

    return mixed;
}

/* =========================================================================
 * Volume sweep tick — called once per sample per voice from spu_mixing.c
 * Implements PSX-SPX "Sweep Volume Control" algorithm.
 * Only runs when bit15 of the volume register is set (sweep mode).
 * ========================================================================= */
void spu_voice_sweep_tick(SpuVoice* voice) {
    /* Left channel — sweep mode (bit15=1).
     * PSX-SPX (DOCS/soundprocessingunitspu.md:366-387): bit14=Exp(0=lin,1=exp),
     * bit13=Direction(0=inc,1=dec), bit12=Phase, bits6-2=Shift, bits1-0=Step.
     * Bit 7 is documented as unused; reading the direction from it made every
     * sweep run the wrong way. */
    if (voice->volume_left & 0x8000) {
        uint16_t reg = voice->volume_left;
        int shift     = (reg >> 2) & 0x1F;
        int step_idx  = reg & 0x03;
        int decrease  = (reg >> 13) & 1;
        int exp_mode  = (reg >> 14) & 1;
        int threshold = (shift >= 11) ? (1 << (shift - 11)) : 1;
        if (++voice->vol_left_count >= threshold) {
            voice->vol_left_count = 0;
            int step = decrease ? (-8 + step_idx) : (7 - step_idx);
            if (shift < 11) step <<= (11 - shift);
            int32_t v;
            if (exp_mode && (decrease || voice->vol_left >= 0x6000))
                v = voice->vol_left + ((step * voice->vol_left) >> 15);
            else
                v = voice->vol_left + step;
            voice->vol_left = (v > 0x7FFF) ? 0x7FFF : (v < -0x8000) ? -0x8000 : (int)v;
        }
    }
    /* Right channel */
    if (voice->volume_right & 0x8000) {
        uint16_t reg = voice->volume_right;
        int shift     = (reg >> 2) & 0x1F;
        int step_idx  = reg & 0x03;
        int decrease  = (reg >> 13) & 1;
        int exp_mode  = (reg >> 14) & 1;
        int threshold = (shift >= 11) ? (1 << (shift - 11)) : 1;
        if (++voice->vol_right_count >= threshold) {
            voice->vol_right_count = 0;
            int step = decrease ? (-8 + step_idx) : (7 - step_idx);
            if (shift < 11) step <<= (11 - shift);
            int32_t v;
            if (exp_mode && (decrease || voice->vol_right >= 0x6000))
                v = voice->vol_right + ((step * voice->vol_right) >> 15);
            else
                v = voice->vol_right + step;
            voice->vol_right = (v > 0x7FFF) ? 0x7FFF : (v < -0x8000) ? -0x8000 : (int)v;
        }
    }
}
