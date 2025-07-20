#include "../include/interconnect.h"
#include "../include/log.h"
#include <stdio.h>
#include "../include/emulator.h" // For EmulatorContext cast

// RAM region handlers
uint32_t ram_read32(void* ctx, uint32_t addr) {
    EmulatorContext* ectx = (EmulatorContext*)ctx;
    uint32_t offset = addr & (sizeof(ectx->ram.data) - 1);
    uint8_t* data = ectx->ram.data;
    uint32_t b0 = data[offset + 0];
    uint32_t b1 = data[offset + 1];
    uint32_t b2 = data[offset + 2];
    uint32_t b3 = data[offset + 3];
    uint32_t value = b0 | (b1 << 8) | (b2 << 16) | (b3 << 24);
    NC_LOGI("RAM read32: addr=0x%08x value=0x%08x", addr, value);
    return value;
}
void ram_write32(void* ctx, uint32_t addr, uint32_t value) {
    EmulatorContext* ectx = (EmulatorContext*)ctx;
    uint32_t offset = addr & (sizeof(ectx->ram.data) - 1);
    uint8_t* data = ectx->ram.data;
    data[offset + 0] = (uint8_t)(value & 0xFF);
    data[offset + 1] = (uint8_t)((value >> 8) & 0xFF);
    data[offset + 2] = (uint8_t)((value >> 16) & 0xFF);
    data[offset + 3] = (uint8_t)((value >> 24) & 0xFF);
    NC_LOGI("RAM write32: addr=0x%08x value=0x%08x", addr, value);
}

// BIOS region handlers
uint32_t bios_read32(void* ctx, uint32_t addr) {
    EmulatorContext* ectx = (EmulatorContext*)ctx;
    uint32_t offset = addr & (sizeof(ectx->bios.data) - 1);
    uint8_t* data = ectx->bios.data;
    uint32_t b0 = data[offset + 0];
    uint32_t b1 = data[offset + 1];
    uint32_t b2 = data[offset + 2];
    uint32_t b3 = data[offset + 3];
    uint32_t value = b0 | (b1 << 8) | (b2 << 16) | (b3 << 24);
    NC_LOGI("BIOS read32: addr=0x%08x value=0x%08x", addr, value);
    return value;
}
void bios_write32(void* ctx, uint32_t addr, uint32_t value) {
    // BIOS is read-only
    NC_LOGI("BIOS write32 IGNORED: addr=0x%08x value=0x%08x", addr, value);
}

// DMA region handlers
uint32_t dma_read32(void* ctx, uint32_t addr) {
    NC_LOGI("DMA read32: addr=0x%08x", addr);
    // TODO: Implement real DMA register reads
    return 0;
}
void dma_write32(void* ctx, uint32_t addr, uint32_t value) {
    NC_LOGI("DMA write32: addr=0x%08x value=0x%08x", addr, value);
    // TODO: Implement real DMA register writes
}

// Memory region table
#undef NUM_REGIONS
#define NUM_REGIONS 3
static NcMemRegion memory_map[NUM_REGIONS] = {
    { 0x00000000, 0x001FFFFF, ram_read32, ram_write32 },      // RAM (2MB)
    { 0x1FC00000, 0x1FC7FFFF, bios_read32, bios_write32 },    // BIOS (512KB)
    { 0x1F801080, 0x1F8010FF, dma_read32, dma_write32 },      // DMA registers
    // Add VRAM, hardware, etc. as needed
};

uint32_t nc_interconnect_read32(struct NcInterconnect* inter, uint32_t addr) {
    EmulatorContext* ctx = (EmulatorContext*)((char*)inter - offsetof(EmulatorContext, interconnect));
    for (int i = 0; i < NUM_REGIONS; ++i) {
        if (addr >= memory_map[i].start && addr <= memory_map[i].end) {
            return memory_map[i].read32(ctx, addr);
        }
    }
    NC_LOGI("Unmapped read32: addr=0x%08x", addr);
    return 0xFFFFFFFF;
}

void nc_interconnect_write32(struct NcInterconnect* inter, uint32_t addr, uint32_t value) {
    EmulatorContext* ctx = (EmulatorContext*)((char*)inter - offsetof(EmulatorContext, interconnect));
    for (int i = 0; i < NUM_REGIONS; ++i) {
        if (addr >= memory_map[i].start && addr <= memory_map[i].end) {
            memory_map[i].write32(ctx, addr, value);
            return;
        }
    }
    NC_LOGI("Unmapped write32: addr=0x%08x value=0x%08x", addr, value);
} 