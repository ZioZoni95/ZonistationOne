/**
 * gpu_internal.h
 * Temporary header exposing old GPU functions during Phase 1-2 transition
 * This will be removed once all functions are migrated to modular structure
 */
#ifndef GPU_INTERNAL_H
#define GPU_INTERNAL_H

#include "gpu.h"

// Helper functions from old gpu.c (to be migrated in Phase 2)
void clear_gp0_command_buffer(Gpu* gpu);
void push_gp0_command_word(Gpu* gpu, uint32_t word);

// Command processing (still in old gpu.c for Phase 1)
void gpu_gp0_handle_word(Gpu* gpu, uint32_t command);
void gpu_gp1_handle_command(Gpu* gpu, uint32_t command);

#endif // GPU_INTERNAL_H
