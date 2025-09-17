#ifndef PSX_SPU_H
#define PSX_SPU_H

#include "psx_types.h"

// PSX-SPX: Sound Processing Unit (SPU) Implementation
// Skeleton for future implementation

// PSX-SPX: SPU Register base
#define SPU_BASE 0x1F801C00

typedef struct {
    // TODO: Add SPU state
    u32 dummy;
} psx_spu_t;

// SPU interface functions
void spu_init(void);
void spu_reset(void);
void spu_step(void);

// Register access
u32 spu_read32(u32 addr);
void spu_write32(u32 addr, u32 value);

#endif // PSX_SPU_H