#ifndef PSX_MEMORY_H
#define PSX_MEMORY_H

#include "psx_types.h"

// Guide.tex: Simple memory interface following PSX-SPX memory map

// Memory subsystem initialization
void memory_init(void);
void memory_shutdown(void);

// Basic memory interface
u32 memory_read32(u32 addr);
u16 memory_read16(u32 addr);
u8 memory_read8(u32 addr);

void memory_write32(u32 addr, u32 value);
void memory_write16(u32 addr, u16 value);
void memory_write8(u32 addr, u8 value);

// BIOS loading
bool memory_load_bios(const char* filename);

// Memory region access (for hardware components)
u32 ram_read32(u32 addr);
void ram_write32(u32 addr, u32 value);

u32 scratchpad_read32(u32 addr);
void scratchpad_write32(u32 addr, u32 value);

// Hardware register access (will forward to components)
u32 hw_read32(u32 addr);
void hw_write32(u32 addr, u32 value);

#endif // PSX_MEMORY_H