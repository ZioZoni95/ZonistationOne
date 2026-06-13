/**
 * gte_internal.h
 * Private GTE helper functions and register macros.
 * Include only from gte.c and gte_ops.c.
 */
#ifndef GTE_INTERNAL_H
#define GTE_INTERNAL_H

#include "gte.h"
#include <stdint.h>
#include <stdbool.h>

// --- GTE Data Register Indices ---
#define GTE_REG_V0_XY   0
#define GTE_REG_V0_Z    1
#define GTE_REG_V1_XY   2
#define GTE_REG_V1_Z    3
#define GTE_REG_V2_XY   4
#define GTE_REG_V2_Z    5
#define GTE_REG_RGBC    6
#define GTE_REG_OTZ     7
#define GTE_REG_IR0     8
#define GTE_REG_IR1     9
#define GTE_REG_IR2     10
#define GTE_REG_IR3     11
#define GTE_REG_SXY0    12
#define GTE_REG_SXY1    13
#define GTE_REG_SXY2    14
#define GTE_REG_SXYP    15
#define GTE_REG_SZ0     16
#define GTE_REG_SZ1     17
#define GTE_REG_SZ2     18
#define GTE_REG_SZ3     19
#define GTE_REG_RGB0    20
#define GTE_REG_RGB1    21
#define GTE_REG_RGB2    22
#define GTE_REG_RES1    23
#define GTE_REG_MAC0    24
#define GTE_REG_MAC1    25
#define GTE_REG_MAC2    26
#define GTE_REG_MAC3    27
#define GTE_REG_IRGB    28
#define GTE_REG_ORGB    29
#define GTE_REG_LZCS    30
#define GTE_REG_LZCR    31

// --- GTE Control Register Indices ---
#define GTE_CTL_RT11RT12 0
#define GTE_CTL_RT13RT21 1
#define GTE_CTL_RT22RT23 2
#define GTE_CTL_RT31RT32 3
#define GTE_CTL_RT33     4
#define GTE_CTL_TRX      5
#define GTE_CTL_TRY      6
#define GTE_CTL_TRZ      7
#define GTE_CTL_L11L12   8
#define GTE_CTL_L13L21   9
#define GTE_CTL_L22L23   10
#define GTE_CTL_L31L32   11
#define GTE_CTL_L33      12
#define GTE_CTL_RBK      13
#define GTE_CTL_GBK      14
#define GTE_CTL_BBK      15
#define GTE_CTL_LR1LR2   16
#define GTE_CTL_LR3LG1   17
#define GTE_CTL_LG2LG3   18
#define GTE_CTL_LB1LB2   19
#define GTE_CTL_LB3      20
#define GTE_CTL_RFC      21
#define GTE_CTL_GFC      22
#define GTE_CTL_BFC      23
#define GTE_CTL_OFX      24
#define GTE_CTL_OFY      25
#define GTE_CTL_H        26
#define GTE_CTL_DQA      27
#define GTE_CTL_DQB      28
#define GTE_CTL_ZSF3     29
#define GTE_CTL_ZSF4     30
#define GTE_CTL_FLAG     31

// --- UNR Reciprocal Table (for perspective division) ---
static const uint8_t unr_table[257] = {
    0xFF, 0xFD, 0xFB, 0xF9, 0xF7, 0xF5, 0xF3, 0xF1, 0xEF, 0xEE, 0xEC, 0xEA, 0xE8, 0xE6, 0xE4, 0xE3,
    0xE1, 0xDF, 0xDD, 0xDC, 0xDA, 0xD8, 0xD6, 0xD5, 0xD3, 0xD1, 0xD0, 0xCE, 0xCD, 0xCB, 0xC9, 0xC8,
    0xC6, 0xC5, 0xC3, 0xC1, 0xC0, 0xBE, 0xBD, 0xBB, 0xBA, 0xB8, 0xB7, 0xB5, 0xB4, 0xB2, 0xB1, 0xB0,
    0xAE, 0xAD, 0xAB, 0xAA, 0xA9, 0xA7, 0xA6, 0xA4, 0xA3, 0xA2, 0xA0, 0x9F, 0x9E, 0x9C, 0x9B, 0x9A,
    0x99, 0x97, 0x96, 0x95, 0x94, 0x92, 0x91, 0x90, 0x8F, 0x8D, 0x8C, 0x8B, 0x8A, 0x89, 0x87, 0x86,
    0x85, 0x84, 0x83, 0x82, 0x81, 0x7F, 0x7E, 0x7D, 0x7C, 0x7B, 0x7A, 0x79, 0x78, 0x77, 0x75, 0x74,
    0x73, 0x72, 0x71, 0x70, 0x6F, 0x6E, 0x6D, 0x6C, 0x6B, 0x6A, 0x69, 0x68, 0x67, 0x66, 0x65, 0x64,
    0x63, 0x62, 0x61, 0x60, 0x5F, 0x5E, 0x5D, 0x5D, 0x5C, 0x5B, 0x5A, 0x59, 0x58, 0x57, 0x56, 0x55,
    0x54, 0x53, 0x53, 0x52, 0x51, 0x50, 0x4F, 0x4E, 0x4D, 0x4D, 0x4C, 0x4B, 0x4A, 0x49, 0x48, 0x48,
    0x47, 0x46, 0x45, 0x44, 0x43, 0x43, 0x42, 0x41, 0x40, 0x3F, 0x3F, 0x3E, 0x3D, 0x3C, 0x3C, 0x3B,
    0x3A, 0x39, 0x39, 0x38, 0x37, 0x36, 0x36, 0x35, 0x34, 0x33, 0x33, 0x32, 0x31, 0x31, 0x30, 0x2F,
    0x2E, 0x2E, 0x2D, 0x2C, 0x2C, 0x2B, 0x2A, 0x2A, 0x29, 0x28, 0x28, 0x27, 0x26, 0x26, 0x25, 0x24,
    0x24, 0x23, 0x22, 0x22, 0x21, 0x20, 0x20, 0x1F, 0x1E, 0x1E, 0x1D, 0x1D, 0x1C, 0x1B, 0x1B, 0x1A,
    0x19, 0x19, 0x18, 0x18, 0x17, 0x16, 0x16, 0x15, 0x15, 0x14, 0x14, 0x13, 0x12, 0x12, 0x11, 0x11,
    0x10, 0x0F, 0x0F, 0x0E, 0x0E, 0x0D, 0x0D, 0x0C, 0x0C, 0x0B, 0x0A, 0x0A, 0x09, 0x09, 0x08, 0x08,
    0x07, 0x07, 0x06, 0x06, 0x05, 0x05, 0x04, 0x04, 0x03, 0x03, 0x02, 0x02, 0x01, 0x01, 0x00, 0x00,
    0x00
};

// --- Primitive Helpers ---

static inline void set_flag(Gte* gte, int bit) {
    gte->control[GTE_CTL_FLAG] |= (1 << bit);
}

static inline int64_t sign_extend_44(int64_t val) {
    if (val & (1ULL << 43))
        return val | 0xFFFFF00000000000ULL;
    return val & 0x00000FFFFFFFFFFFULL;
}

static inline void check_mac_overflow(Gte* gte, int index, int64_t value) {
    int64_t min_val, max_val;
    if (index == 0) { min_val = -(1LL << 31); max_val = (1LL << 31) - 1; }
    else            { min_val = -(1LL << 43); max_val = (1LL << 43) - 1; }

    if (value < min_val) {
        /* negative overflow */
        if      (index == 0) set_flag(gte, 15);
        else if (index == 1) set_flag(gte, 27);
        else if (index == 2) set_flag(gte, 26);
        else if (index == 3) set_flag(gte, 25);
    } else if (value > max_val) {
        /* positive overflow */
        if      (index == 0) set_flag(gte, 16);
        else if (index == 1) set_flag(gte, 30);
        else if (index == 2) set_flag(gte, 29);
        else if (index == 3) set_flag(gte, 28);
    }
}

static inline void truncate_and_set_mac(Gte* gte, int index, int64_t value, int shift) {
    check_mac_overflow(gte, index, value);
    value >>= shift;
    gte->data[GTE_REG_MAC0 + index] = (int32_t)value;
}

/* IR1/IR2/IR3: sat flag bits 24/23/22; IR0: flag bit 12, clamp [0,0x1000] */
static inline void truncate_and_set_ir(Gte* gte, int index, int32_t value, bool lm) {
    if (index == 0) {
        if (value < 0)      { value = 0;      set_flag(gte, 12); }
        else if (value > 0x1000) { value = 0x1000; set_flag(gte, 12); }
        gte->data[GTE_REG_IR0] = (int16_t)value;
        return;
    }
    int32_t min_val = lm ? 0 : -32768;
    if (value < min_val) {
        value = min_val;
        set_flag(gte, 24 - (index - 1));
    } else if (value > 32767) {
        value = 32767;
        set_flag(gte, 24 - (index - 1));
    }
    gte->data[GTE_REG_IR0 + index] = (int16_t)value;
}

static inline int32_t gte_clamp_ir_nolm(Gte* gte, int idx, int32_t value) {
    if (value < -32768 || value > 32767)
        set_flag(gte, 24 - (idx - 1));
    return value < -32768 ? -32768 : (value > 32767 ? 32767 : value);
}

static inline uint32_t truncate_rgb(Gte* gte, int index, int32_t value) {
    int32_t mac_val = value >> 4;
    if (mac_val < 0 || mac_val > 0xFF) {
        /* COLOR R→bit21, G→bit20, B→bit19 (index 1,2,3 → 22-index) */
        set_flag(gte, 22 - index);
        return (mac_val < 0) ? 0 : 0xFF;
    }
    return (uint32_t)mac_val;
}

static inline void push_rgb_from_mac(Gte* gte) {
    uint32_t r = truncate_rgb(gte, 1, gte->data[GTE_REG_MAC1]);
    uint32_t g = truncate_rgb(gte, 2, gte->data[GTE_REG_MAC2]);
    uint32_t b = truncate_rgb(gte, 3, gte->data[GTE_REG_MAC3]);
    gte->data[GTE_REG_RGB0] = gte->data[GTE_REG_RGB1];
    gte->data[GTE_REG_RGB1] = gte->data[GTE_REG_RGB2];
    gte->data[GTE_REG_RGB2] = r | (g << 8) | (b << 16);
}

static inline void push_sz(Gte* gte, int32_t value) {
    if (value < 0 || value > 0xFFFF) {
        set_flag(gte, 18);
        value = (value < 0) ? 0 : 0xFFFF;
    }
    gte->data[GTE_REG_SZ0] = gte->data[GTE_REG_SZ1];
    gte->data[GTE_REG_SZ1] = gte->data[GTE_REG_SZ2];
    gte->data[GTE_REG_SZ2] = gte->data[GTE_REG_SZ3];
    gte->data[GTE_REG_SZ3] = value;
}

static inline void push_sxy(Gte* gte, int32_t x, int32_t y) {
    if (x < -1024 || x > 1023) { set_flag(gte, 14); set_flag(gte, 31); }
    if (y < -1024 || y > 1023) { set_flag(gte, 13); set_flag(gte, 31); }
    if (x < -1024) x = -1024; else if (x > 1023) x = 1023;
    if (y < -1024) y = -1024; else if (y > 1023) y = 1023;
    gte->data[GTE_REG_SXY0] = gte->data[GTE_REG_SXY1];
    gte->data[GTE_REG_SXY1] = gte->data[GTE_REG_SXY2];
    gte->data[GTE_REG_SXY2] = (x & 0xFFFF) | (y << 16);
}

static inline uint32_t count_leading_zeros(uint16_t val) {
    if (val == 0) return 16;
    return __builtin_clz(val) - 16;
}

static inline uint32_t gte_unr_divide(Gte* gte, uint32_t lhs, uint32_t rhs) {
    if (rhs * 2 <= lhs) {
        set_flag(gte, 17);
        set_flag(gte, 31);
        return 0x1FFFF;
    }
    uint32_t shift = (rhs == 0) ? 16 : count_leading_zeros((uint16_t)rhs);
    lhs <<= shift;
    rhs <<= shift;
    uint32_t divisor = rhs | 0x8000;
    int32_t x = 0x101 + unr_table[((divisor & 0x7FFF) + 0x40) >> 7];
    int32_t d = ((int32_t)divisor * -x + 0x80) >> 8;
    uint32_t recip = ((x * (0x20000 + d)) + 0x80) >> 8;
    uint32_t result = ((uint64_t)lhs * recip + 0x8000) >> 16;
    return (result > 0x1FFFF) ? 0x1FFFF : result;
}

// --- Pipeline Stage Helpers ---

static inline void gte_mul_mat_vec(Gte* gte, const int16_t* m, const int32_t* t,
                                   const int16_t* v, int shift, bool lm) {
    int64_t mac1 = sign_extend_44(((int64_t)t[0] << 12)
        + (int64_t)m[0]*v[0] + (int64_t)m[1]*v[1] + (int64_t)m[2]*v[2]);
    int64_t mac2 = sign_extend_44(((int64_t)t[1] << 12)
        + (int64_t)m[3]*v[0] + (int64_t)m[4]*v[1] + (int64_t)m[5]*v[2]);
    int64_t mac3 = sign_extend_44(((int64_t)t[2] << 12)
        + (int64_t)m[6]*v[0] + (int64_t)m[7]*v[1] + (int64_t)m[8]*v[2]);
    truncate_and_set_mac(gte, 1, mac1, shift);
    truncate_and_set_mac(gte, 2, mac2, shift);
    truncate_and_set_mac(gte, 3, mac3, shift);
    truncate_and_set_ir(gte, 1, gte->data[GTE_REG_MAC1], lm);
    truncate_and_set_ir(gte, 2, gte->data[GTE_REG_MAC2], lm);
    truncate_and_set_ir(gte, 3, gte->data[GTE_REG_MAC3], lm);
}

static inline void gte_mul_mat_vec_buggy(Gte* gte, const int16_t* m, const int32_t* t,
                                         const int16_t* v, int shift, bool lm) {
    int64_t mac1_temp = sign_extend_44(((int64_t)t[0] << 12) + (int64_t)m[0]*v[0]);
    truncate_and_set_ir(gte, 1, (int32_t)(mac1_temp >> shift), false);
    int64_t mac1 = sign_extend_44((int64_t)m[1]*v[1] + (int64_t)m[2]*v[2]);
    truncate_and_set_mac(gte, 1, mac1, shift);
    truncate_and_set_ir(gte, 1, gte->data[GTE_REG_MAC1], lm);

    int64_t mac2_temp = sign_extend_44(((int64_t)t[1] << 12) + (int64_t)m[3]*v[0]);
    truncate_and_set_ir(gte, 2, (int32_t)(mac2_temp >> shift), false);
    int64_t mac2 = sign_extend_44((int64_t)m[4]*v[1] + (int64_t)m[5]*v[2]);
    truncate_and_set_mac(gte, 2, mac2, shift);
    truncate_and_set_ir(gte, 2, gte->data[GTE_REG_MAC2], lm);

    int64_t mac3_temp = sign_extend_44(((int64_t)t[2] << 12) + (int64_t)m[6]*v[0]);
    truncate_and_set_ir(gte, 3, (int32_t)(mac3_temp >> shift), false);
    int64_t mac3 = sign_extend_44((int64_t)m[7]*v[1] + (int64_t)m[8]*v[2]);
    truncate_and_set_mac(gte, 3, mac3, shift);
    truncate_and_set_ir(gte, 3, gte->data[GTE_REG_MAC3], lm);
}

// lightTransform: L * V → MAC/IR
static inline void gte_light_step(Gte* gte, const int16_t* v, int shift, bool lm) {
    int16_t l11 = (int16_t)gte->control[GTE_CTL_L11L12];
    int16_t l12 = (int16_t)(gte->control[GTE_CTL_L11L12] >> 16);
    int16_t l13 = (int16_t)gte->control[GTE_CTL_L13L21];
    int16_t l21 = (int16_t)(gte->control[GTE_CTL_L13L21] >> 16);
    int16_t l22 = (int16_t)gte->control[GTE_CTL_L22L23];
    int16_t l23 = (int16_t)(gte->control[GTE_CTL_L22L23] >> 16);
    int16_t l31 = (int16_t)gte->control[GTE_CTL_L31L32];
    int16_t l32 = (int16_t)(gte->control[GTE_CTL_L31L32] >> 16);
    int16_t l33 = (int16_t)gte->control[GTE_CTL_L33];
    int64_t mac1 = sign_extend_44((int64_t)l11*v[0] + (int64_t)l12*v[1] + (int64_t)l13*v[2]);
    int64_t mac2 = sign_extend_44((int64_t)l21*v[0] + (int64_t)l22*v[1] + (int64_t)l23*v[2]);
    int64_t mac3 = sign_extend_44((int64_t)l31*v[0] + (int64_t)l32*v[1] + (int64_t)l33*v[2]);
    truncate_and_set_mac(gte, 1, mac1, shift);
    truncate_and_set_mac(gte, 2, mac2, shift);
    truncate_and_set_mac(gte, 3, mac3, shift);
    truncate_and_set_ir(gte, 1, gte->data[GTE_REG_MAC1], lm);
    truncate_and_set_ir(gte, 2, gte->data[GTE_REG_MAC2], lm);
    truncate_and_set_ir(gte, 3, gte->data[GTE_REG_MAC3], lm);
}

// colorMatrix: BK*4096 + LC * IR → MAC/IR
static inline void gte_color_matrix_step(Gte* gte, int shift, bool lm) {
    int16_t lcr1 = (int16_t)gte->control[GTE_CTL_LR1LR2];
    int16_t lcr2 = (int16_t)(gte->control[GTE_CTL_LR1LR2] >> 16);
    int16_t lcr3 = (int16_t)gte->control[GTE_CTL_LR3LG1];
    int16_t lcg1 = (int16_t)(gte->control[GTE_CTL_LR3LG1] >> 16);
    int16_t lcg2 = (int16_t)gte->control[GTE_CTL_LG2LG3];
    int16_t lcg3 = (int16_t)(gte->control[GTE_CTL_LG2LG3] >> 16);
    int16_t lcb1 = (int16_t)gte->control[GTE_CTL_LB1LB2];
    int16_t lcb2 = (int16_t)(gte->control[GTE_CTL_LB1LB2] >> 16);
    int16_t lcb3 = (int16_t)gte->control[GTE_CTL_LB3];
    int16_t ir1  = (int16_t)gte->data[GTE_REG_IR1];
    int16_t ir2  = (int16_t)gte->data[GTE_REG_IR2];
    int16_t ir3  = (int16_t)gte->data[GTE_REG_IR3];
    int64_t mac1 = sign_extend_44(((int64_t)(int32_t)gte->control[GTE_CTL_RBK] << 12)
                   + (int64_t)lcr1*ir1 + (int64_t)lcr2*ir2 + (int64_t)lcr3*ir3);
    int64_t mac2 = sign_extend_44(((int64_t)(int32_t)gte->control[GTE_CTL_GBK] << 12)
                   + (int64_t)lcg1*ir1 + (int64_t)lcg2*ir2 + (int64_t)lcg3*ir3);
    int64_t mac3 = sign_extend_44(((int64_t)(int32_t)gte->control[GTE_CTL_BBK] << 12)
                   + (int64_t)lcb1*ir1 + (int64_t)lcb2*ir2 + (int64_t)lcb3*ir3);
    truncate_and_set_mac(gte, 1, mac1, shift);
    truncate_and_set_mac(gte, 2, mac2, shift);
    truncate_and_set_mac(gte, 3, mac3, shift);
    truncate_and_set_ir(gte, 1, gte->data[GTE_REG_MAC1], lm);
    truncate_and_set_ir(gte, 2, gte->data[GTE_REG_MAC2], lm);
    truncate_and_set_ir(gte, 3, gte->data[GTE_REG_MAC3], lm);
}

// colorApply: RGBC * IR << 4 → MAC/IR
static inline void gte_color_apply_step(Gte* gte, int shift, bool lm) {
    uint8_t r   = (uint8_t)gte->data[GTE_REG_RGBC];
    uint8_t g   = (uint8_t)(gte->data[GTE_REG_RGBC] >> 8);
    uint8_t b   = (uint8_t)(gte->data[GTE_REG_RGBC] >> 16);
    int16_t ir1 = (int16_t)gte->data[GTE_REG_IR1];
    int16_t ir2 = (int16_t)gte->data[GTE_REG_IR2];
    int16_t ir3 = (int16_t)gte->data[GTE_REG_IR3];
    int64_t mac1 = sign_extend_44(((int64_t)r * ir1) << 4);
    int64_t mac2 = sign_extend_44(((int64_t)g * ir2) << 4);
    int64_t mac3 = sign_extend_44(((int64_t)b * ir3) << 4);
    truncate_and_set_mac(gte, 1, mac1, shift);
    truncate_and_set_mac(gte, 2, mac2, shift);
    truncate_and_set_mac(gte, 3, mac3, shift);
    truncate_and_set_ir(gte, 1, gte->data[GTE_REG_MAC1], lm);
    truncate_and_set_ir(gte, 2, gte->data[GTE_REG_MAC2], lm);
    truncate_and_set_ir(gte, 3, gte->data[GTE_REG_MAC3], lm);
}

// depthCue: interpolate (in_r, in_g, in_b) toward FC using IR0
static inline void gte_depth_cue_step(Gte* gte, int64_t in_r, int64_t in_g, int64_t in_b,
                                      int shift, bool lm) {
    int16_t ir0 = (int16_t)gte->data[GTE_REG_IR0];
    int64_t rfc = (int32_t)gte->control[GTE_CTL_RFC];
    int64_t gfc = (int32_t)gte->control[GTE_CTL_GFC];
    int64_t bfc = (int32_t)gte->control[GTE_CTL_BFC];

    int64_t tmp_r = sign_extend_44((rfc << 12) - in_r);
    check_mac_overflow(gte, 1, tmp_r);
    int32_t int_r = gte_clamp_ir_nolm(gte, 1, (int32_t)(tmp_r >> shift));

    int64_t tmp_g = sign_extend_44((gfc << 12) - in_g);
    check_mac_overflow(gte, 2, tmp_g);
    int32_t int_g = gte_clamp_ir_nolm(gte, 2, (int32_t)(tmp_g >> shift));

    int64_t tmp_b = sign_extend_44((bfc << 12) - in_b);
    check_mac_overflow(gte, 3, tmp_b);
    int32_t int_b = gte_clamp_ir_nolm(gte, 3, (int32_t)(tmp_b >> shift));

    int64_t mac1 = sign_extend_44(in_r + (int64_t)ir0 * int_r);
    int64_t mac2 = sign_extend_44(in_g + (int64_t)ir0 * int_g);
    int64_t mac3 = sign_extend_44(in_b + (int64_t)ir0 * int_b);
    truncate_and_set_mac(gte, 1, mac1, shift);
    truncate_and_set_mac(gte, 2, mac2, shift);
    truncate_and_set_mac(gte, 3, mac3, shift);
    truncate_and_set_ir(gte, 1, gte->data[GTE_REG_MAC1], lm);
    truncate_and_set_ir(gte, 2, gte->data[GTE_REG_MAC2], lm);
    truncate_and_set_ir(gte, 3, gte->data[GTE_REG_MAC3], lm);
}

// depthCueColor: RGBC*IR<<4 depth-cued toward FC
static inline void gte_depth_cue_color_step(Gte* gte, int shift, bool lm) {
    uint8_t r   = (uint8_t)gte->data[GTE_REG_RGBC];
    uint8_t g   = (uint8_t)(gte->data[GTE_REG_RGBC] >> 8);
    uint8_t b   = (uint8_t)(gte->data[GTE_REG_RGBC] >> 16);
    int16_t ir1 = (int16_t)gte->data[GTE_REG_IR1];
    int16_t ir2 = (int16_t)gte->data[GTE_REG_IR2];
    int16_t ir3 = (int16_t)gte->data[GTE_REG_IR3];
    gte_depth_cue_step(gte,
                       ((int64_t)r * ir1) << 4,
                       ((int64_t)g * ir2) << 4,
                       ((int64_t)b * ir3) << 4,
                       shift, lm);
}

/* Recompute GTE_ERROR (bit 31) from all error bits after each operation */
static inline void gte_update_error_flag(Gte* gte) {
    uint32_t f = (uint32_t)gte->control[GTE_CTL_FLAG];
    if (f & 0x7f87e000u) f |= (1u << 31);
    gte->control[GTE_CTL_FLAG] = (int32_t)f;
}

#endif // GTE_INTERNAL_H
