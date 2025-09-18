/*
 * ZonistationOne - PlayStation One Emulator
 * GPU Interface
 */

#ifndef PSX_GPU_H
#define PSX_GPU_H

#include "system.h"
#include "memory.h"

#ifdef __cplusplus
extern "C" {
#endif

/* GPU interface functions */
psx_gpu_t *gpu_create(void);
void gpu_destroy(psx_gpu_t *gpu);

int gpu_init(psx_gpu_t *gpu, psx_memory_t *memory);
void gpu_shutdown(psx_gpu_t *gpu);
void gpu_reset(psx_gpu_t *gpu);

/* Execution */
int gpu_step(psx_gpu_t *gpu, uint32_t cycles);

/* Register access */
uint32_t gpu_read_status(psx_gpu_t *gpu);
uint32_t gpu_read_data(psx_gpu_t *gpu);
void gpu_write_command(psx_gpu_t *gpu, uint32_t value);
void gpu_write_data(psx_gpu_t *gpu, uint32_t value);

#ifdef __cplusplus
}
#endif

#endif /* PSX_GPU_H */