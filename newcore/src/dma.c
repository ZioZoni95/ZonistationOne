#include "../include/dma.h"
#include "../include/log.h"
#include "../include/emulator.h"
#include <string.h>

void nc_dma_init(NcDma* dma) {
    memset(dma, 0, sizeof(NcDma));
    dma->control = 0x07654321; // Default value used by PCSX ReARMed
    NC_LOGI("DMA initialized");
}

void nc_dma_step(NcDma* dma) {
    // Minimal simulation: for each channel, if enabled, simulate a transfer and clear enable
    for (int ch = 0; ch < NC_DMA_CHANNELS; ++ch) {
        if (dma->channels[ch].control & 0x01000000) { // CHCR start bit
            // Simulate transfer complete: clear start bit
            dma->channels[ch].control &= ~0x01000000;
            // Set DMA interrupt flag (bit 3 in DMA interrupt reg)
            dma->interrupt |= (1 << 3);
            // Optionally, set global IRQ (I_STAT) if unmasked (done in event handler)
        }
    }
} 