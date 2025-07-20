#ifndef NEWCORE_VRAM_H
#define NEWCORE_VRAM_H

#include <stdint.h>

// VRAM dimensions and size for PlayStation (1MB, 1024x512, 16bpp)
#define NC_VRAM_WIDTH 1024
#define NC_VRAM_HEIGHT 512
#define NC_VRAM_BPP 2
#define NC_VRAM_SIZE (NC_VRAM_WIDTH * NC_VRAM_HEIGHT * NC_VRAM_BPP)

// VRAM structure for newcore
typedef struct {
    uint8_t data[NC_VRAM_SIZE];
} NcVram;

// Initialize VRAM (fills with zeros)
void nc_vram_init(NcVram* vram);

// 32-bit read/write
uint32_t nc_vram_load32(NcVram* vram, uint32_t offset);
void nc_vram_store32(NcVram* vram, uint32_t offset, uint32_t value);

// 16-bit read/write
uint16_t nc_vram_load16(NcVram* vram, uint32_t offset);
void nc_vram_store16(NcVram* vram, uint32_t offset, uint16_t value);

// 8-bit read/write
uint8_t nc_vram_load8(NcVram* vram, uint32_t offset);
void nc_vram_store8(NcVram* vram, uint32_t offset, uint8_t value);

#endif // NEWCORE_VRAM_H 