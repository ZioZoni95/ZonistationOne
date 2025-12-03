#ifndef MEMORY_H
#define MEMORY_H

#include "psx_types.h"

// Memory subsystem function declarations

// Initialize memory system
psx_result_t memory_init(psx_memory_t* memory);

// Load BIOS from file
psx_result_t memory_load_bios(psx_memory_t* memory, const char* bios_path);

// Memory read functions (with address translation)
u8  memory_read8(const psx_memory_t* memory, u32 address);
u16 memory_read16(const psx_memory_t* memory, u32 address);
u32 memory_read32(const psx_memory_t* memory, u32 address);

// Memory write functions (with address translation)
void memory_write8(psx_memory_t* memory, u32 address, u8 value);
void memory_write16(psx_memory_t* memory, u32 address, u16 value);
void memory_write32(psx_memory_t* memory, u32 address, u32 value);

// Address translation helpers
u32 memory_translate_address(u32 virtual_address);
bool memory_is_valid_address(u32 address);

// Memory region detection
typedef enum {
    MEMORY_REGION_RAM,
    MEMORY_REGION_BIOS,
    MEMORY_REGION_SCRATCHPAD,
    MEMORY_REGION_IO,
    MEMORY_REGION_INVALID
} memory_region_t;

memory_region_t memory_get_region(u32 address);

// Debug helpers
void memory_dump_region(const psx_memory_t* memory, u32 start_addr, u32 length);

#endif // MEMORY_H