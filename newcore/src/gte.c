#include "../include/gte.h"
#include "../include/log.h"
#include <string.h>

// Initialize GTE state
void nc_gte_init(NcGte* gte) {
    NC_LOGI("GTE initialization started");
    memset(gte->data, 0, sizeof(gte->data));
    memset(gte->control, 0, sizeof(gte->control));
    gte->busy = false;
    gte->cycles_remaining = 0;
    NC_LOGI("GTE initialized (registers cleared)");
}

// GTE instruction dispatcher (ported from old code)
uint32_t nc_gte_execute_instruction(NcGte* gte, uint32_t instruction) {
    NC_LOGI("[GTE] Executing instruction: 0x%08x", instruction);
    uint32_t opcode = (instruction >> 20) & 0x3F;
    uint32_t cycles = 1;
    switch (opcode) {
        case 0x01: /* RTPS */ NC_LOGI("[GTE] RTPS stub"); cycles = 8; break;
        case 0x06: /* NCLIP */ NC_LOGI("[GTE] NCLIP stub"); cycles = 8; break;
        case 0x12: /* MVMVA */ NC_LOGI("[GTE] MVMVA stub"); cycles = 8; break;
        case 0x28: /* SQR */ NC_LOGI("[GTE] SQR stub"); cycles = 7; break;
        case 0x29: /* DCPL */ NC_LOGI("[GTE] DCPL stub"); cycles = 5; break;
        case 0x2A: /* INTPL */ NC_LOGI("[GTE] INTPL stub"); cycles = 5; break;
        case 0x2D: /* AVSZ3 */ NC_LOGI("[GTE] AVSZ3 stub"); cycles = 5; break;
        case 0x2E: /* AVSZ4 */ NC_LOGI("[GTE] AVSZ4 stub"); cycles = 6; break;
        case 0x30: /* RTPT */ NC_LOGI("[GTE] RTPT stub"); cycles = 17; break;
        case 0x3D: /* OP */ NC_LOGI("[GTE] OP stub"); cycles = 4; break;
        default:
            NC_LOGW("[GTE] Unhandled instruction 0x%08x (opcode 0x%02x)", instruction, opcode);
            break;
    }
    return cycles;
} 