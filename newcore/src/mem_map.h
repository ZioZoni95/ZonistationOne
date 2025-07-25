// mem_map.h
// Migrated from ram.c: RAM, BIOS, and memory mapping logic (header)
// TODO: Move RAM, BIOS, and memory mapping declarations here.

#ifndef MEM_MAP_H
#define MEM_MAP_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// --- Memory Subsystem State ---
typedef struct {
    uint8_t* ram;      // Main RAM
    size_t ram_size;
    uint8_t* bios;     // BIOS ROM
    size_t bios_size;
    // TODO: Add mapping tables, scratchpad, etc.
} MemMap;

// --- Memory Subsystem API ---
void mem_map_init(MemMap* mem);
uint8_t mem_map_read8(MemMap* mem, uint32_t addr);
uint16_t mem_map_read16(MemMap* mem, uint32_t addr);
uint32_t mem_map_read32(MemMap* mem, uint32_t addr);
void mem_map_write8(MemMap* mem, uint32_t addr, uint8_t value);
void mem_map_write16(MemMap* mem, uint32_t addr, uint16_t value);
void mem_map_write32(MemMap* mem, uint32_t addr, uint32_t value);

#endif // MEM_MAP_H 