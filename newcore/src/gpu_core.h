// gpu_core.h
// Migrated from gpu.c: graphics processing logic (header)
// TODO: Move GPU declarations here.

#ifndef GPU_CORE_H
#define GPU_CORE_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// --- GPU Core State ---
typedef struct {
    // TODO: Add GPU state, VRAM, etc.
} GpuCore;

// --- GPU Core API ---
void gpu_core_init(GpuCore* gpu);
void gpu_core_process_command(GpuCore* gpu, uint32_t command);
void gpu_core_render(GpuCore* gpu);

#endif // GPU_CORE_H 