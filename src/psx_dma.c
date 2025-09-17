#include "../include/psx_dma.h"
#include <stdio.h>
#include <string.h>

// PSX-SPX: DMA Controller implementation (skeleton)
static psx_dma_t dma;

void dma_init(void) {
    memset(&dma, 0, sizeof(dma));
    dma_reset();
    printf("[DMA] DMA controller initialized\n");
}

void dma_reset(void) {
    // PSX-SPX: Reset all channels
    for (int i = 0; i < 7; i++) {
        dma.channels[i].base_addr = 0;
        dma.channels[i].block_ctrl = 0;
        dma.channels[i].channel_ctrl = 0;
        dma.channels[i].active = false;
        dma.channels[i].current_addr = 0;
        dma.channels[i].remaining_blocks = 0;
        dma.channels[i].remaining_words = 0;
    }
    
    dma.dpcr = 0x07654321;  // PSX-SPX: Default priority
    dma.dicr = 0;
    
    printf("[DMA] DMA controller reset\n");
}

void dma_step(void) {
    // TODO: Implement DMA transfers
    // Guide.tex: Handle active channels
}

u32 dma_read32(u32 addr) {
    // PSX-SPX: Decode DMA register address
    
    if (addr == DMA_DPCR) {
        return dma.dpcr;
    }
    
    if (addr == DMA_DICR) {
        return dma.dicr;
    }
    
    // Channel registers
    if (addr >= 0x1F801080 && addr <= 0x1F8010E8) {
        int channel = (addr - 0x1F801080) / 0x10;
        int reg = (addr - 0x1F801080) % 0x10;
        
        if (channel < 7) {
            switch (reg) {
                case 0x0: return dma.channels[channel].base_addr;
                case 0x4: return dma.channels[channel].block_ctrl;
                case 0x8: return dma.channels[channel].channel_ctrl;
            }
        }
    }
    
    printf("[DMA] ERROR: Unmapped read32 at 0x%08X\n", addr);
    return 0;
}

void dma_write32(u32 addr, u32 value) {
    // PSX-SPX: Decode DMA register address
    
    if (addr == DMA_DPCR) {
        dma.dpcr = value;
        printf("[DMA] DPCR = 0x%08X\n", value);
        return;
    }
    
    if (addr == DMA_DICR) {
        dma.dicr = value;
        printf("[DMA] DICR = 0x%08X\n", value);
        return;
    }
    
    // Channel registers
    if (addr >= 0x1F801080 && addr <= 0x1F8010E8) {
        int channel = (addr - 0x1F801080) / 0x10;
        int reg = (addr - 0x1F801080) % 0x10;
        
        if (channel < 7) {
            switch (reg) {
                case 0x0: // Base Address
                    dma.channels[channel].base_addr = value;
                    printf("[DMA] Ch%d Base = 0x%08X\n", channel, value);
                    break;
                case 0x4: // Block Control
                    dma.channels[channel].block_ctrl = value;
                    printf("[DMA] Ch%d Block = 0x%08X\n", channel, value);
                    break;
                case 0x8: // Channel Control
                    dma.channels[channel].channel_ctrl = value;
                    printf("[DMA] Ch%d Control = 0x%08X\n", channel, value);
                    
                    // Guide.tex: Check if channel should start
                    if (value & 0x01000000) {  // Start bit
                        dma_start_channel(channel);
                    }
                    break;
            }
            return;
        }
    }
    
    printf("[DMA] ERROR: Unmapped write32 at 0x%08X = 0x%08X\n", addr, value);
}

void dma_start_channel(int channel) {
    printf("[DMA] TODO: Start channel %d\n", channel);
    // TODO: Implement channel transfer logic
}

void dma_complete_channel(int channel) {
    printf("[DMA] TODO: Complete channel %d\n", channel);
    // TODO: Clear start bit, trigger interrupt
}