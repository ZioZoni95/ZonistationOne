#ifndef PSX_CDROM_H
#define PSX_CDROM_H

#include "psx_types.h"

// PSX-SPX: CD-ROM Controller Implementation
// Skeleton for future implementation

// PSX-SPX: CD-ROM Register base
#define CDROM_BASE 0x1F801800

typedef struct {
    // TODO: Add CDROM state
    u32 dummy;
} psx_cdrom_t;

// CDROM interface functions
void cdrom_init(void);
void cdrom_reset(void);
void cdrom_step(void);

// Register access
u32 cdrom_read32(u32 addr);
void cdrom_write32(u32 addr, u32 value);

#endif // PSX_CDROM_H