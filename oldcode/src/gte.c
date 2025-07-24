/**
 * gte.c
 * Implementation of the PlayStation GTE (Geometry Transformation Engine) emulation.
 * Based on PSX-Spex documentation.
 */
#include "gte.h"
#include <stdio.h>
#include <string.h>
#include "log.h"

// --- GTE Initialization ---

void gte_init(Gte* gte) {
    LOG_GTE_INFO("GTE initialized");
    
    LOG_DEBUG("Initializing GTE...\n");
    
    // Clear all data registers
    memset(gte->data, 0, sizeof(gte->data));
    
    // Clear all control registers
    memset(gte->control, 0, sizeof(gte->control));
    
    // Set initial state
    gte->busy = false;
    gte->cycles_remaining = 0;
    
    LOG_DEBUG("GTE Initialized\n");
}

// --- GTE Register Access ---

int32_t gte_read_data_register(Gte* gte, uint32_t reg) {
    if (reg >= 32) {
        LOG_ERROR("GTE: Invalid data register read: %u\n", reg);
        return 0;
    }
    return gte->data[reg];
}

void gte_write_data_register(Gte* gte, uint32_t reg, int32_t value) {
    if (reg >= 32) {
        LOG_ERROR("GTE: Invalid data register write: %u = 0x%08x\n", reg, value);
        return;
    }
    gte->data[reg] = value;
}

int32_t gte_read_control_register(Gte* gte, uint32_t reg) {
    if (reg >= 32) {
        LOG_ERROR("GTE: Invalid control register read: %u\n", reg);
        return 0;
    }
    return gte->control[reg];
}

void gte_write_control_register(Gte* gte, uint32_t reg, int32_t value) {
    if (reg >= 32) {
        LOG_ERROR("GTE: Invalid control register write: %u = 0x%08x\n", reg, value);
        return;
    }
    gte->control[reg] = value;
}

// --- GTE Instruction Execution ---

uint32_t gte_execute_instruction(Gte* gte, uint32_t instruction) {
    if (log_get_level() >= LOG_LEVEL_INFO) {
        LOG_GTE_INFO("[GTE] Executing instruction: 0x%08x", instruction);
    }
    uint32_t opcode = (instruction >> 20) & 0x3F; // Bits 25-20
    uint32_t cycles = 1; // Default cycle count
    
    switch (opcode) {
        case 0x01: // RTPS - Perspective Transformation (Single Point)
            gte_rtps(gte);
            cycles = 8;
            break;
            
        case 0x06: // NCLIP - Normal Clipping
            gte_nclip(gte);
            cycles = 8;
            break;
            
        case 0x12: // MVMVA - Matrix-Vector Multiplication
            gte_mvmva(gte, instruction);
            cycles = 8;
            break;
            
        case 0x28: // SQR - Square Root
            gte_sqr(gte);
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
            gte_rtpt(gte);
            cycles = 17;
            break;
            
        case 0x3D: // OP - Outer Product
            gte_op(gte);
            cycles = 4;
            break;
            
        default:
            LOG_WARN("GTE: Unhandled instruction 0x%08x (opcode 0x%02x)\n", instruction, opcode);
            break;
    }
    
    return cycles;
}

// --- GTE Operation Implementations (Stubs) ---

void gte_rtps(Gte* gte) {
    (void)gte;
    LOG_DEBUG("GTE: RTPS (Perspective Transformation Single Point) - TODO: Implement\n");
    // TODO: Implement perspective transformation for single point
    // This transforms a 3D point to 2D screen coordinates
}

void gte_rtpt(Gte* gte) {
    (void)gte;
    LOG_DEBUG("GTE: RTPT (Perspective Transformation Triangle) - TODO: Implement\n");
    // TODO: Implement perspective transformation for triangle
    // This transforms three 3D points to 2D screen coordinates
}

void gte_nclip(Gte* gte) {
    (void)gte;
    LOG_DEBUG("GTE: NCLIP (Normal Clipping) - TODO: Implement\n");
    // TODO: Implement normal clipping
    // This calculates the cross product of two vectors for clipping
}

void gte_mvmva(Gte* gte, uint32_t instruction) {
    (void)gte;
    (void)instruction;
    LOG_DEBUG("GTE: MVMVA (Matrix-Vector Multiplication) - TODO: Implement\n");
    // TODO: Implement matrix-vector multiplication
    // This performs various matrix operations based on instruction bits
}

void gte_sqr(Gte* gte) {
    (void)gte;
    LOG_DEBUG("GTE: SQR (Square Root) - TODO: Implement\n");
    // TODO: Implement square root calculation
}

void gte_op(Gte* gte) {
    (void)gte;
    LOG_DEBUG("GTE: OP (Outer Product) - TODO: Implement\n");
    // TODO: Implement outer product calculation
}

void gte_dcpl(Gte* gte) {
    (void)gte;
    LOG_DEBUG("GTE: DCPL (Depth Cueing) - TODO: Implement\n");
    // TODO: Implement depth cueing
}

void gte_intpl(Gte* gte) {
    (void)gte;
    LOG_DEBUG("GTE: INTPL (Interpolation) - TODO: Implement\n");
    // TODO: Implement interpolation
}

void gte_avsz3(Gte* gte) {
    (void)gte;
    LOG_DEBUG("GTE: AVSZ3 (Average Z 3 points) - TODO: Implement\n");
    // TODO: Implement average Z calculation for 3 points
}

void gte_avsz4(Gte* gte) {
    (void)gte;
    LOG_DEBUG("GTE: AVSZ4 (Average Z 4 points) - TODO: Implement\n");
    // TODO: Implement average Z calculation for 4 points
} 