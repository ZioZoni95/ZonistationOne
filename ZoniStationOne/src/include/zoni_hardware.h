#ifndef ZONI_HARDWARE_H
#define ZONI_HARDWARE_H

#include "zoni_common.h"

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
typedef struct {
    u8 scratchpad[PSX_HW_SCRATCH_SIZE];  // Scratchpad memory
    u8 hw_regs[PSX_HW_SIZE];            // Hardware registers
} zoni_hardware_t;

// Hardware functions
zoni_error_t zoni_hardware_init(zoni_hardware_t* hw);
void zoni_hardware_shutdown(zoni_hardware_t* hw);
void zoni_hardware_reset(zoni_hardware_t* hw);

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