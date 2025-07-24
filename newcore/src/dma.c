#include "../include/dma.h"
#include "../include/log.h"
#include <string.h>

void nc_dma_init(NcDma* dma) {
    memset(dma, 0, sizeof(NcDma));
    dma->control = 0x07654321; // Default value used by PCSX ReARMed
    NC_LOGI("DMA initialized");
}

void nc_dma_step(NcDma* dma) {
    // Minimal simulation: for each channel, if enabled, simulate a transfer and clear enable
    for (int ch = 0; ch < 7; ++ch) {
        if (dma->channels[ch].control & 0x01000000) { // DMA enable bit
            // Simulate transfer: clear enable, set 'DMA complete' (for BIOS polling)
            dma->channels[ch].control &= ~0x01000000;
            dma->channels[ch].control |= 0x80000000; // Set 'DMA complete' flag (not real, but plausible)
            // Optionally, set interrupt flag (handled in event handler)
        }
    }
} 