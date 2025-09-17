#ifndef PSX_TYPES_H
#define PSX_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

// PSX-SPX: Basic types for PlayStation emulation
typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int8_t   s8;
typedef int16_t  s16;
typedef int32_t  s32;

// PSX-SPX: Memory Map (Section: Memory Map)
// Main RAM: 2MB at 00000000h-001FFFFFh
#define PSX_RAM_BASE        0x00000000
#define PSX_RAM_SIZE        0x00200000  // 2MB
#define PSX_RAM_MASK        0x001FFFFF

// Scratchpad: 1KB at 1F800000h-1F8003FFh  
#define PSX_SCRATCHPAD_BASE 0x1F800000
#define PSX_SCRATCHPAD_SIZE 0x00000400  // 1KB
#define PSX_SCRATCHPAD_MASK 0x000003FF

// Hardware Registers: 1F801000h-1F802FFFh
#define PSX_HW_BASE         0x1F801000

// PSX-SPX: Hardware Register Addresses
#define PSX_DMA_BASE        0x1F801080  // DMA Controller
#define PSX_TIMER_BASE      0x1F801100  // Timer Registers
#define PSX_IRQ_BASE        0x1F801070  // Interrupt Controller
#define PSX_GPU_BASE        0x1F801810  // Graphics Controller
#define PSX_SPU_BASE        0x1F801C00  // Sound Controller
#define PSX_CDROM_BASE      0x1F801800  // CD-ROM Controller
#define PSX_SIO_BASE        0x1F801040  // Serial I/O (Controllers/Memory Cards)
#define PSX_MDEC_BASE       0x1F801820  // MDEC (Motion Decoder)

// BIOS: 512KB at BFC00000h-BFC7FFFFh
#define PSX_BIOS_BASE       0xBFC00000
#define PSX_BIOS_SIZE       0x00080000  // 512KB
#define PSX_BIOS_MASK       0x0007FFFF

// Expansion Region 1: 8MB at 1F000000h-1F7FFFFFh
#define PSX_EXP1_BASE       0x1F000000
#define PSX_EXP1_SIZE       0x00800000  // 8MB

// PSX-SPX: Address translation masks for mirrored regions
#define PSX_ADDR_MASK_2MB   0x001FFFFF  // RAM mirroring
#define PSX_ADDR_MASK_8MB   0x007FFFFF  // Various regions

#endif // PSX_TYPES_H