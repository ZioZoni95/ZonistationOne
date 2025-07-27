#ifndef ZONISTATION_MEMORY_H
#define ZONISTATION_MEMORY_H

#include "zonistation_common.h"

#ifdef __cplusplus
extern "C" {
#endif

// Memory mapping tags (migrated from PCSX-ReARMed)
typedef enum {
    ZS_MAP_TAG_OTHER = 0,
    ZS_MAP_TAG_RAM,
    ZS_MAP_TAG_VRAM,
    ZS_MAP_TAG_LUTS,
} zs_map_tag_t;

// Memory structure
typedef struct zs_memory_t {
    // Memory regions
    zs_u8* ram;                    // 2MB main RAM
    zs_u8* bios;                   // 512KB BIOS
    zs_u8* scratchpad;             // 1KB scratchpad
    zs_u8* hardware_regs;          // 8KB hardware registers
    
    // Memory mapping
    zs_bool initialized;
    zs_size_t ram_size;
    zs_size_t bios_size;
    zs_size_t scratchpad_size;
    zs_size_t hardware_regs_size;
    
} zs_memory_t;

// Memory initialization and shutdown
zs_error_t zs_memory_init(zs_memory_t** memory);
zs_error_t zs_memory_shutdown(zs_memory_t* memory);
zs_error_t zs_memory_reset(zs_memory_t* memory);

// Memory access functions
zs_error_t zs_memory_read(zs_memory_t* memory, zs_u32 address, zs_u8* data, zs_size_t size);
zs_error_t zs_memory_write(zs_memory_t* memory, zs_u32 address, const zs_u8* data, zs_size_t size);

// Convenience functions
zs_u8 zs_memory_read_byte(zs_memory_t* memory, zs_u32 address);
zs_u16 zs_memory_read_halfword(zs_memory_t* memory, zs_u32 address);
zs_u32 zs_memory_read_word(zs_memory_t* memory, zs_u32 address);
zs_error_t zs_memory_write_byte(zs_memory_t* memory, zs_u32 address, zs_u8 value);
zs_error_t zs_memory_write_halfword(zs_memory_t* memory, zs_u32 address, zs_u16 value);
zs_error_t zs_memory_write_word(zs_memory_t* memory, zs_u32 address, zs_u32 value);

// Fast memory access functions (migrated from PCSX-ReARMed)
zs_u8 zs_memory_read_byte_fast(zs_memory_t* memory, zs_u32 address);
zs_u16 zs_memory_read_halfword_fast(zs_memory_t* memory, zs_u32 address);
zs_u32 zs_memory_read_word_fast(zs_memory_t* memory, zs_u32 address);
zs_error_t zs_memory_write_byte_fast(zs_memory_t* memory, zs_u32 address, zs_u8 value);
zs_error_t zs_memory_write_halfword_fast(zs_memory_t* memory, zs_u32 address, zs_u16 value);
zs_error_t zs_memory_write_word_fast(zs_memory_t* memory, zs_u32 address, zs_u32 value);

// BIOS loading
zs_error_t zs_memory_load_bios(zs_memory_t* memory, const char* filename);

// Memory mapping
zs_error_t zs_memory_map_region(zs_memory_t* memory, zs_u32 address, zs_size_t size, zs_u8* data);
zs_error_t zs_memory_unmap_region(zs_memory_t* memory, zs_u32 address);

// Advanced memory mapping (migrated from PCSX-ReARMed)
void* zs_memory_map(zs_u32 addr, zs_size_t size, zs_bool is_fixed, zs_map_tag_t tag);
void zs_memory_unmap(void* ptr, zs_size_t size, zs_map_tag_t tag);
void* zs_memory_get_pointer(zs_memory_t* memory, zs_u32 address);

// Memory mapping hooks
void zs_memory_set_map_hooks(
    void* (*map_hook)(zs_u32 addr, zs_size_t size, zs_map_tag_t tag, zs_bool* can_retry_addr),
    void (*unmap_hook)(void* ptr, zs_size_t size, zs_map_tag_t tag)
);

#ifdef __cplusplus
}
#endif

#endif // ZONISTATION_MEMORY_H 