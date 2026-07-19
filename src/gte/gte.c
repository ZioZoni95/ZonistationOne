/**
 * gte.c
 * GTE init, register access, and instruction dispatch.
 */
#include "gte.h"
#include "gte_internal.h"
#include <string.h>
#include "log.h"
#include "lua_debug.h"

static const char* gte_opcode_name(uint8_t op) {
    switch (op) {
        case 0x01: return "RTPS";  case 0x06: return "NCLIP"; case 0x0C: return "OP";
        case 0x10: return "DPCS";  case 0x11: return "INTPL"; case 0x12: return "MVMVA";
        case 0x13: return "NCDS";  case 0x14: return "CDP";   case 0x16: return "NCDT";
        case 0x1B: return "NCCS";  case 0x1C: return "CC";    case 0x1E: return "NCS";
        case 0x20: return "NCT";   case 0x28: return "SQR";   case 0x29: return "DCPL";
        case 0x2A: return "DPCT";  case 0x2D: return "AVSZ3"; case 0x2E: return "AVSZ4";
        case 0x30: return "RTPT";  case 0x3D: return "GPF";   case 0x3E: return "GPL";
        case 0x3F: return "NCCT";
        default:   return "UNKNOWN";
    }
}

void gte_init(Gte* gte) {
    memset(gte->data, 0, sizeof(gte->data));
    memset(gte->control, 0, sizeof(gte->control));
    LOG_GTE_INFO("[GTE] GTE initialized");
}

int32_t gte_read_data_register(Gte* gte, uint32_t reg) {
    if (reg >= 32) { LOG_GTE_ERROR("[GTE] Invalid data reg read: %u", reg); return 0; }
    switch (reg) {
        case 1: case 3: case 5: case 8: case 9: case 10: case 11:
            return (int32_t)(int16_t)gte->data[reg];
        case 7: case 16: case 17: case 18: case 19:
            return (int32_t)(uint32_t)(uint16_t)gte->data[reg];
        case 15:
            return gte->data[GTE_REG_SXY2];
        case 28: case 29: {
            int32_t ir1 = (int16_t)gte->data[GTE_REG_IR1];
            int32_t ir2 = (int16_t)gte->data[GTE_REG_IR2];
            int32_t ir3 = (int16_t)gte->data[GTE_REG_IR3];
            uint32_t r = (uint32_t)((ir1 >> 7) < 0 ? 0 : (ir1 >> 7) > 31 ? 31 : (ir1 >> 7));
            uint32_t g = (uint32_t)((ir2 >> 7) < 0 ? 0 : (ir2 >> 7) > 31 ? 31 : (ir2 >> 7));
            uint32_t b = (uint32_t)((ir3 >> 7) < 0 ? 0 : (ir3 >> 7) > 31 ? 31 : (ir3 >> 7));
            return (int32_t)(r | (g << 5) | (b << 10));
        }
        default:
            return gte->data[reg];
    }
}

static inline uint32_t count_leading_bits(uint32_t val) {
    if (val == 0) return 32;
    int32_t s = (int32_t)val;
    return (s >= 0) ? (uint32_t)__builtin_clz((uint32_t)s)
                    : (uint32_t)__builtin_clz((uint32_t)~s);
}

void gte_write_data_register(Gte* gte, uint32_t reg, int32_t value) {
    if (reg >= 32) { LOG_GTE_ERROR("[GTE] Invalid data reg write: %u = 0x%08x", reg, value); return; }
    switch (reg) {
        case 15:
            gte->data[GTE_REG_SXY0] = gte->data[GTE_REG_SXY1];
            gte->data[GTE_REG_SXY1] = gte->data[GTE_REG_SXY2];
            gte->data[GTE_REG_SXY2] = value;
            break;
        case 28: {
            uint32_t u = (uint32_t)value;
            gte->data[GTE_REG_IR1] = (int32_t)(int16_t)((u & 0x1Fu) << 7);
            gte->data[GTE_REG_IR2] = (int32_t)(int16_t)((u & 0x3E0u) << 2);
            gte->data[GTE_REG_IR3] = (int32_t)(int16_t)((u & 0x7C00u) >> 3);
            gte->data[reg] = value;
            break;
        }
        case 30:
            gte->data[reg] = value;
            gte->data[GTE_REG_LZCR] = (int32_t)count_leading_bits((uint32_t)value);
            break;
        case 31:
            break;
        default:
            gte->data[reg] = value;
            break;
    }
}

int32_t gte_read_control_register(Gte* gte, uint32_t reg) {
    if (reg >= 32) { LOG_GTE_ERROR("[GTE] Invalid ctrl reg read: %u", reg); return 0; }
    return gte->control[reg];
}

void gte_write_control_register(Gte* gte, uint32_t reg, int32_t value) {
    if (reg >= 32) { LOG_GTE_ERROR("[GTE] Invalid ctrl reg write: %u = 0x%08x", reg, value); return; }
    switch (reg) {
        case 4: case 12: case 20: case 26: case 27: case 29: case 30:
            gte->control[reg] = (int32_t)(int16_t)value;
            break;
        case 31: {
            uint32_t f = (uint32_t)value & 0x7ffff000u;
            if (f & 0x7f87e000u) f |= (1u << 31);
            gte->control[reg] = (int32_t)f;
            break;
        }
        default:
            gte->control[reg] = value;
            break;
    }
}

uint32_t gte_execute_instruction(Gte* gte, uint32_t instruction) {
    uint32_t opcode = instruction & 0x3F;
    uint32_t cycles = 1;

    switch (opcode) {
        case 0x01: gte_rtps(gte, instruction);  cycles = 15; break;
        case 0x06: gte_nclip(gte);              cycles =  8; break;
        case 0x0C: gte_op(gte, instruction);    cycles =  6; break;
        case 0x10: gte_dpcs(gte, instruction);  cycles =  8; break;
        case 0x11: gte_intpl(gte, instruction); cycles =  8; break;
        case 0x12: gte_mvmva(gte, instruction); cycles =  8; break;
        case 0x13: gte_ncds(gte, instruction);  cycles = 19; break;
        case 0x14: gte_cdp(gte, instruction);   cycles = 13; break;
        case 0x16: gte_ncdt(gte, instruction);  cycles = 44; break;
        case 0x1B: gte_nccs(gte, instruction);  cycles = 17; break;
        case 0x1C: gte_cc(gte, instruction);    cycles = 11; break;
        case 0x1E: gte_ncs(gte, instruction);   cycles = 14; break;
        case 0x3F: gte_ncct(gte, instruction);  cycles = 39; break;
        case 0x20: gte_nct(gte, instruction);   cycles = 30; break;
        case 0x28: gte_sqr(gte, instruction);   cycles =  5; break;
        case 0x29: gte_dcpl(gte, instruction);  cycles =  8; break;
        case 0x2A: gte_dpct(gte, instruction);  cycles = 17; break;
        case 0x2D: gte_avsz3(gte);              cycles =  5; break;
        case 0x2E: gte_avsz4(gte);              cycles =  6; break;
        case 0x30: gte_rtpt(gte, instruction);  cycles = 23; break;
        case 0x3D: gte_gpf(gte, instruction);   cycles =  5; break;
        case 0x3E: gte_gpl(gte, instruction);   cycles =  5; break;
        default: {
            static uint32_t seen = 0;
            uint32_t mask = (opcode < 32) ? (1u << opcode) : 0;
            if (mask == 0 || !(seen & mask)) {
                LOG_GTE_WARN("[GTE] Unhandled opcode %s (0x%02x), instr=0x%08x",
                             gte_opcode_name((uint8_t)opcode), opcode, instruction);
                if (mask) seen |= mask;
            }
            break;
        }
    }

    gte_update_error_flag(gte);
    lua_debug_notify(gte_opcode_name((uint8_t)opcode));
    return cycles;
}
