#ifndef NEWCORE_DMA_H
#define NEWCORE_DMA_H
#include <stdint.h>
#define NC_DMA_CHANNELS 7

typedef struct {
    uint32_t base;
    uint32_t block_control;
    uint32_t control;
    uint32_t active;
} NcDmaChannel;

typedef struct {
    NcDmaChannel channels[NC_DMA_CHANNELS];
    uint32_t control;
    uint32_t interrupt;
} NcDma;

void nc_dma_init(NcDma* dma);
void nc_dma_step(NcDma* dma);

#endif // NEWCORE_DMA_H 