/**
 * gte.c
 * GTE init, register access, and instruction dispatch.
 */
#include "gte.h"
#include "gte_internal.h"
#include <string.h>
#include "log.h"

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
    gte->busy = false;
    gte->cycles_remaining = 0;
    LOG_GTE_INFO("[GTE] GTE initialized");
}

int32_t gte_read_data_register(Gte* gte, uint32_t reg) {
    if (reg >= 32) { LOG_GTE_ERROR("[GTE] Invalid data reg read: %u", reg); return 0; }
    return gte->data[reg];
}

void gte_write_data_register(Gte* gte, uint32_t reg, int32_t value) {
    if (reg >= 32) { LOG_GTE_ERROR("[GTE] Invalid data reg write: %u = 0x%08x", reg, value); return; }
    gte->data[reg] = value;
}

int32_t gte_read_control_register(Gte* gte, uint32_t reg) {
    if (reg >= 32) { LOG_GTE_ERROR("[GTE] Invalid ctrl reg read: %u", reg); return 0; }
    return gte->control[reg];
}

void gte_write_control_register(Gte* gte, uint32_t reg, int32_t value) {
    if (reg >= 32) { LOG_GTE_ERROR("[GTE] Invalid ctrl reg write: %u = 0x%08x", reg, value); return; }
    gte->control[reg] = value;
}

uint32_t gte_execute_instruction(Gte* gte, uint32_t instruction) {
    uint32_t opcode = instruction & 0x3F;
    uint32_t cycles = 1;

    switch (opcode) {
        case 0x01: gte_rtps(gte, instruction);  cycles = 15; break;
        case 0x06: gte_nclip(gte);              cycles =  8; break;
        case 0x0C: gte_op(gte, instruction);    cycles =  6; break;
        case 0x10: gte_dpcs(gte, instruction);  cycles =  8; break;
        case 0x11: gte_intpl(gte, instruction); cycles =  5; break;
        case 0x12: gte_mvmva(gte, instruction); cycles =  8; break;
        case 0x13: gte_ncds(gte, instruction);  cycles = 19; break;
        case 0x14: gte_cdp(gte, instruction);   cycles = 13; break;
        case 0x16: gte_ncdt(gte, instruction);  cycles = 44; break;
        case 0x1B: gte_nccs(gte, instruction);  cycles = 17; break;
        case 0x1C: gte_cc(gte, instruction);    cycles = 11; break;
        case 0x1E: gte_ncs(gte, instruction);   cycles = 14; break;
        case 0x3F: gte_ncct(gte, instruction);  cycles = 39; break;
        case 0x20: gte_nct(gte, instruction);   cycles = 30; break;
        case 0x28: gte_sqr(gte, instruction);   cycles =  7; break;
        case 0x29: gte_dcpl(gte, instruction);  cycles =  5; break;
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

    gte->busy = true;
    gte->cycles_remaining = cycles;
    return cycles;
}
