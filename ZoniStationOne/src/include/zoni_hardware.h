#ifndef ZONI_HARDWARE_H
#define ZONI_HARDWARE_H

#include "zoni_common.h"

// Forward declaration
typedef struct zoni_memory_s zoni_memory_t;

// Hardware register ranges
#define PSX_HW_BASE        0x1F801000
#define PSX_HW_SIZE        0x8000
#define PSX_HW_SCRATCHPAD  0x1F800000
#define PSX_HW_SCRATCH_SIZE 0x400

// DMA registers
#define HW_DMA0_MADR       0x1F801080  // MDEC in DMA
#define HW_DMA0_BCR        0x1F801084
#define HW_DMA0_CHCR       0x1F801088

#define HW_DMA1_MADR       0x1F801090  // MDEC out DMA
#define HW_DMA1_BCR        0x1F801094
#define HW_DMA1_CHCR       0x1F801098

#define HW_DMA2_MADR       0x1F8010A0  // GPU DMA
#define HW_DMA2_BCR        0x1F8010A4
#define HW_DMA2_CHCR       0x1F8010A8

#define HW_DMA3_MADR       0x1F8010B0  // CDROM DMA
#define HW_DMA3_BCR        0x1F8010B4
#define HW_DMA3_CHCR       0x1F8010B8

#define HW_DMA4_MADR       0x1F8010C0  // SPU DMA
#define HW_DMA4_BCR        0x1F8010C4
#define HW_DMA4_CHCR       0x1F8010C8

#define HW_DMA6_MADR       0x1F8010E0  // GPU DMA (OT)
#define HW_DMA6_BCR        0x1F8010E4
#define HW_DMA6_CHCR       0x1F8010E8

#define HW_DMA_PCR         0x1F8010F0
#define HW_DMA_ICR         0x1F8010F4

// DMA channel control register bits
#define DMA_CHCR_TR        0x01000000  // Transfer direction (0=to device, 1=from device)
#define DMA_CHCR_CO        0x02000000  // Chopping enable
#define DMA_CHCR_CH        0x04000000  // Chopping DMA window size
#define DMA_CHCR_LN        0x08000000  // Link to next block
#define DMA_CHCR_TD        0x10000000  // Transfer direction (0=to device, 1=from device)
#define DMA_CHCR_DR        0x20000000  // DMA request
#define DMA_CHCR_LE        0x40000000  // Link enable
#define DMA_CHCR_DE        0x80000000  // DMA enable

// DMA block control register
#define DMA_BCR_SIZE       0x0000FFFF  // Transfer size
#define DMA_BCR_BS         0xFFFF0000  // Block size

// DMA priority control register
#define DMA_PCR_PRIO0      0x000000FF  // DMA0 priority
#define DMA_PCR_PRIO1      0x0000FF00  // DMA1 priority
#define DMA_PCR_PRIO2      0x00FF0000  // DMA2 priority
#define DMA_PCR_PRIO3      0xFF000000  // DMA3 priority

// DMA interrupt control register
#define DMA_ICR_IRQ0       0x00000001  // DMA0 interrupt
#define DMA_ICR_IRQ1       0x00000002  // DMA1 interrupt
#define DMA_ICR_IRQ2       0x00000004  // DMA2 interrupt
#define DMA_ICR_IRQ3       0x00000008  // DMA3 interrupt
#define DMA_ICR_IRQ4       0x00000010  // DMA4 interrupt
#define DMA_ICR_IRQ5       0x00000020  // DMA5 interrupt
#define DMA_ICR_IRQ6       0x00000040  // DMA6 interrupt
#define DMA_ICR_IRQ7       0x00000080  // DMA7 interrupt
#define DMA_ICR_MASTER     0x80000000  // Master interrupt enable

// DMA channel structure
typedef struct {
    u32 madr;      // Memory address
    u32 bcr;       // Block control
    u32 chcr;      // Channel control
    bool active;   // Channel active
    u32 current_addr;  // Current transfer address
    u32 remaining;     // Remaining bytes to transfer
} zoni_dma_channel_t;

// Interrupt registers
#define HW_ISTAT           0x1F801070  // Interrupt Status
#define HW_IMASK           0x1F801074  // Interrupt Mask

// Timer registers
#define HW_RCNT0           0x1F801100  // Timer 0 Count
#define HW_RMODE0          0x1F801104  // Timer 0 Mode
#define HW_RTARGET0        0x1F801108  // Timer 0 Target

#define HW_RCNT1           0x1F801110  // Timer 1 Count
#define HW_RMODE1          0x1F801114  // Timer 1 Mode
#define HW_RTARGET1        0x1F801118  // Timer 1 Target

#define HW_RCNT2           0x1F801120  // Timer 2 Count
#define HW_RMODE2          0x1F801124  // Timer 2 Mode
#define HW_RTARGET2        0x1F801128  // Timer 2 Target

// GPU registers
#define HW_GPU_DATA        0x1F801810  // GPU Data
#define HW_GPU_STATUS      0x1F801814  // GPU Status

// CDROM registers
#define HW_CDROM_DATA      0x1F801800  // CDROM Data
#define HW_CDROM_STATUS    0x1F801801  // CDROM Status
#define HW_CDROM_MODE      0x1F801802  // CDROM Mode
#define HW_CDROM_CONTROL   0x1F801803  // CDROM Control

// SIO registers
#define HW_SIO_DATA        0x1F801040  // SIO Data
#define HW_SIO_STAT        0x1F801044  // SIO Status
#define HW_SIO_MODE        0x1F801048  // SIO Mode
#define HW_SIO_CTRL        0x1F80104A  // SIO Control
#define HW_SIO_BAUD        0x1F80104E  // SIO Baud Rate

// Hardware structure
typedef struct zoni_hardware_s {
    u8 scratchpad[PSX_HW_SCRATCH_SIZE];  // Scratchpad memory
    u8 hw_regs[PSX_HW_SIZE];            // Hardware registers
    
    // DMA controller state
    zoni_dma_channel_t dma_channels[7];  // DMA channels 0-6
    u32 dma_pcr;                         // Priority control register
    u32 dma_icr;                         // Interrupt control register
    
    zoni_memory_t* memory;               // Pointer to memory system for GPU access
} zoni_hardware_t;

// Hardware functions
zoni_error_t zoni_hardware_init(zoni_hardware_t* hw);
void zoni_hardware_shutdown(zoni_hardware_t* hw);
void zoni_hardware_reset(zoni_hardware_t* hw);

// DMA controller functions
void zoni_dma_init(zoni_hardware_t* hw);
void zoni_dma_reset(zoni_hardware_t* hw);
void zoni_dma_update(zoni_hardware_t* hw);
zoni_error_t zoni_dma_write_channel(zoni_hardware_t* hw, u32 channel, u32 reg, u32 value);
u32 zoni_dma_read_channel(zoni_hardware_t* hw, u32 channel, u32 reg);

// Hardware read/write functions
u8 zoni_hw_read8(zoni_hardware_t* hw, u32 address);
u16 zoni_hw_read16(zoni_hardware_t* hw, u32 address);
u32 zoni_hw_read32(zoni_hardware_t* hw, u32 address);

zoni_error_t zoni_hw_write8(zoni_hardware_t* hw, u32 address, u8 value);
zoni_error_t zoni_hw_write16(zoni_hardware_t* hw, u32 address, u16 value);
zoni_error_t zoni_hw_write32(zoni_hardware_t* hw, u32 address, u32 value);

// Helper macros for register access
#define HW_READ8(hw, addr)   zoni_hw_read8(hw, addr)
#define HW_READ16(hw, addr)  zoni_hw_read16(hw, addr)
#define HW_READ32(hw, addr)  zoni_hw_read32(hw, addr)

#define HW_WRITE8(hw, addr, val)   zoni_hw_write8(hw, addr, val)
#define HW_WRITE16(hw, addr, val)  zoni_hw_write16(hw, addr, val)
#define HW_WRITE32(hw, addr, val)  zoni_hw_write32(hw, addr, val)

#endif // ZONI_HARDWARE_H 