#ifndef NEWCORE_RAM_H
#define NEWCORE_RAM_H

#include <stdint.h>

// PlayStation RAM size: 2MB
#define NC_RAM_SIZE (2 * 1024 * 1024)

// RAM structure for newcore
typedef struct {
    uint8_t data[NC_RAM_SIZE];
} NcRam;

// Initialize RAM (fills with a recognizable pattern)
void nc_ram_init(NcRam* ram);

// 32-bit read/write
uint32_t nc_ram_load32(NcRam* ram, uint32_t offset);
void nc_ram_store32(NcRam* ram, uint32_t offset, uint32_t value);

// 16-bit read/write
uint16_t nc_ram_load16(NcRam* ram, uint32_t offset);
void nc_ram_store16(NcRam* ram, uint32_t offset, uint16_t value);

// 8-bit read/write
uint8_t nc_ram_load8(NcRam* ram, uint32_t offset);
void nc_ram_store8(NcRam* ram, uint32_t offset, uint8_t value);

#endif // NEWCORE_RAM_H 