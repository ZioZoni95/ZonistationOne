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

// Scratchpad region handlers
uint32_t scratchpad_read32(void* ctx, uint32_t addr);
void     scratchpad_write32(void* ctx, uint32_t addr, uint32_t value);

// Timer region handlers
uint32_t timer_read32(void* ctx, uint32_t addr);
void     timer_write32(void* ctx, uint32_t addr, uint32_t value);

// Interrupt controller handlers
uint32_t irq_read32(void* ctx, uint32_t addr);
void     irq_write32(void* ctx, uint32_t addr, uint32_t value);

// SIO (Serial I/O) handlers
uint32_t sio_read32(void* ctx, uint32_t addr);
void     sio_write32(void* ctx, uint32_t addr, uint32_t value);

// CDROM handlers
uint32_t cdrom_read32(void* ctx, uint32_t addr);
void     cdrom_write32(void* ctx, uint32_t addr, uint32_t value);

// SPU (Sound Processing Unit) handlers
uint32_t spu_read32(void* ctx, uint32_t addr);
void     spu_write32(void* ctx, uint32_t addr, uint32_t value);

// Memory control handlers
uint32_t memctrl_read32(void* ctx, uint32_t addr);
void     memctrl_write32(void* ctx, uint32_t addr, uint32_t value);

// Hardware register fallback handlers
uint32_t hwreg_read32(void* ctx, uint32_t addr);
void     hwreg_write32(void* ctx, uint32_t addr, uint32_t value);

// VRAM region handlers
uint32_t vram_read32(void* ctx, uint32_t addr);
void     vram_write32(void* ctx, uint32_t addr, uint32_t value);

// GPU command region handlers
uint32_t gpu_read32(void* ctx, uint32_t addr);
void     gpu_write32(void* ctx, uint32_t addr, uint32_t value);

#endif // NEWCORE_INTERCONNECT_H 