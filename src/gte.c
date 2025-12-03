/**
 * gte.c
 * Implementation of the PlayStation GTE (Geometry Transformation Engine) emulation.
 * Based on PSX-Spex documentation.
 */
#include "gte.h"
#include <stdio.h>
#include <string.h>
#include "log.h"

// --- GTE Register Macros ---
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

// --- GTE Constants ---
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

// --- Helper Functions ---

// --- Forward Declarations ---
static int64_t sign_extend_44(int64_t val);
static void truncate_and_set_mac(Gte* gte, int index, int64_t value, int shift);
static void truncate_and_set_ir(Gte* gte, int index, int32_t value, bool lm);

static void gte_mul_mat_vec(Gte* gte, const int16_t* m, const int32_t* t, const int16_t* v, int shift, bool lm) {
    int64_t mac1 = ((int64_t)t[0] << 12) + ((int64_t)m[0] * v[0]) + ((int64_t)m[1] * v[1]) + ((int64_t)m[2] * v[2]);
    int64_t mac2 = ((int64_t)t[1] << 12) + ((int64_t)m[3] * v[0]) + ((int64_t)m[4] * v[1]) + ((int64_t)m[5] * v[2]);
    int64_t mac3 = ((int64_t)t[2] << 12) + ((int64_t)m[6] * v[0]) + ((int64_t)m[7] * v[1]) + ((int64_t)m[8] * v[2]);

    mac1 = sign_extend_44(mac1);
    mac2 = sign_extend_44(mac2);
    mac3 = sign_extend_44(mac3);

    truncate_and_set_mac(gte, 1, mac1, shift);
    truncate_and_set_mac(gte, 2, mac2, shift);
    truncate_and_set_mac(gte, 3, mac3, shift);

    truncate_and_set_ir(gte, 1, gte->data[GTE_REG_MAC1], lm);
    truncate_and_set_ir(gte, 2, gte->data[GTE_REG_MAC2], lm);
    truncate_and_set_ir(gte, 3, gte->data[GTE_REG_MAC3], lm);
}

static void gte_mul_mat_vec_buggy(Gte* gte, const int16_t* m, const int32_t* t, const int16_t* v, int shift, bool lm) {
    // Row 0
    int64_t mac1_temp = ((int64_t)t[0] << 12) + ((int64_t)m[0] * v[0]);
    mac1_temp = sign_extend_44(mac1_temp);
    truncate_and_set_ir(gte, 1, (int32_t)(mac1_temp >> shift), false);
    int64_t mac1 = ((int64_t)m[1] * v[1]) + ((int64_t)m[2] * v[2]);
    mac1 = sign_extend_44(mac1);
    truncate_and_set_mac(gte, 1, mac1, shift);
    truncate_and_set_ir(gte, 1, gte->data[GTE_REG_MAC1], lm);

    // Row 1
    int64_t mac2_temp = ((int64_t)t[1] << 12) + ((int64_t)m[3] * v[0]);
    mac2_temp = sign_extend_44(mac2_temp);
    truncate_and_set_ir(gte, 2, (int32_t)(mac2_temp >> shift), false);
    int64_t mac2 = ((int64_t)m[4] * v[1]) + ((int64_t)m[5] * v[2]);
    mac2 = sign_extend_44(mac2);
    truncate_and_set_mac(gte, 2, mac2, shift);
    truncate_and_set_ir(gte, 2, gte->data[GTE_REG_MAC2], lm);

    // Row 2
    int64_t mac3_temp = ((int64_t)t[2] << 12) + ((int64_t)m[6] * v[0]);
    mac3_temp = sign_extend_44(mac3_temp);
    truncate_and_set_ir(gte, 3, (int32_t)(mac3_temp >> shift), false);
    int64_t mac3 = ((int64_t)m[7] * v[1]) + ((int64_t)m[8] * v[2]);
    mac3 = sign_extend_44(mac3);
    truncate_and_set_mac(gte, 3, mac3, shift);
    truncate_and_set_ir(gte, 3, gte->data[GTE_REG_MAC3], lm);
}

static int64_t sign_extend_44(int64_t val) {
    // Sign extend from 44 bits to 64 bits
    if (val & (1ULL << 43)) {
        return val | 0xFFFFF00000000000ULL;
    }
    return val & 0x00000FFFFFFFFFFFULL;
}

static inline void set_flag(Gte* gte, int bit) {
    gte->control[GTE_CTL_FLAG] |= (1 << bit);
}

static void check_mac_overflow(Gte* gte, int index, int64_t value) {
    int64_t min_val, max_val;
    if (index == 0) {
        min_val = -(1LL << 31);
        max_val = (1LL << 31) - 1;
    } else {
        min_val = -(1LL << 43);
        max_val = (1LL << 43) - 1;
    }

    if (value < min_val) {
        if (index == 0) set_flag(gte, 15);
        else if (index == 1) set_flag(gte, 30);
        else if (index == 2) set_flag(gte, 29);
        else if (index == 3) set_flag(gte, 28);
    } else if (value > max_val) {
        if (index == 0) set_flag(gte, 16);
        else if (index == 1) set_flag(gte, 27);
        else if (index == 2) set_flag(gte, 26);
        else if (index == 3) set_flag(gte, 25);
    }
}

static void truncate_and_set_mac(Gte* gte, int index, int64_t value, int shift) {
    check_mac_overflow(gte, index, value);
    value >>= shift;
    gte->data[GTE_REG_MAC0 + index] = (int32_t)value;
}

static void truncate_and_set_ir(Gte* gte, int index, int32_t value, bool lm) {
    int32_t min_val = lm ? 0 : -32768;
    int32_t max_val = 32767;
    
    if (value < min_val) {
        value = min_val;
        set_flag(gte, 24 - (index - 1)); // IR1->24, IR2->23, IR3->22
    } else if (value > max_val) {
        value = max_val;
        set_flag(gte, 24 - (index - 1));
    }
    gte->data[GTE_REG_IR0 + index] = (int16_t)value; // Store as 16-bit (sign extended on read)
}

static uint32_t count_leading_zeros(uint16_t val) {
    if (val == 0) return 16;
    return __builtin_clz(val) - 16; // __builtin_clz works on unsigned int (32-bit usually)
}

static uint32_t gte_unr_divide(Gte* gte, uint32_t lhs, uint32_t rhs) {
    if (rhs * 2 <= lhs) {
        set_flag(gte, 17); // Divide overflow
        set_flag(gte, 31); // Error
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
    
    if (result > 0x1FFFF) return 0x1FFFF;
    return result;
}

static void push_sz(Gte* gte, int32_t value) {
    if (value < 0) {
        set_flag(gte, 18);
        value = 0;
    } else if (value > 0xFFFF) {
        set_flag(gte, 18);
        value = 0xFFFF;
    }
    
    gte->data[GTE_REG_SZ0] = gte->data[GTE_REG_SZ1];
    gte->data[GTE_REG_SZ1] = gte->data[GTE_REG_SZ2];
    gte->data[GTE_REG_SZ2] = gte->data[GTE_REG_SZ3];
    gte->data[GTE_REG_SZ3] = value;
}

static void push_sxy(Gte* gte, int32_t x, int32_t y) {
    if (x < -1024) {
        set_flag(gte, 31); // Error (SX2 saturated)
        x = -1024;
    } else if (x > 1023) {
        set_flag(gte, 31);
        x = 1023;
    }
    
    if (y < -1024) {
        set_flag(gte, 31);
        y = -1024;
    } else if (y > 1023) {
        set_flag(gte, 31);
        y = 1023;
    }
    
    gte->data[GTE_REG_SXY0] = gte->data[GTE_REG_SXY1];
    gte->data[GTE_REG_SXY1] = gte->data[GTE_REG_SXY2];
    gte->data[GTE_REG_SXY2] = (x & 0xFFFF) | (y << 16);
}

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
    
    int64_t vx = v[0];
    int64_t vy = v[1];
    int64_t vz = v[2];
    
    int64_t mac1 = (tr_x << 12) + (rt11 * vx) + (rt12 * vy) + (rt13 * vz);
    int64_t mac2 = (tr_y << 12) + (rt21 * vx) + (rt22 * vy) + (rt23 * vz);
    int64_t mac3 = (tr_z << 12) + (rt31 * vx) + (rt32 * vy) + (rt33 * vz);
    
    mac1 = sign_extend_44(mac1);
    mac2 = sign_extend_44(mac2);
    mac3 = sign_extend_44(mac3);
    
    truncate_and_set_mac(gte, 1, mac1, shift);
    truncate_and_set_mac(gte, 2, mac2, shift);
    truncate_and_set_mac(gte, 3, mac3, shift);
    
    truncate_and_set_ir(gte, 1, gte->data[GTE_REG_MAC1], lm);
    truncate_and_set_ir(gte, 2, gte->data[GTE_REG_MAC2], lm);
    truncate_and_set_ir(gte, 3, (int32_t)(mac3 >> 12), false);
    
    int32_t ir3_val = gte->data[GTE_REG_MAC3];
    int32_t min_val = lm ? 0 : -32768;
    int32_t max_val = 32767;
    if (ir3_val < min_val) ir3_val = min_val;
    else if (ir3_val > max_val) ir3_val = max_val;
    gte->data[GTE_REG_IR3] = (int16_t)ir3_val;
    
    push_sz(gte, (int32_t)(mac3 >> 12));
    
    uint16_t h = (uint16_t)gte->control[GTE_CTL_H];
    uint16_t sz3 = (uint16_t)gte->data[GTE_REG_SZ3];
    
    uint32_t result = gte_unr_divide(gte, h, sz3);
    
    int32_t ir1 = (int16_t)gte->data[GTE_REG_IR1];
    int32_t ir2 = (int16_t)gte->data[GTE_REG_IR2];
    int32_t ofx = gte->control[GTE_CTL_OFX];
    int32_t ofy = gte->control[GTE_CTL_OFY];
    
    int64_t sx = ((int64_t)result * ir1) + ofx;
    int64_t sy = ((int64_t)result * ir2) + ofy;
    
    check_mac_overflow(gte, 0, sx);
    check_mac_overflow(gte, 0, sy);
    
    push_sxy(gte, (int32_t)(sx >> 16), (int32_t)(sy >> 16));
    
    if (last) {
        int32_t dqa = (int16_t)gte->control[GTE_CTL_DQA];
        int32_t dqb = gte->control[GTE_CTL_DQB];
        
        int64_t sz = ((int64_t)result * dqa) + dqb;
        truncate_and_set_mac(gte, 0, sz, 0);
        truncate_and_set_ir(gte, 0, (int32_t)(sz >> 12), true);
    }
}


// --- Forward Declarations ---
void gte_rtps(Gte* gte, uint32_t instruction);
void gte_rtpt(Gte* gte, uint32_t instruction);
void gte_nclip(Gte* gte);
void gte_mvmva(Gte* gte, uint32_t instruction);
void gte_sqr(Gte* gte, uint32_t instruction);
void gte_op(Gte* gte, uint32_t instruction);
void gte_dcpl(Gte* gte);
void gte_intpl(Gte* gte);
void gte_avsz3(Gte* gte);
void gte_avsz4(Gte* gte);

// --- GTE Initialization ---

void gte_init(Gte* gte) {
    LOG_GTE_INFO("GTE initialized");
    
    LOG_GTE_DEBUG("Initializing GTE...\n");
    
    // Clear all data registers
    memset(gte->data, 0, sizeof(gte->data));
    
    // Clear all control registers
    memset(gte->control, 0, sizeof(gte->control));
    
    // Set initial state
    gte->busy = false;
    gte->cycles_remaining = 0;
    
    LOG_GTE_DEBUG("GTE Initialized\n");
}

// --- GTE Register Access ---

int32_t gte_read_data_register(Gte* gte, uint32_t reg) {
    if (reg >= 32) {
        LOG_GTE_ERROR("GTE: Invalid data register read: %u\n", reg);
        return 0;
    }
    return gte->data[reg];
}

void gte_write_data_register(Gte* gte, uint32_t reg, int32_t value) {
    if (reg >= 32) {
        LOG_GTE_ERROR("GTE: Invalid data register write: %u = 0x%08x\n", reg, value);
        return;
    }
    gte->data[reg] = value;
}

int32_t gte_read_control_register(Gte* gte, uint32_t reg) {
    if (reg >= 32) {
        LOG_GTE_ERROR("GTE: Invalid control register read: %u\n", reg);
        return 0;
    }
    return gte->control[reg];
}

void gte_write_control_register(Gte* gte, uint32_t reg, int32_t value) {
    if (reg >= 32) {
        LOG_GTE_ERROR("GTE: Invalid control register write: %u = 0x%08x\n", reg, value);
        return;
    }
    gte->control[reg] = value;
}

// --- GTE Instruction Execution ---

uint32_t gte_execute_instruction(Gte* gte, uint32_t instruction) {
    // Per-instruction logging is TRACE level (too spammy for DEBUG)
    LOG_GTE_TRACE("[GTE] Executing instruction: 0x%08x", instruction);
    uint32_t opcode = (instruction >> 20) & 0x3F; // Bits 25-20
    uint32_t cycles = 1; // Default cycle count
    
    switch (opcode) {
        case 0x01: // RTPS - Perspective Transformation (Single Point)
            gte_rtps(gte, instruction);
            cycles = 15;
            break;
            
        case 0x06: // NCLIP - Normal Clipping
            gte_nclip(gte);
            cycles = 8;
            break;
            
        case 0x0C: // OP - Outer Product
            gte_op(gte, instruction);
            cycles = 6;
            break;

        case 0x12: // MVMVA - Matrix-Vector Multiplication
            gte_mvmva(gte, instruction);
            cycles = 8;
            break;
            
        case 0x28: // SQR - Square Root
            gte_sqr(gte, instruction);
            cycles = 7;
            break;
            
        case 0x29: // DCPL - Depth Cueing
            gte_dcpl(gte);
            cycles = 5;
            break;
            
        case 0x2A: // INTPL - Interpolation
            gte_intpl(gte);
            cycles = 5;
            break;
            
        case 0x2D: // AVSZ3 - Average Z (3 points)
            gte_avsz3(gte);
            cycles = 5;
            break;
            
        case 0x2E: // AVSZ4 - Average Z (4 points)
            gte_avsz4(gte);
            cycles = 6;
            break;
            
        case 0x30: // RTPT - Perspective Transformation (Triangle)
            gte_rtpt(gte, instruction);
            cycles = 23;
            break;
            
        case 0x3D: // GPF - General Purpose Interpolation
            // gte_gpf(gte, instruction);
            cycles = 5;
            break;
            
        default: {
            static int unhandled_gte_count = 0;
            if (unhandled_gte_count < 10) {
                LOG_GTE_WARN("GTE: Unhandled instruction 0x%08x (opcode 0x%02x)\n", instruction, opcode);
                unhandled_gte_count++;
            }
            break;
        }
    }
    
    return cycles;
}

// --- GTE Operation Implementations (Stubs) ---

void gte_rtps(Gte* gte, uint32_t instruction) {
    int shift = (instruction >> 19) & 1; // sf bit
    bool lm = (instruction >> 10) & 1;   // lm bit
    
    LOG_GTE_DEBUG("GTE: RTPS (sf=%d, lm=%d)\n", shift, lm);

    // V0
    int16_t v[3];
    v[0] = (int16_t)gte->data[GTE_REG_V0_XY];
    v[1] = (int16_t)(gte->data[GTE_REG_V0_XY] >> 16);
    v[2] = (int16_t)gte->data[GTE_REG_V0_Z];
    
    gte->control[GTE_CTL_FLAG] = 0;
    
    gte_rtps_core(gte, v, shift ? 12 : 0, lm, true);
}

void gte_rtpt(Gte* gte, uint32_t instruction) {
    int shift = (instruction >> 19) & 1; // sf bit
    bool lm = (instruction >> 10) & 1;   // lm bit
    
    LOG_GTE_DEBUG("GTE: RTPT (sf=%d, lm=%d)\n", shift, lm);

    gte->control[GTE_CTL_FLAG] = 0;
    
    int16_t v0[3], v1[3], v2[3];
    
    v0[0] = (int16_t)gte->data[GTE_REG_V0_XY];
    v0[1] = (int16_t)(gte->data[GTE_REG_V0_XY] >> 16);
    v0[2] = (int16_t)gte->data[GTE_REG_V0_Z];
    
    v1[0] = (int16_t)gte->data[GTE_REG_V1_XY];
    v1[1] = (int16_t)(gte->data[GTE_REG_V1_XY] >> 16);
    v1[2] = (int16_t)gte->data[GTE_REG_V1_Z];
    
    v2[0] = (int16_t)gte->data[GTE_REG_V2_XY];
    v2[1] = (int16_t)(gte->data[GTE_REG_V2_XY] >> 16);
    v2[2] = (int16_t)gte->data[GTE_REG_V2_Z];
    
    gte_rtps_core(gte, v0, shift ? 12 : 0, lm, false);
    gte_rtps_core(gte, v1, shift ? 12 : 0, lm, false);
    gte_rtps_core(gte, v2, shift ? 12 : 0, lm, true);
}

void gte_nclip(Gte* gte) {
    int16_t sx0 = (int16_t)gte->data[GTE_REG_SXY0];
    int16_t sy0 = (int16_t)(gte->data[GTE_REG_SXY0] >> 16);
    int16_t sx1 = (int16_t)gte->data[GTE_REG_SXY1];
    int16_t sy1 = (int16_t)(gte->data[GTE_REG_SXY1] >> 16);
    int16_t sx2 = (int16_t)gte->data[GTE_REG_SXY2];
    int16_t sy2 = (int16_t)(gte->data[GTE_REG_SXY2] >> 16);
    
    LOG_GTE_DEBUG("GTE: NCLIP SXY0(%d,%d) SXY1(%d,%d) SXY2(%d,%d)\n", sx0, sy0, sx1, sy1, sx2, sy2);
    
    int64_t mac0 = (int64_t)sx0 * sy1 + (int64_t)sx1 * sy2 + (int64_t)sx2 * sy0 -
                   (int64_t)sx0 * sy2 - (int64_t)sx1 * sy0 - (int64_t)sx2 * sy1;
                   
    gte->control[GTE_CTL_FLAG] = 0;
    truncate_and_set_mac(gte, 0, mac0, 0);
    
    LOG_GTE_DEBUG("GTE: NCLIP Result MAC0=%ld\n", mac0);
}

void gte_mvmva(Gte* gte, uint32_t instruction) {
    int shift = (instruction >> 19) & 1;
    bool lm = (instruction >> 10) & 1;
    int mat_idx = (instruction >> 17) & 3;
    int vec_idx = (instruction >> 15) & 3;
    int tr_idx = (instruction >> 13) & 3;

    LOG_GTE_DEBUG("GTE: MVMVA (sf=%d, lm=%d, mat=%d, vec=%d, tr=%d)\n", shift, lm, mat_idx, vec_idx, tr_idx);

    int16_t m[9];
    int32_t t[3];
    int16_t v[3];

    // Matrix Selection
    if (mat_idx == 3) {
        // Buggy Matrix
        uint32_t rgbc_val = gte->data[GTE_REG_RGBC];
        int16_t r = (int16_t)((rgbc_val) & 0xFF);
        
        m[0] = -(r << 4);
        m[1] = (r << 4);
        m[2] = (int16_t)gte->data[GTE_REG_IR0];
        
        int16_t rt13 = (int16_t)gte->control[GTE_CTL_RT13RT21];
        m[3] = rt13;
        m[4] = rt13;
        m[5] = rt13;
        
        int16_t rt21 = (int16_t)(gte->control[GTE_CTL_RT13RT21] >> 16);
        m[6] = rt21;
        m[7] = rt21;
        m[8] = rt21;
    } else {
        int32_t* mat_base;
        if (mat_idx == 0) mat_base = &gte->control[GTE_CTL_RT11RT12];
        else if (mat_idx == 1) mat_base = &gte->control[GTE_CTL_L11L12];
        else mat_base = &gte->control[GTE_CTL_LR1LR2];
        
        m[0] = (int16_t)mat_base[0];
        m[1] = (int16_t)(mat_base[0] >> 16);
        m[2] = (int16_t)mat_base[1];
        m[3] = (int16_t)(mat_base[1] >> 16);
        m[4] = (int16_t)mat_base[2];
        m[5] = (int16_t)(mat_base[2] >> 16);
        m[6] = (int16_t)mat_base[3];
        m[7] = (int16_t)(mat_base[3] >> 16);
        m[8] = (int16_t)mat_base[4];
    }

    // Vector Selection
    if (vec_idx == 0) { // V0
        v[0] = (int16_t)gte->data[GTE_REG_V0_XY];
        v[1] = (int16_t)(gte->data[GTE_REG_V0_XY] >> 16);
        v[2] = (int16_t)gte->data[GTE_REG_V0_Z];
    } else if (vec_idx == 1) { // V1
        v[0] = (int16_t)gte->data[GTE_REG_V1_XY];
        v[1] = (int16_t)(gte->data[GTE_REG_V1_XY] >> 16);
        v[2] = (int16_t)gte->data[GTE_REG_V1_Z];
    } else if (vec_idx == 2) { // V2
        v[0] = (int16_t)gte->data[GTE_REG_V2_XY];
        v[1] = (int16_t)(gte->data[GTE_REG_V2_XY] >> 16);
        v[2] = (int16_t)gte->data[GTE_REG_V2_Z];
    } else { // IR
        v[0] = (int16_t)gte->data[GTE_REG_IR1];
        v[1] = (int16_t)gte->data[GTE_REG_IR2];
        v[2] = (int16_t)gte->data[GTE_REG_IR3];
    }

    // Translation Vector Selection
    if (tr_idx == 0) { // TR
        t[0] = gte->control[GTE_CTL_TRX];
        t[1] = gte->control[GTE_CTL_TRY];
        t[2] = gte->control[GTE_CTL_TRZ];
    } else if (tr_idx == 1) { // BK
        t[0] = gte->control[GTE_CTL_RBK];
        t[1] = gte->control[GTE_CTL_GBK];
        t[2] = gte->control[GTE_CTL_BBK];
    } else if (tr_idx == 2) { // FC
        t[0] = gte->control[GTE_CTL_RFC];
        t[1] = gte->control[GTE_CTL_GFC];
        t[2] = gte->control[GTE_CTL_BFC];
    } else { // None
        t[0] = 0;
        t[1] = 0;
        t[2] = 0;
    }

    if (tr_idx == 2) {
        gte_mul_mat_vec_buggy(gte, m, t, v, shift ? 12 : 0, lm);
    } else {
        gte_mul_mat_vec(gte, m, t, v, shift ? 12 : 0, lm);
    }
}

void gte_sqr(Gte* gte, uint32_t instruction) {
    int shift = (instruction >> 19) & 1;
    bool lm = (instruction >> 10) & 1;
    LOG_GTE_DEBUG("GTE: SQR (sf=%d, lm=%d)\n", shift, lm);
    
    int32_t ir1 = (int16_t)gte->data[GTE_REG_IR1];
    int32_t ir2 = (int16_t)gte->data[GTE_REG_IR2];
    int32_t ir3 = (int16_t)gte->data[GTE_REG_IR3];
    
    int64_t mac1 = ((int64_t)ir1 * ir1) >> (shift ? 12 : 0);
    int64_t mac2 = ((int64_t)ir2 * ir2) >> (shift ? 12 : 0);
    int64_t mac3 = ((int64_t)ir3 * ir3) >> (shift ? 12 : 0);
    
    gte->data[GTE_REG_MAC1] = (int32_t)mac1;
    gte->data[GTE_REG_MAC2] = (int32_t)mac2;
    gte->data[GTE_REG_MAC3] = (int32_t)mac3;
    
    truncate_and_set_ir(gte, 1, (int32_t)mac1, lm);
    truncate_and_set_ir(gte, 2, (int32_t)mac2, lm);
    truncate_and_set_ir(gte, 3, (int32_t)mac3, lm);
}

void gte_op(Gte* gte, uint32_t instruction) {
    int shift = (instruction >> 19) & 1;
    bool lm = (instruction >> 10) & 1;
    
    LOG_GTE_DEBUG("GTE: OP (sf=%d, lm=%d)\n", shift, lm);
    
    int16_t ir1 = (int16_t)gte->data[GTE_REG_IR1];
    int16_t ir2 = (int16_t)gte->data[GTE_REG_IR2];
    int16_t ir3 = (int16_t)gte->data[GTE_REG_IR3];
    
    int16_t d1 = (int16_t)gte->control[GTE_CTL_RT11RT12]; // RT11
    int16_t d2 = (int16_t)gte->control[GTE_CTL_RT22RT23]; // RT22
    int16_t d3 = (int16_t)gte->control[GTE_CTL_RT33];     // RT33
    
    gte->control[GTE_CTL_FLAG] = 0;
    
    int64_t mac1 = (int64_t)ir3 * d2 - (int64_t)ir2 * d3;
    int64_t mac2 = (int64_t)ir1 * d3 - (int64_t)ir3 * d1;
    int64_t mac3 = (int64_t)ir2 * d1 - (int64_t)ir1 * d2;
    
    truncate_and_set_mac(gte, 1, mac1, shift ? 12 : 0);
    truncate_and_set_mac(gte, 2, mac2, shift ? 12 : 0);
    truncate_and_set_mac(gte, 3, mac3, shift ? 12 : 0);
    
    truncate_and_set_ir(gte, 1, gte->data[GTE_REG_MAC1], lm);
    truncate_and_set_ir(gte, 2, gte->data[GTE_REG_MAC2], lm);
    truncate_and_set_ir(gte, 3, gte->data[GTE_REG_MAC3], lm);
}

void gte_dcpl(Gte* gte) {
    (void)gte;
    LOG_GTE_DEBUG("GTE: DCPL (Depth Cueing) - TODO: Implement\n");
    // TODO: Implement depth cueing
}

void gte_intpl(Gte* gte) {
    (void)gte;
    LOG_GTE_DEBUG("GTE: INTPL (Interpolation) - TODO: Implement\n");
    // TODO: Implement interpolation
}

void gte_avsz3(Gte* gte) {
    LOG_GTE_DEBUG("GTE: AVSZ3\n");
    int32_t zsf3 = (int16_t)gte->control[GTE_CTL_ZSF3];
    uint32_t sz1 = (uint16_t)gte->data[GTE_REG_SZ1];
    uint32_t sz2 = (uint16_t)gte->data[GTE_REG_SZ2];
    uint32_t sz3 = (uint16_t)gte->data[GTE_REG_SZ3];
    
    int64_t result = (int64_t)zsf3 * (sz1 + sz2 + sz3);
    
    truncate_and_set_mac(gte, 0, result, 0);
    
    int32_t otz = (int32_t)(result >> 12);
    if (otz < 0) otz = 0;
    if (otz > 65535) otz = 65535;
    gte->data[GTE_REG_OTZ] = (uint16_t)otz;
}

void gte_avsz4(Gte* gte) {
    LOG_GTE_DEBUG("GTE: AVSZ4\n");
    int32_t zsf4 = (int16_t)gte->control[GTE_CTL_ZSF4];
    uint32_t sz0 = (uint16_t)gte->data[GTE_REG_SZ0];
    uint32_t sz1 = (uint16_t)gte->data[GTE_REG_SZ1];
    uint32_t sz2 = (uint16_t)gte->data[GTE_REG_SZ2];
    uint32_t sz3 = (uint16_t)gte->data[GTE_REG_SZ3];
    
    int64_t result = (int64_t)zsf4 * (sz0 + sz1 + sz2 + sz3);
    
    truncate_and_set_mac(gte, 0, result, 0);
    
    int32_t otz = (int32_t)(result >> 12);
    if (otz < 0) otz = 0;
    if (otz > 65535) otz = 65535;
    gte->data[GTE_REG_OTZ] = (uint16_t)otz;
} 