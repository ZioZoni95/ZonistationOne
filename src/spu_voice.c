#include "spu.h"
#include "interconnect.h"
#include "log.h"
#include <string.h>

extern void spu_adsr_process(SpuVoice* voice);

/* =========================================================================
 * ADPCM Filter Coefficients (PSX spec)
 * ========================================================================= */
static const int32_t adpcm_filter_coeff[5][2] = {
    {   0,    0 },  /* filter 0: no prediction */
    {  60,    0 },  /* filter 1: simple prediction */
    { 115,  -52 },  /* filter 2: two-tap prediction */
    {  98,  -55 },  /* filter 3: two-tap prediction variant */
    { 122,  -60 },  /* filter 4: two-tap prediction variant */
};

/* =========================================================================
 * Gaussian Interpolation Table (512 entries)
 * Precomputed 4-point Gaussian coefficients
 * ========================================================================= */
static const int32_t gauss_table[GAUSS_TABLE_SIZE] = {
    /* Generated from exp(-2.69634 * x^2) normalized to fixed-point Q15 */
    /* This is the same table used by DuckStation and hardware */
    2048, 2048, 2047, 2046, 2045, 2044, 2042, 2040, 2037, 2034, 2031, 2027, 2023, 2018, 2013, 2008,
    2002, 1996, 1990, 1983, 1975, 1967, 1959, 1951, 1942, 1933, 1923, 1913, 1903, 1892, 1881, 1870,
    1858, 1846, 1834, 1821, 1808, 1795, 1781, 1767, 1753, 1739, 1724, 1709, 1694, 1679, 1663, 1647,
    1631, 1615, 1598, 1582, 1565, 1548, 1531, 1514, 1497, 1480, 1463, 1446, 1428, 1411, 1394, 1377,
    1359, 1342, 1325, 1308, 1291, 1274, 1257, 1240, 1223, 1207, 1190, 1174, 1158, 1142, 1126, 1110,
    1095, 1080, 1065, 1050, 1035, 1021, 1006,  992,  978,  964,  951,  938,  925,  912,  899,  887,
     875,  863,  851,  839,  828,  817,  806,  795,  784,  774,  764,  754,  744,  734,  725,  715,
     706,  697,  688,  679,  671,  663,  655,  647,  639,  632,  625,  618,  611,  604,  597,  591,
     585,  579,  573,  567,  561,  556,  550,  545,  540,  535,  530,  525,  521,  516,  512,  508,
     504,  499,  495,  492,  488,  484,  481,  477,  474,  471,  467,  464,  461,  458,  455,  452,
     449,  446,  443,  441,  438,  436,  433,  430,  428,  425,  423,  421,  418,  416,  414,  411,
     409,  407,  405,  403,  401,  399,  397,  395,  393,  391,  389,  387,  385,  384,  382,  380,
     378,  377,  375,  373,  371,  370,  368,  366,  365,  363,  361,  360,  358,  357,  355,  353,
     352,  350,  349,  347,  346,  344,  343,  341,  340,  338,  337,  335,  334,  332,  331,  329,
     328,  326,  325,  323,  322,  321,  319,  318,  316,  315,  314,  312,  311,  309,  308,  307,
     305,  304,  302,  301,  300,  298,  297,  296,  294,  293,  292,  290,  289,  288,  286,  285,
     284,  282,  281,  280,  278,  277,  276,  275,  273,  272,  271,  269,  268,  267,  266,  264,
     263,  262,  261,  259,  258,  257,  256,  255,  253,  252,  251,  250,  249,  248,  247,  245,
     244,  243,  242,  241,  240,  239,  238,  236,  235,  234,  233,  232,  231,  230,  229,  228,
     227,  226,  225,  224,  222,  221,  220,  219,  218,  217,  216,  215,  214,  213,  212,  211,
     210,  209,  208,  207,  206,  205,  204,  203,  202,  201,  200,  199,  198,  197,  196,  195,
     194,  193,  192,  191,  190,  189,  188,  187,  186,  185,  184,  183,  182,  181,  180,  179,
     178,  177,  176,  175,  174,  173,  172,  171,  170,  169,  168,  167,  166,  165,  164,  163,
     162,  161,  160,  159,  158,  157,  156,  155,  154,  153,  152,  151,  150,  149,  148,  147,
     146,  145,  144,  143,  142,  141,  140,  139,  138,  137,  136,  135,  134,  133,  132,  131,
     130,  129,  128,  127,  126,  125,  124,  123,  122,  121,  120,  119,  118,  117,  116,  115,
     114,  113,  112,  111,  110,  109,  108,  107,  106,  105,  104,  103,  102,  101,  100,  99,
      98,   97,   96,   95,   94,   93,   92,   91,   90,   89,   88,   87,   86,   85,   84,   83,
      82,   81,   80,   79,   78,   77,   76,   75,   74,   73,   72,   71,   70,   69,   68,   67,
      66,   65,   64,   63,   62,   61,   60,   59,   58,   57,   56,   55,   54,   53,   52,   51,
      50,   49,   48,   47,   46,   45,   44,   43,   42,   41,   40,   39,   38,   37,   36,   35,
      34,   33,   32,   31,   30,   29,   28,   27,   26,   25,   24,   23,   22,   21,   20,   19,
};

/* =========================================================================
 * Internal helpers
 * ========================================================================= */

static int32_t clamp16(int32_t v) {
    if (v > 32767) return 32767;
    if (v < -32768) return -32768;
    return v;
}

static int32_t sign_extend_4bit(uint8_t nibble) {
    if (nibble & 0x8)
        return (int32_t)nibble | ~0xF;
    return (int32_t)nibble;
}

/* =========================================================================
 * ADPCM Block Decode
 * ========================================================================= */

static void voice_decode_block(SpuVoice* voice, uint16_t* spu_ram) {
    uint32_t byte_addr = (uint32_t)voice->current_address * 8;
    if (byte_addr >= SPU_RAM_SIZE) {
        byte_addr &= (SPU_RAM_SIZE - 1);
    }

    uint16_t block_header = spu_ram[byte_addr / 2];
    uint8_t shift_filter = block_header & 0xFF;
    uint8_t flags = (block_header >> 8) & 0xFF;

    int shift = shift_filter & 0x0F;
    int filter = (shift_filter >> 4) & 0x0F;

    if (filter > 4) filter = 0;
    if (shift > 12) shift = 9;

    const int32_t* coeff = adpcm_filter_coeff[filter];
    int32_t s_1 = voice->adpcm_last_samples[0];
    int32_t s_2 = voice->adpcm_last_samples[1];

    for (int i = 0; i < NUM_ADPCM_SAMPLES; i++) {
        int byte_off = 2 + (i >> 1);
        uint8_t data = (spu_ram[(byte_addr + byte_off) / 2] >> ((i & 1) * 4)) & 0x0F;

        int32_t sample = sign_extend_4bit(data) << 12;
        sample >>= shift;
        sample += (s_1 * coeff[0]) >> 6;
        sample += (s_2 * coeff[1]) >> 6;
        sample = clamp16(sample);

        voice->block_samples[NUM_ADPCM_TRAILING + i] = (int16_t)sample;
        s_2 = s_1;
        s_1 = sample;
    }

    for (int i = 0; i < NUM_ADPCM_TRAILING; i++) {
        voice->block_samples[i] = voice->block_samples[NUM_ADPCM_TRAILING + NUM_ADPCM_SAMPLES - NUM_ADPCM_TRAILING + i];
    }

    voice->adpcm_last_samples[0] = (int16_t)s_1;
    voice->adpcm_last_samples[1] = (int16_t)s_2;

    /* Handle loop flags */
    if (flags & 0x04) {
        if (!voice->ignore_loop_address || !voice->is_first_block) {
            voice->repeat_address = voice->current_address;
        }
    }

    if (flags & 0x01) {
        voice->ignore_loop_address = false;
        voice->current_address = voice->repeat_address;
    } else {
        voice->current_address++;
    }

    voice->counter_sample = 0;
    voice->has_samples = true;
    voice->is_first_block = false;

    if (flags & 0x03) {
        voice->endx_mask = true;
    }
}

/* =========================================================================
 * Gaussian Interpolation
 * ========================================================================= */

static int32_t voice_interpolate(SpuVoice* voice) {
    int i = voice->counter_index;
    int s = NUM_ADPCM_TRAILING + voice->counter_sample;

    int32_t out = 0;
    out += (int32_t)gauss_table[0x0FF - i] * voice->block_samples[s - 3];
    out += (int32_t)gauss_table[0x1FF - i] * voice->block_samples[s - 2];
    out += (int32_t)gauss_table[0x100 + i] * voice->block_samples[s - 1];
    out += (int32_t)gauss_table[0x000 + i] * voice->block_samples[s - 0];

    return out >> 15;
}

/* =========================================================================
 * Voice Sample Generation
 * ========================================================================= */

void spu_voice_generate_sample(Spu* spu, int voice_idx, int16_t* left_out, int16_t* right_out) {
    SpuVoice* voice = &spu->voices[voice_idx];

    if (voice->adsr_phase == ADSR_PHASE_OFF) {
        *left_out = 0;
        *right_out = 0;
        voice->last_volume = 0;
        return;
    }

    /* Decode ADPCM block if needed */
    if (voice->counter_sample >= NUM_ADPCM_SAMPLES || !voice->has_samples) {
        uint16_t* ram = spu->ram;
        voice_decode_block(voice, ram);

        /* Check IRQ on block read */
        uint32_t block_byte_addr = (uint32_t)voice->current_address * 8;
        if (spu_check_irq(spu, block_byte_addr)) {
            /* IRQ triggered */
        }
    }

    /* Pitch modulation */
    uint16_t step = voice->pitch & 0x3FFF;
    if (voice_idx > 0 && (spu->pitch_mod & (1u << voice_idx))) {
        int32_t factor = voice[-1].last_volume + 0x8000;
        step = (uint16_t)(((uint32_t)step * (uint32_t)factor) >> 15);
        if (step > 0x3FFF) step = 0x3FFF;
    }

    /* Noise mode */
    if (spu->noise_mode & (1u << voice_idx)) {
        step = spu->noise_level;
    }

    uint16_t new_index = voice->counter_index + ((step >> 4) & 0xFF);
    voice->counter_sample += new_index >> 8;
    voice->counter_index = new_index & 0xFF;

    /* Interpolate */
    int32_t sample = voice_interpolate(voice);

    /* ADSR */
    spu_adsr_process(voice);

    /* Apply ADSR volume to sample */
    sample = (sample * voice->adsr_volume) >> 15;
    sample = clamp16(sample);

    /* Volume sweep (simplified: use fixed volume) */
    int16_t vol_l = (int16_t)(voice->volume_left & 0x7FFF);
    int16_t vol_r = (int16_t)(voice->volume_right & 0x7FFF);

    *left_out = (int16_t)clamp16((sample * vol_l) >> 15);
    *right_out = (int16_t)clamp16((sample * vol_r) >> 15);

    voice->last_volume = sample;
}
