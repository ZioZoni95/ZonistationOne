/**
 * @file zoni_memory.h
 * @brief Memory management for ZoniStationOne
 * 
 * This file defines the PlayStation memory map and provides
 * memory access functions for the emulator.
 */

#ifndef ZONI_MEMORY_H
#define ZONI_MEMORY_H

#include "zoni_common.h"

// PlayStation memory map
#define PSX_MEMORY_REGIONS 8

typedef enum {
    PSX_MEM_RAM = 0,           // 0x00000000 - 0x01FFFFFF (32MB, mirrored)
    PSX_MEM_BIOS = 1,          // 0x1FC00000 - 0x1FFFFFFF (512KB)
    PSX_MEM_SCRATCHPAD = 2,    // 0x1F800000 - 0x1F8003FF (1KB)
    PSX_MEM_HW_REG = 3,        // 0x1F801000 - 0x1F802FFF (Hardware registers)
    PSX_MEM_CACHE_CTRL = 4,    // 0xFFFE0000 - 0xFFFEFFFF (Cache control)
    PSX_MEM_CDROM = 5,         // 0x1F801800 - 0x1F801803 (CDROM controller)
    PSX_MEM_SPU = 6,           // 0x1F801C00 - 0x1F801FFF (SPU registers)
    PSX_MEM_EXPANSION = 7      // 0x1F000000 - 0x1F7FFFFF (Expansion)
} psx_memory_region_t;

// Memory region structure
typedef struct {
    u32 base_address;
    u32 size;
    u8* data;
    bool writable;
    bool readable;
    const char* name;
} zoni_memory_region_t;

// Memory management structure
typedef struct {
    zoni_memory_region_t regions[PSX_MEMORY_REGIONS];
    u8* ram;                    // Main RAM (8MB)
    u8* bios;                   // BIOS (512KB)
    u8* scratchpad;             // Scratchpad (1KB)
    
    // Memory access callbacks
    zoni_error_t (*read8_callback)(u32 address, u8* value);
    zoni_error_t (*read16_callback)(u32 address, u16* value);
    zoni_error_t (*read32_callback)(u32 address, u32* value);
    
    zoni_error_t (*write8_callback)(u32 address, u8 value);
    zoni_error_t (*write16_callback)(u32 address, u16 value);
    zoni_error_t (*write32_callback)(u32 address, u32 value);
    
    // Memory access statistics
    u32 read_count;
    u32 write_count;
    u32 cache_hits;
    u32 cache_misses;
} zoni_memory_t;

// Memory access types
typedef enum {
    ZONI_MEM_ACCESS_READ = 0,
    ZONI_MEM_ACCESS_WRITE = 1
} zoni_mem_access_t;

// Memory access flags
typedef enum {
    ZONI_MEM_FLAG_NONE = 0,
    ZONI_MEM_FLAG_ALIGNED = 1,
    ZONI_MEM_FLAG_SIGNED = 2,
    ZONI_MEM_FLAG_CACHEABLE = 4
} zoni_mem_flags_t;

// Memory functions
zoni_error_t zoni_memory_init(zoni_memory_t* memory);
void zoni_memory_shutdown(zoni_memory_t* memory);
void zoni_memory_reset(zoni_memory_t* memory);

// Memory region management
zoni_error_t zoni_memory_map_region(zoni_memory_t* memory, psx_memory_region_t region,
                                   u32 base_address, u32 size, u8* data,
                                   bool readable, bool writable, const char* name);
zoni_error_t zoni_memory_unmap_region(zoni_memory_t* memory, psx_memory_region_t region);

// Basic memory access functions
zoni_error_t zoni_memory_read8(zoni_memory_t* memory, u32 address, u8* value);
zoni_error_t zoni_memory_read16(zoni_memory_t* memory, u32 address, u16* value);
zoni_error_t zoni_memory_read32(zoni_memory_t* memory, u32 address, u32* value);

zoni_error_t zoni_memory_write8(zoni_memory_t* memory, u32 address, u8 value);
zoni_error_t zoni_memory_write16(zoni_memory_t* memory, u32 address, u16 value);
zoni_error_t zoni_memory_write32(zoni_memory_t* memory, u32 address, u32 value);

// Signed memory access functions
zoni_error_t zoni_memory_read8s(zoni_memory_t* memory, u32 address, s8* value);
zoni_error_t zoni_memory_read16s(zoni_memory_t* memory, u32 address, s16* value);

// Memory block operations
zoni_error_t zoni_memory_read_block(zoni_memory_t* memory, u32 address, void* buffer, u32 size);
zoni_error_t zoni_memory_write_block(zoni_memory_t* memory, u32 address, const void* buffer, u32 size);

// Memory utility functions
bool zoni_memory_is_valid_address(zoni_memory_t* memory, u32 address);
u32 zoni_memory_get_region_index(zoni_memory_t* memory, u32 address);
const zoni_memory_region_t* zoni_memory_get_region(zoni_memory_t* memory, u32 address);

// Memory debugging functions
void zoni_memory_dump_region(zoni_memory_t* memory, psx_memory_region_t region, u32 offset, u32 size);
void zoni_memory_dump_stats(zoni_memory_t* memory);
void zoni_memory_validate_address(zoni_memory_t* memory, u32 address, zoni_mem_access_t access);

// Memory access callbacks
typedef zoni_error_t (*zoni_memory_read_callback)(u32 address, void* value, u32 size);
typedef zoni_error_t (*zoni_memory_write_callback)(u32 address, const void* value, u32 size);

zoni_error_t zoni_memory_set_callbacks(zoni_memory_t* memory,
                                      zoni_memory_read_callback read_callback,
                                      zoni_memory_write_callback write_callback);

// Memory mapping constants
#define PSX_RAM_SIZE (8 * 1024 * 1024)
#define PSX_BIOS_SIZE (512 * 1024)
// PSX_SCRATCHPAD_SIZE is already defined in zoni_common.h

// Memory addresses
#define PSX_RAM_BASE 0x00000000
#define PSX_RAM_END 0x01FFFFFF
#define PSX_BIOS_BASE 0x1FC00000
#define PSX_BIOS_END 0x1FFFFFFF
#define PSX_SCRATCHPAD_BASE 0x1F800000
#define PSX_SCRATCHPAD_END 0x1F8003FF

// Hardware register addresses
#define PSX_HW_BASE 0x1F801000
#define PSX_HW_END 0x1F802FFF

// CDROM controller addresses
#define PSX_CDROM_BASE 0x1F801800
#define PSX_CDROM_END 0x1F801803

// SPU addresses
#define PSX_SPU_BASE 0x1F801C00
#define PSX_SPU_END 0x1F801FFF

// Expansion addresses
#define PSX_EXPANSION_BASE 0x1F000000
#define PSX_EXPANSION_END 0x1F7FFFFF

// Cache control addresses
#define PSX_CACHE_CTRL_BASE 0xFFFE0000
#define PSX_CACHE_CTRL_END 0xFFFEFFFF

// Memory access macros
#define ZONI_MEM_READ8(mem, addr, val) zoni_memory_read8(mem, addr, val)
#define ZONI_MEM_READ16(mem, addr, val) zoni_memory_read16(mem, addr, val)
#define ZONI_MEM_READ32(mem, addr, val) zoni_memory_read32(mem, addr, val)

#define ZONI_MEM_WRITE8(mem, addr, val) zoni_memory_write8(mem, addr, val)
#define ZONI_MEM_WRITE16(mem, addr, val) zoni_memory_write16(mem, addr, val)
#define ZONI_MEM_WRITE32(mem, addr, val) zoni_memory_write32(mem, addr, val)

zoni_error_t zoni_memory_load_bios(zoni_memory_t* memory, const char* bios_path);

#endif // ZONI_MEMORY_H