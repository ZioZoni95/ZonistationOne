/*
 * ZonistationOne - PlayStation One Emulator
 * Core System Definitions and Types
 */

#ifndef PSX_SYSTEM_H
#define PSX_SYSTEM_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* PlayStation One hardware specifications */
#define PSX_CPU_CLOCK_NTSC      33868800    /* 33.8688 MHz */
#define PSX_CPU_CLOCK_PAL       33868800    /* Same for PAL */
#define PSX_RAM_SIZE            0x200000    /* 2MB main RAM */
#define PSX_VRAM_SIZE           0x100000    /* 1MB video RAM */
#define PSX_BIOS_SIZE           0x80000     /* 512KB BIOS ROM */
#define PSX_SCRATCHPAD_SIZE     0x400       /* 1KB scratchpad */

/* Memory map constants */
#define PSX_RAM_BASE            0x00000000
#define PSX_RAM_SIZE_MASK       0x1FFFFF
#define PSX_BIOS_BASE           0x1FC00000
#define PSX_HW_REGS_BASE        0x1F800000
#define PSX_SCRATCHPAD_BASE     0x1F800000
#define PSX_IO_PORTS_BASE       0x1F801000
#define PSX_EXPANSION_1_BASE    0x1F000000
#define PSX_EXPANSION_2_BASE    0x1F802000
#define PSX_EXPANSION_3_BASE    0x1FA00000

/* Interrupt masks */
#define PSX_IRQ_VBLANK          0x01
#define PSX_IRQ_GPU             0x02
#define PSX_IRQ_CDROM           0x04
#define PSX_IRQ_DMA             0x08
#define PSX_IRQ_TIMER0          0x10
#define PSX_IRQ_TIMER1          0x20
#define PSX_IRQ_TIMER2          0x40
#define PSX_IRQ_CONTROLLER      0x80
#define PSX_IRQ_SIO             0x100
#define PSX_IRQ_SPU             0x200
#define PSX_IRQ_LIGHTPEN        0x400

/* CPU register indices */
#define PSX_REG_ZERO            0
#define PSX_REG_AT              1
#define PSX_REG_V0              2
#define PSX_REG_V1              3
#define PSX_REG_A0              4
#define PSX_REG_A1              5
#define PSX_REG_A2              6
#define PSX_REG_A3              7
#define PSX_REG_T0              8
#define PSX_REG_T1              9
#define PSX_REG_T2              10
#define PSX_REG_T3              11
#define PSX_REG_T4              12
#define PSX_REG_T5              13
#define PSX_REG_T6              14
#define PSX_REG_T7              15
#define PSX_REG_S0              16
#define PSX_REG_S1              17
#define PSX_REG_S2              18
#define PSX_REG_S3              19
#define PSX_REG_S4              20
#define PSX_REG_S5              21
#define PSX_REG_S6              22
#define PSX_REG_S7              23
#define PSX_REG_T8              24
#define PSX_REG_T9              25
#define PSX_REG_K0              26
#define PSX_REG_K1              27
#define PSX_REG_GP              28
#define PSX_REG_SP              29
#define PSX_REG_FP              30
#define PSX_REG_RA              31

/* System status */
typedef enum {
    PSX_STATUS_STOPPED = 0,
    PSX_STATUS_RUNNING,
    PSX_STATUS_PAUSED,
    PSX_STATUS_ERROR
} psx_status_t;

/* Video modes */
typedef enum {
    PSX_VIDEO_NTSC = 0,
    PSX_VIDEO_PAL
} psx_video_mode_t;

/* Forward declarations */
typedef struct psx_emulator_s psx_emulator_t;
typedef struct psx_cpu_s psx_cpu_t;
typedef struct psx_gpu_s psx_gpu_t;
typedef struct psx_spu_s psx_spu_t;
typedef struct psx_memory_s psx_memory_t;
typedef struct psx_cdrom_s psx_cdrom_t;

/* Utility macros */
#define PSX_ALIGN(x, align)     (((x) + (align) - 1) & ~((align) - 1))
#define PSX_ARRAY_SIZE(arr)     (sizeof(arr) / sizeof((arr)[0]))
#define PSX_MIN(a, b)           ((a) < (b) ? (a) : (b))
#define PSX_MAX(a, b)           ((a) > (b) ? (a) : (b))

/* Endianness handling */
#ifdef PSX_BIG_ENDIAN
#define PSX_CPU_TO_LE32(x)      __builtin_bswap32(x)
#define PSX_LE32_TO_CPU(x)      __builtin_bswap32(x)
#define PSX_CPU_TO_LE16(x)      __builtin_bswap16(x)
#define PSX_LE16_TO_CPU(x)      __builtin_bswap16(x)
#else
#define PSX_CPU_TO_LE32(x)      (x)
#define PSX_LE32_TO_CPU(x)      (x)
#define PSX_CPU_TO_LE16(x)      (x)
#define PSX_LE16_TO_CPU(x)      (x)
#endif

#ifdef __cplusplus
}
#endif

#endif /* PSX_SYSTEM_H */