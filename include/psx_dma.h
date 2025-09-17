#ifndef PSX_DMA_H
#define PSX_DMA_H

#include "psx_types.h"

// PSX-SPX: DMA Controller Implementation
// Following guide.tex DMA structure with PSX-SPX register specifications

// PSX-SPX: DMA Channel definitions
#define DMA_CHANNEL_MDECIN  0  // MDEC Input
#define DMA_CHANNEL_MDECOUT 1  // MDEC Output
#define DMA_CHANNEL_GPU     2  // GPU
#define DMA_CHANNEL_CDROM   3  // CD-ROM
#define DMA_CHANNEL_SPU     4  // Sound
#define DMA_CHANNEL_PIO     5  // Expansion Port
#define DMA_CHANNEL_OTC     6  // Ordering Table Clear

// PSX-SPX: DMA Register offsets
#define DMA_CH0_BASE_ADDR   0x1F801080  // Channel 0 Base Address
#define DMA_CH0_BLOCK_CTRL  0x1F801084  // Channel 0 Block Control
#define DMA_CH0_CHANNEL_CTRL 0x1F801088 // Channel 0 Channel Control

#define DMA_DPCR           0x1F8010F0  // DMA Priority Control
#define DMA_DICR           0x1F8010F4  // DMA Interrupt Control

// PSX-SPX: DMA Channel structure
typedef struct {
    u32 base_addr;      // Base address register
    u32 block_ctrl;     // Block control register
    u32 channel_ctrl;   // Channel control register
    
    // Internal state
    bool active;
    u32 current_addr;
    u32 remaining_blocks;
    u32 remaining_words;
} dma_channel_t;

// PSX-SPX: DMA Controller state
typedef struct {
    dma_channel_t channels[7];  // 7 DMA channels
    u32 dpcr;                   // DMA Priority Control
    u32 dicr;                   // DMA Interrupt Control
} psx_dma_t;

// DMA interface functions
void dma_init(void);
void dma_reset(void);
void dma_step(void);

// Register access
u32 dma_read32(u32 addr);
void dma_write32(u32 addr, u32 value);

// Channel operations  
void dma_start_channel(int channel);
void dma_complete_channel(int channel);

#endif // PSX_DMA_H