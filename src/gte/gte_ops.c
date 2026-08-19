/* SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 ZioZoni95
 *
 * Part of ZoniStation One, a PlayStation 1 emulator.
 * See LICENSE for the full licence text and THIRD-PARTY.md for the
 * components of this project that have other authors.
 */
/**
 * gte_ops.c
 * GTE opcode implementations.
 */
#include "gte_internal.h"
#include "log.h"

// --- RTPS/RTPT core (perspective transform for one vertex) ---
static void gte_rtps_core(Gte* gte, int16_t* v, int shift, bool lm, bool last) {
    int64_t tr_x = gte->control[GTE_CTL_TRX];
    int64_t tr_y = gte->control[GTE_CTL_TRY];
    int64_t tr_z = gte->control[GTE_CTL_TRZ];

    int16_t rt11 = (int16_t)gte->control[GTE_CTL_RT11RT12];
    int16_t rt12 = (int16_t)(gte->control[GTE_CTL_RT11RT12] >> 16);
    int16_t rt13 = (int16_t)gte->control[GTE_CTL_RT13RT21];
    int16_t rt21 = (int16_t)(gte->control[GTE_CTL_RT13RT21] >> 16);
    int16_t rt22 = (int16_t)gte->control[GTE_CTL_RT22RT23];
    int16_t rt23 = (int16_t)(gte->control[GTE_CTL_RT22RT23] >> 16);
    int16_t rt31 = (int16_t)gte->control[GTE_CTL_RT31RT32];
    int16_t rt32 = (int16_t)(gte->control[GTE_CTL_RT31RT32] >> 16);
    int16_t rt33 = (int16_t)gte->control[GTE_CTL_RT33];

    int64_t vx = v[0], vy = v[1], vz = v[2];

    int64_t mac1 = sign_extend_44((tr_x << 12) + rt11*vx + rt12*vy + rt13*vz);
    int64_t mac2 = sign_extend_44((tr_y << 12) + rt21*vx + rt22*vy + rt23*vz);
    int64_t mac3 = sign_extend_44((tr_z << 12) + rt31*vx + rt32*vy + rt33*vz);

    truncate_and_set_mac(gte, 1, mac1, shift);
    truncate_and_set_mac(gte, 2, mac2, shift);
    truncate_and_set_mac(gte, 3, mac3, shift);

    truncate_and_set_ir(gte, 1, gte->data[GTE_REG_MAC1], lm);
    truncate_and_set_ir(gte, 2, gte->data[GTE_REG_MAC2], lm);

    /* IR3 saturation check uses raw mac3>>12 always; stored value is MAC3 clamped */
    {
        int32_t raw12 = (int32_t)(mac3 >> 12);
        int32_t min_val = lm ? 0 : -32768;
        if (raw12 < min_val || raw12 > 32767)
            set_flag(gte, 22); /* bit 22 = IR3 saturation */
        int32_t ir3_val = gte->data[GTE_REG_MAC3];
        if      (ir3_val < min_val) ir3_val = min_val;
        else if (ir3_val > 32767)   ir3_val = 32767;
        gte->data[GTE_REG_IR3] = (int16_t)ir3_val;
    }

    push_sz(gte, (int32_t)(mac3 >> 12));

    uint16_t h   = (uint16_t)gte->control[GTE_CTL_H];
    uint16_t sz3 = (uint16_t)gte->data[GTE_REG_SZ3];
    uint32_t result = gte_unr_divide(gte, h, sz3);

    int32_t ir1 = (int16_t)gte->data[GTE_REG_IR1];
    int32_t ir2 = (int16_t)gte->data[GTE_REG_IR2];
    int64_t sx  = (int64_t)result * ir1 + gte->control[GTE_CTL_OFX];
    int64_t sy  = (int64_t)result * ir2 + gte->control[GTE_CTL_OFY];
    check_mac_overflow(gte, 0, sx);
    check_mac_overflow(gte, 0, sy);
    push_sxy(gte, (int32_t)(sx >> 16), (int32_t)(sy >> 16));

    if (last) {
        int32_t dqa = (int16_t)gte->control[GTE_CTL_DQA];
        int64_t sz2 = (int64_t)result * dqa + gte->control[GTE_CTL_DQB];
        truncate_and_set_mac(gte, 0, sz2, 0);
        truncate_and_set_ir(gte, 0, (int32_t)(sz2 >> 12), true);
    }
}

// NCS/NCT pipeline: L*V → BK+LC*IR → pushColor
static inline void gte_color_calc_step(Gte* gte, int16_t* v, int shift, bool lm) {
    gte_light_step(gte, v, shift, lm);
    gte_color_matrix_step(gte, shift, lm);
    push_rgb_from_mac(gte);
}

// NCCS/NCCT pipeline: L*V → BK+LC*IR → RGBC*IR<<4 → pushColor
static inline void gte_nccs_core(Gte* gte, const int16_t* v, int shift, bool lm) {
    gte_light_step(gte, v, shift, lm);
    gte_color_matrix_step(gte, shift, lm);
    gte_color_apply_step(gte, shift, lm);
    push_rgb_from_mac(gte);
}

// --- RTPS (0x01): Perspective Transformation — Single Point ---
void gte_rtps(Gte* gte, uint32_t instruction) {
    int shift  = (instruction >> 19) & 1 ? 12 : 0;
    bool lm    = (instruction >> 10) & 1;
    LOG_GTE_TRACE("[GTE] RTPS (sf=%d, lm=%d)", shift, lm);
    gte->control[GTE_CTL_FLAG] = 0;
    int16_t v[3] = {
        (int16_t)gte->data[GTE_REG_V0_XY],
        (int16_t)(gte->data[GTE_REG_V0_XY] >> 16),
        (int16_t)gte->data[GTE_REG_V0_Z]
    };
    gte_rtps_core(gte, v, shift, lm, true);
}

// --- RTPT (0x30): Perspective Transformation — Triangle ---
void gte_rtpt(Gte* gte, uint32_t instruction) {
    int shift  = (instruction >> 19) & 1 ? 12 : 0;
    bool lm    = (instruction >> 10) & 1;
    LOG_GTE_TRACE("[GTE] RTPT (sf=%d, lm=%d)", shift, lm);
    gte->control[GTE_CTL_FLAG] = 0;
    int16_t v0[3] = { (int16_t)gte->data[GTE_REG_V0_XY], (int16_t)(gte->data[GTE_REG_V0_XY] >> 16), (int16_t)gte->data[GTE_REG_V0_Z] };
    int16_t v1[3] = { (int16_t)gte->data[GTE_REG_V1_XY], (int16_t)(gte->data[GTE_REG_V1_XY] >> 16), (int16_t)gte->data[GTE_REG_V1_Z] };
    int16_t v2[3] = { (int16_t)gte->data[GTE_REG_V2_XY], (int16_t)(gte->data[GTE_REG_V2_XY] >> 16), (int16_t)gte->data[GTE_REG_V2_Z] };
    gte_rtps_core(gte, v0, shift, lm, false);
    gte_rtps_core(gte, v1, shift, lm, false);
    gte_rtps_core(gte, v2, shift, lm, true);
}

// --- NCLIP (0x06): Normal Clipping ---
void gte_nclip(Gte* gte) {
    int16_t sx0 = (int16_t)gte->data[GTE_REG_SXY0], sy0 = (int16_t)(gte->data[GTE_REG_SXY0] >> 16);
    int16_t sx1 = (int16_t)gte->data[GTE_REG_SXY1], sy1 = (int16_t)(gte->data[GTE_REG_SXY1] >> 16);
    int16_t sx2 = (int16_t)gte->data[GTE_REG_SXY2], sy2 = (int16_t)(gte->data[GTE_REG_SXY2] >> 16);
    LOG_GTE_TRACE("[GTE] NCLIP SXY0(%d,%d) SXY1(%d,%d) SXY2(%d,%d)", sx0, sy0, sx1, sy1, sx2, sy2);
    gte->control[GTE_CTL_FLAG] = 0;
    int64_t mac0 = (int64_t)sx0*sy1 + (int64_t)sx1*sy2 + (int64_t)sx2*sy0
                 - (int64_t)sx0*sy2 - (int64_t)sx1*sy0 - (int64_t)sx2*sy1;
    truncate_and_set_mac(gte, 0, mac0, 0);
    LOG_GTE_TRACE("[GTE] NCLIP Result MAC0=%ld", mac0);
}

// --- MVMVA (0x12): Matrix-Vector Multiplication ---
void gte_mvmva(Gte* gte, uint32_t instruction) {
    int shift   = (instruction >> 19) & 1;
    bool lm     = (instruction >> 10) & 1;
    int mat_idx = (instruction >> 17) & 3;
    int vec_idx = (instruction >> 15) & 3;
    int tr_idx  = (instruction >> 13) & 3;
    LOG_GTE_TRACE("[GTE] MVMVA (sf=%d, lm=%d, mat=%d, vec=%d, tr=%d)", shift, lm, mat_idx, vec_idx, tr_idx);
    /* Every GTE command resets FLAG at its start
     * (psx-spx-docs geometrytransformationenginegte.md:302-303). MVMVA was the
     * only opcode here that did not, so it inherited the previous command's
     * saturation bits and any code polling FLAG after an MVMVA read stale ones. */
    gte->control[GTE_CTL_FLAG] = 0;

    int16_t m[9]; int32_t t[3]; int16_t v[3];

    if (mat_idx == 3) {
        /* Garbage matrix: -R*10h, +R*10h, IR0, RT13 x3, RT22 x3
         * (geometrytransformationenginegte.md:489-491). The last row used to be
         * RT21 — the high half of the same register RT13 lives in, which is easy
         * to reach for and is not what the hardware selects. */
        uint32_t rgbc_val = gte->data[GTE_REG_RGBC];
        int16_t r = (int16_t)((rgbc_val) & 0xFF);
        m[0] = -(r << 4); m[1] = (r << 4); m[2] = (int16_t)gte->data[GTE_REG_IR0];
        int16_t rt13 = (int16_t)gte->control[GTE_CTL_RT13RT21];
        m[3] = rt13; m[4] = rt13; m[5] = rt13;
        int16_t rt22 = (int16_t)gte->control[GTE_CTL_RT22RT23];
        m[6] = rt22; m[7] = rt22; m[8] = rt22;
    } else {
        int32_t* mb;
        if      (mat_idx == 0) mb = &gte->control[GTE_CTL_RT11RT12];
        else if (mat_idx == 1) mb = &gte->control[GTE_CTL_L11L12];
        else                   mb = &gte->control[GTE_CTL_LR1LR2];
        m[0]=(int16_t)mb[0]; m[1]=(int16_t)(mb[0]>>16); m[2]=(int16_t)mb[1];
        m[3]=(int16_t)(mb[1]>>16); m[4]=(int16_t)mb[2]; m[5]=(int16_t)(mb[2]>>16);
        m[6]=(int16_t)mb[3]; m[7]=(int16_t)(mb[3]>>16); m[8]=(int16_t)mb[4];
    }

    if      (vec_idx == 0) { v[0]=(int16_t)gte->data[GTE_REG_V0_XY]; v[1]=(int16_t)(gte->data[GTE_REG_V0_XY]>>16); v[2]=(int16_t)gte->data[GTE_REG_V0_Z]; }
    else if (vec_idx == 1) { v[0]=(int16_t)gte->data[GTE_REG_V1_XY]; v[1]=(int16_t)(gte->data[GTE_REG_V1_XY]>>16); v[2]=(int16_t)gte->data[GTE_REG_V1_Z]; }
    else if (vec_idx == 2) { v[0]=(int16_t)gte->data[GTE_REG_V2_XY]; v[1]=(int16_t)(gte->data[GTE_REG_V2_XY]>>16); v[2]=(int16_t)gte->data[GTE_REG_V2_Z]; }
    else                   { v[0]=(int16_t)gte->data[GTE_REG_IR1];  v[1]=(int16_t)gte->data[GTE_REG_IR2];  v[2]=(int16_t)gte->data[GTE_REG_IR3]; }

    if      (tr_idx == 0) { t[0]=gte->control[GTE_CTL_TRX]; t[1]=gte->control[GTE_CTL_TRY]; t[2]=gte->control[GTE_CTL_TRZ]; }
    else if (tr_idx == 1) { t[0]=gte->control[GTE_CTL_RBK]; t[1]=gte->control[GTE_CTL_GBK]; t[2]=gte->control[GTE_CTL_BBK]; }
    else if (tr_idx == 2) { t[0]=gte->control[GTE_CTL_RFC]; t[1]=gte->control[GTE_CTL_GFC]; t[2]=gte->control[GTE_CTL_BFC]; }
    else                  { t[0]=0; t[1]=0; t[2]=0; }

    if (tr_idx == 2) gte_mul_mat_vec_buggy(gte, m, t, v, shift ? 12 : 0, lm);
    else             gte_mul_mat_vec(gte, m, t, v, shift ? 12 : 0, lm);
}

// --- SQR (0x28): Square ---
void gte_sqr(Gte* gte, uint32_t instruction) {
    int shift = (instruction >> 19) & 1 ? 12 : 0;
    bool lm   = (instruction >> 10) & 1;
    LOG_GTE_TRACE("[GTE] SQR (sf=%d, lm=%d)", shift, lm);
    gte->control[GTE_CTL_FLAG] = 0;
    int32_t ir1 = (int16_t)gte->data[GTE_REG_IR1];
    int32_t ir2 = (int16_t)gte->data[GTE_REG_IR2];
    int32_t ir3 = (int16_t)gte->data[GTE_REG_IR3];
    truncate_and_set_mac(gte, 1, (int64_t)ir1*ir1, shift);
    truncate_and_set_mac(gte, 2, (int64_t)ir2*ir2, shift);
    truncate_and_set_mac(gte, 3, (int64_t)ir3*ir3, shift);
    truncate_and_set_ir(gte, 1, gte->data[GTE_REG_MAC1], lm);
    truncate_and_set_ir(gte, 2, gte->data[GTE_REG_MAC2], lm);
    truncate_and_set_ir(gte, 3, gte->data[GTE_REG_MAC3], lm);
}

// --- OP (0x0C): Outer Product ---
void gte_op(Gte* gte, uint32_t instruction) {
    int shift = (instruction >> 19) & 1;
    bool lm   = (instruction >> 10) & 1;
    LOG_GTE_TRACE("[GTE] OP (sf=%d, lm=%d)", shift, lm);
    gte->control[GTE_CTL_FLAG] = 0;
    int16_t ir1 = (int16_t)gte->data[GTE_REG_IR1];
    int16_t ir2 = (int16_t)gte->data[GTE_REG_IR2];
    int16_t ir3 = (int16_t)gte->data[GTE_REG_IR3];
    int16_t d1  = (int16_t)gte->control[GTE_CTL_RT11RT12];
    int16_t d2  = (int16_t)gte->control[GTE_CTL_RT22RT23];
    int16_t d3  = (int16_t)gte->control[GTE_CTL_RT33];
    int64_t mac1 = (int64_t)ir3*d2 - (int64_t)ir2*d3;
    int64_t mac2 = (int64_t)ir1*d3 - (int64_t)ir3*d1;
    int64_t mac3 = (int64_t)ir2*d1 - (int64_t)ir1*d2;
    truncate_and_set_mac(gte, 1, mac1, shift ? 12 : 0);
    truncate_and_set_mac(gte, 2, mac2, shift ? 12 : 0);
    truncate_and_set_mac(gte, 3, mac3, shift ? 12 : 0);
    truncate_and_set_ir(gte, 1, gte->data[GTE_REG_MAC1], lm);
    truncate_and_set_ir(gte, 2, gte->data[GTE_REG_MAC2], lm);
    truncate_and_set_ir(gte, 3, gte->data[GTE_REG_MAC3], lm);
}

// --- NCS (0x1E): Normal Color Single ---
void gte_ncs(Gte* gte, uint32_t instruction) {
    int shift = (instruction >> 19) & 1 ? 12 : 0;
    bool lm   = (instruction >> 10) & 1;
    LOG_GTE_TRACE("[GTE] NCS sf=%d lm=%d", shift, lm);
    gte->control[GTE_CTL_FLAG] = 0;
    int16_t v[3] = { (int16_t)gte->data[GTE_REG_V0_XY], (int16_t)(gte->data[GTE_REG_V0_XY] >> 16), (int16_t)gte->data[GTE_REG_V0_Z] };
    gte_color_calc_step(gte, v, shift, lm);
}

// --- NCT (0x20): Normal Color Triple ---
void gte_nct(Gte* gte, uint32_t instruction) {
    int shift = (instruction >> 19) & 1 ? 12 : 0;
    bool lm   = (instruction >> 10) & 1;
    LOG_GTE_TRACE("[GTE] NCT sf=%d lm=%d", shift, lm);
    gte->control[GTE_CTL_FLAG] = 0;
    static const int regs[3][2] = {{GTE_REG_V0_XY,GTE_REG_V0_Z},{GTE_REG_V1_XY,GTE_REG_V1_Z},{GTE_REG_V2_XY,GTE_REG_V2_Z}};
    for (int i = 0; i < 3; i++) {
        int16_t v[3] = { (int16_t)gte->data[regs[i][0]], (int16_t)(gte->data[regs[i][0]]>>16), (int16_t)gte->data[regs[i][1]] };
        gte_color_calc_step(gte, v, shift, lm);
    }
}

// --- NCCS (0x1B): Normal Color Color Single ---
void gte_nccs(Gte* gte, uint32_t instruction) {
    int shift = (instruction >> 19) & 1 ? 12 : 0;
    bool lm   = (instruction >> 10) & 1;
    LOG_GTE_TRACE("[GTE] NCCS sf=%d lm=%d", shift, lm);
    gte->control[GTE_CTL_FLAG] = 0;
    int16_t v[3] = { (int16_t)gte->data[GTE_REG_V0_XY], (int16_t)(gte->data[GTE_REG_V0_XY]>>16), (int16_t)gte->data[GTE_REG_V0_Z] };
    gte_nccs_core(gte, v, shift, lm);
}

// --- NCCT (0x1F): Normal Color Color Triple ---
void gte_ncct(Gte* gte, uint32_t instruction) {
    int shift = (instruction >> 19) & 1 ? 12 : 0;
    bool lm   = (instruction >> 10) & 1;
    LOG_GTE_TRACE("[GTE] NCCT sf=%d lm=%d", shift, lm);
    gte->control[GTE_CTL_FLAG] = 0;
    static const int regs[3][2] = {{GTE_REG_V0_XY,GTE_REG_V0_Z},{GTE_REG_V1_XY,GTE_REG_V1_Z},{GTE_REG_V2_XY,GTE_REG_V2_Z}};
    for (int i = 0; i < 3; i++) {
        int16_t v[3] = { (int16_t)gte->data[regs[i][0]], (int16_t)(gte->data[regs[i][0]]>>16), (int16_t)gte->data[regs[i][1]] };
        gte_nccs_core(gte, v, shift, lm);
    }
}

// --- CC (0x1C): Color Color — BK+LC*IR → RGBC*IR → pushColor ---
void gte_cc(Gte* gte, uint32_t instruction) {
    int shift = (instruction >> 19) & 1 ? 12 : 0;
    bool lm   = (instruction >> 10) & 1;
    LOG_GTE_TRACE("[GTE] CC sf=%d lm=%d", shift, lm);
    gte->control[GTE_CTL_FLAG] = 0;
    gte_color_matrix_step(gte, shift, lm);
    gte_color_apply_step(gte, shift, lm);
    push_rgb_from_mac(gte);
}

// --- CDP (0x14): Color Depth Cue — BK+LC*IR → depthCueColor → pushColor ---
void gte_cdp(Gte* gte, uint32_t instruction) {
    int shift = (instruction >> 19) & 1 ? 12 : 0;
    bool lm   = (instruction >> 10) & 1;
    LOG_GTE_TRACE("[GTE] CDP sf=%d lm=%d", shift, lm);
    gte->control[GTE_CTL_FLAG] = 0;
    gte_color_matrix_step(gte, shift, lm);
    gte_depth_cue_color_step(gte, shift, lm);
    push_rgb_from_mac(gte);
}

// --- NCDS (0x13): Normal Color Depth Cue Single ---
void gte_ncds(Gte* gte, uint32_t instruction) {
    int shift = (instruction >> 19) & 1 ? 12 : 0;
    bool lm   = (instruction >> 10) & 1;
    LOG_GTE_TRACE("[GTE] NCDS sf=%d lm=%d", shift, lm);
    gte->control[GTE_CTL_FLAG] = 0;
    int16_t v[3] = { (int16_t)gte->data[GTE_REG_V0_XY], (int16_t)(gte->data[GTE_REG_V0_XY]>>16), (int16_t)gte->data[GTE_REG_V0_Z] };
    gte_light_step(gte, v, shift, lm);
    gte_color_matrix_step(gte, shift, lm);
    gte_depth_cue_color_step(gte, shift, lm);
    push_rgb_from_mac(gte);
}

// --- NCDT (0x16): Normal Color Depth Cue Triple ---
void gte_ncdt(Gte* gte, uint32_t instruction) {
    int shift = (instruction >> 19) & 1 ? 12 : 0;
    bool lm   = (instruction >> 10) & 1;
    LOG_GTE_TRACE("[GTE] NCDT sf=%d lm=%d", shift, lm);
    gte->control[GTE_CTL_FLAG] = 0;
    static const int regs[3][2] = {{GTE_REG_V0_XY,GTE_REG_V0_Z},{GTE_REG_V1_XY,GTE_REG_V1_Z},{GTE_REG_V2_XY,GTE_REG_V2_Z}};
    for (int i = 0; i < 3; i++) {
        int16_t v[3] = { (int16_t)gte->data[regs[i][0]], (int16_t)(gte->data[regs[i][0]]>>16), (int16_t)gte->data[regs[i][1]] };
        gte_light_step(gte, v, shift, lm);
        gte_color_matrix_step(gte, shift, lm);
        gte_depth_cue_color_step(gte, shift, lm);
        push_rgb_from_mac(gte);
    }
}

// --- DPCS (0x10): Depth Cue Single — depthCue(R<<16, G<<16, B<<16) ---
void gte_dpcs(Gte* gte, uint32_t instruction) {
    int shift = (instruction >> 19) & 1 ? 12 : 0;
    bool lm   = (instruction >> 10) & 1;
    LOG_GTE_TRACE("[GTE] DPCS sf=%d lm=%d", shift, lm);
    gte->control[GTE_CTL_FLAG] = 0;
    uint8_t r = (uint8_t)gte->data[GTE_REG_RGBC];
    uint8_t g = (uint8_t)(gte->data[GTE_REG_RGBC] >> 8);
    uint8_t b = (uint8_t)(gte->data[GTE_REG_RGBC] >> 16);
    gte_depth_cue_step(gte, (int64_t)r<<16, (int64_t)g<<16, (int64_t)b<<16, shift, lm);
    push_rgb_from_mac(gte);
}

// --- DPCT (0x2A): Depth Cue Triple — DPCS × 3 using RGB0→RGB1→RGB2 FIFO ---
void gte_dpct(Gte* gte, uint32_t instruction) {
    int shift = (instruction >> 19) & 1 ? 12 : 0;
    bool lm   = (instruction >> 10) & 1;
    LOG_GTE_TRACE("[GTE] DPCT sf=%d lm=%d", shift, lm);
    gte->control[GTE_CTL_FLAG] = 0;
    for (int i = 0; i < 3; i++) {
        uint8_t r = (uint8_t)gte->data[GTE_REG_RGB0];
        uint8_t g = (uint8_t)(gte->data[GTE_REG_RGB0] >> 8);
        uint8_t b = (uint8_t)(gte->data[GTE_REG_RGB0] >> 16);
        gte_depth_cue_step(gte, (int64_t)r<<16, (int64_t)g<<16, (int64_t)b<<16, shift, lm);
        push_rgb_from_mac(gte);
    }
}

// --- DCPL (0x29): Depth Cue with Pattern — depthCueColor → pushColor ---
void gte_dcpl(Gte* gte, uint32_t instruction) {
    int shift = (instruction >> 19) & 1 ? 12 : 0;
    bool lm   = (instruction >> 10) & 1;
    LOG_GTE_TRACE("[GTE] DCPL sf=%d lm=%d", shift, lm);
    gte->control[GTE_CTL_FLAG] = 0;
    gte_depth_cue_color_step(gte, shift, lm);
    push_rgb_from_mac(gte);
}

// --- INTPL (0x11): Interpolation — depthCue(IR1<<12, IR2<<12, IR3<<12) ---
void gte_intpl(Gte* gte, uint32_t instruction) {
    int shift = (instruction >> 19) & 1 ? 12 : 0;
    bool lm   = (instruction >> 10) & 1;
    LOG_GTE_TRACE("[GTE] INTPL sf=%d lm=%d", shift, lm);
    gte->control[GTE_CTL_FLAG] = 0;
    int16_t ir1 = (int16_t)gte->data[GTE_REG_IR1];
    int16_t ir2 = (int16_t)gte->data[GTE_REG_IR2];
    int16_t ir3 = (int16_t)gte->data[GTE_REG_IR3];
    gte_depth_cue_step(gte, (int64_t)ir1<<12, (int64_t)ir2<<12, (int64_t)ir3<<12, shift, lm);
    push_rgb_from_mac(gte);
}

// --- GPF (0x3D): General Purpose Multiply — MAC = IR0*IR ---
void gte_gpf(Gte* gte, uint32_t instruction) {
    int shift = (instruction >> 19) & 1 ? 12 : 0;
    bool lm   = (instruction >> 10) & 1;
    LOG_GTE_TRACE("[GTE] GPF sf=%d lm=%d", shift, lm);
    gte->control[GTE_CTL_FLAG] = 0;
    int16_t ir0 = (int16_t)gte->data[GTE_REG_IR0];
    int16_t ir1 = (int16_t)gte->data[GTE_REG_IR1];
    int16_t ir2 = (int16_t)gte->data[GTE_REG_IR2];
    int16_t ir3 = (int16_t)gte->data[GTE_REG_IR3];
    int64_t mac1 = sign_extend_44((int64_t)ir0*ir1);
    int64_t mac2 = sign_extend_44((int64_t)ir0*ir2);
    int64_t mac3 = sign_extend_44((int64_t)ir0*ir3);
    truncate_and_set_mac(gte, 1, mac1, shift);
    truncate_and_set_mac(gte, 2, mac2, shift);
    truncate_and_set_mac(gte, 3, mac3, shift);
    truncate_and_set_ir(gte, 1, gte->data[GTE_REG_MAC1], lm);
    truncate_and_set_ir(gte, 2, gte->data[GTE_REG_MAC2], lm);
    truncate_and_set_ir(gte, 3, gte->data[GTE_REG_MAC3], lm);
    push_rgb_from_mac(gte);
}

// --- GPL (0x3E): General Purpose Multiply+Add — MAC = (sf?MAC<<12:MAC) + IR0*IR ---
void gte_gpl(Gte* gte, uint32_t instruction) {
    int shift = (instruction >> 19) & 1 ? 12 : 0;
    bool lm   = (instruction >> 10) & 1;
    LOG_GTE_TRACE("[GTE] GPL sf=%d lm=%d", shift, lm);
    gte->control[GTE_CTL_FLAG] = 0;
    int16_t ir0 = (int16_t)gte->data[GTE_REG_IR0];
    int16_t ir1 = (int16_t)gte->data[GTE_REG_IR1];
    int16_t ir2 = (int16_t)gte->data[GTE_REG_IR2];
    int16_t ir3 = (int16_t)gte->data[GTE_REG_IR3];
    int64_t prev1 = shift ? ((int64_t)(int32_t)gte->data[GTE_REG_MAC1] << 12) : (int64_t)(int32_t)gte->data[GTE_REG_MAC1];
    int64_t prev2 = shift ? ((int64_t)(int32_t)gte->data[GTE_REG_MAC2] << 12) : (int64_t)(int32_t)gte->data[GTE_REG_MAC2];
    int64_t prev3 = shift ? ((int64_t)(int32_t)gte->data[GTE_REG_MAC3] << 12) : (int64_t)(int32_t)gte->data[GTE_REG_MAC3];
    int64_t mac1 = sign_extend_44(prev1 + (int64_t)ir0*ir1);
    int64_t mac2 = sign_extend_44(prev2 + (int64_t)ir0*ir2);
    int64_t mac3 = sign_extend_44(prev3 + (int64_t)ir0*ir3);
    truncate_and_set_mac(gte, 1, mac1, shift);
    truncate_and_set_mac(gte, 2, mac2, shift);
    truncate_and_set_mac(gte, 3, mac3, shift);
    truncate_and_set_ir(gte, 1, gte->data[GTE_REG_MAC1], lm);
    truncate_and_set_ir(gte, 2, gte->data[GTE_REG_MAC2], lm);
    truncate_and_set_ir(gte, 3, gte->data[GTE_REG_MAC3], lm);
    push_rgb_from_mac(gte);
}

// --- AVSZ3 (0x2D): Average Z — 3 points ---
void gte_avsz3(Gte* gte) {
    LOG_GTE_TRACE("[GTE] AVSZ3");
    gte->control[GTE_CTL_FLAG] = 0;
    int32_t  zsf3   = (int16_t)gte->control[GTE_CTL_ZSF3];
    uint32_t sz1    = (uint16_t)gte->data[GTE_REG_SZ1];
    uint32_t sz2    = (uint16_t)gte->data[GTE_REG_SZ2];
    uint32_t sz3    = (uint16_t)gte->data[GTE_REG_SZ3];
    int64_t  result = (int64_t)zsf3 * (sz1 + sz2 + sz3);
    truncate_and_set_mac(gte, 0, result, 0);
    int32_t otz = (int32_t)(result >> 12);
    if (otz < 0 || otz > 65535) {
        set_flag(gte, 18);
        otz = (otz < 0) ? 0 : 65535;
    }
    gte->data[GTE_REG_OTZ] = (uint16_t)otz;
}

// --- AVSZ4 (0x2E): Average Z — 4 points ---
void gte_avsz4(Gte* gte) {
    LOG_GTE_TRACE("[GTE] AVSZ4");
    gte->control[GTE_CTL_FLAG] = 0;
    int32_t  zsf4   = (int16_t)gte->control[GTE_CTL_ZSF4];
    uint32_t sz0    = (uint16_t)gte->data[GTE_REG_SZ0];
    uint32_t sz1    = (uint16_t)gte->data[GTE_REG_SZ1];
    uint32_t sz2    = (uint16_t)gte->data[GTE_REG_SZ2];
    uint32_t sz3    = (uint16_t)gte->data[GTE_REG_SZ3];
    int64_t  result = (int64_t)zsf4 * (sz0 + sz1 + sz2 + sz3);
    truncate_and_set_mac(gte, 0, result, 0);
    int32_t otz = (int32_t)(result >> 12);
    if (otz < 0 || otz > 65535) {
        set_flag(gte, 18);
        otz = (otz < 0) ? 0 : 65535;
    }
    gte->data[GTE_REG_OTZ] = (uint16_t)otz;
}
