#ifndef NEWCORE_INTERCONNECT_H
#define NEWCORE_INTERCONNECT_H

#include <stdint.h>
struct EmulatorContext;
struct NcInterconnect {
    // Add interconnect state here as needed
};

// Function pointer types for memory region handlers (use void* for context)
typedef uint32_t (*MemRead32)(void* ctx, uint32_t addr);
typedef void     (*MemWrite32)(void* ctx, uint32_t addr, uint32_t value);

typedef struct {
    uint32_t start;
    uint32_t end;
    MemRead32  read32;
    MemWrite32 write32;
} NcMemRegion;

uint32_t nc_interconnect_read32(struct NcInterconnect* inter, uint32_t addr);
void     nc_interconnect_write32(struct NcInterconnect* inter, uint32_t addr, uint32_t value);

// RAM/BIOS region handlers (to be implemented)
uint32_t ram_read32(void* ctx, uint32_t addr);
void     ram_write32(void* ctx, uint32_t addr, uint32_t value);
uint32_t bios_read32(void* ctx, uint32_t addr);
void     bios_write32(void* ctx, uint32_t addr, uint32_t value);

// DMA region handlers (to be implemented)
uint32_t dma_read32(void* ctx, uint32_t addr);
void     dma_write32(void* ctx, uint32_t addr, uint32_t value);

#endif // NEWCORE_INTERCONNECT_H 