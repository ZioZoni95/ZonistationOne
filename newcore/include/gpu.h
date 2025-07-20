#ifndef NEWCORE_GPU_H
#define NEWCORE_GPU_H

#include <stdint.h>
#include <stdbool.h>
#include "vram.h"

// GPU state structure for newcore
typedef struct {
    NcVram vram;
    // TODO: Add more GPU state fields as needed
} NcGpu;

// Initialize GPU
void nc_gpu_init(NcGpu* gpu);

#endif // NEWCORE_GPU_H 