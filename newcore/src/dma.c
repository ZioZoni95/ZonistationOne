#include "../include/dma.h"
#include "../include/log.h"
#include <string.h>

void nc_dma_init(NcDma* dma) {
    memset(dma, 0, sizeof(NcDma));
    dma->control = 0x07654321; // Default value used by PCSX ReARMed
    NC_LOGI("DMA initialized");
}

void nc_dma_step(NcDma* dma) {
    // TODO: Implement DMA channel stepping and event scheduling
    NC_LOGI("DMA step (stub)");
} 