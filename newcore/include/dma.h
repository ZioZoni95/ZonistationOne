#ifndef NEWCORE_DMA_H
#define NEWCORE_DMA_H
#include <stdint.h>
#define NC_DMA_CHANNELS 7

typedef struct {
    uint32_t madr;    // Base Address Register (MADR)
    uint32_t bcr;     // Block Control Register (BCR)
    uint32_t control; // Channel Control Register (CHCR)
    // Removed 'active' field (not used in register emulation)
} NcDmaChannel;

typedef struct {
    NcDmaChannel channels[NC_DMA_CHANNELS];
    uint32_t control;
    uint32_t interrupt;
} NcDma;

void nc_dma_init(NcDma* dma);
void nc_dma_step(NcDma* dma);

#endif // NEWCORE_DMA_H 