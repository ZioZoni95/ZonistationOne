#include "../include/gpu.h"
#include "../include/log.h"
#include <string.h>

// Initialize GPU state
void nc_gpu_init(NcGpu* gpu) {
    NC_LOGI("GPU initialization started");
    memset(&gpu->vram, 0, sizeof(NcVram));
    NC_LOGI("GPU initialized (VRAM cleared)");
} 