#ifndef NEWCORE_BIOS_H
#define NEWCORE_BIOS_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// PlayStation BIOS size (512KB)
#define NC_BIOS_SIZE (512 * 1024)

// BIOS structure for newcore
typedef struct {
    uint8_t data[NC_BIOS_SIZE];
} NcBios;

// Load BIOS from file
bool nc_bios_load(NcBios* bios, const char* path);
// 32-bit read
uint32_t nc_bios_load32(NcBios* bios, uint32_t offset);
// 16-bit read
uint16_t nc_bios_load16(NcBios* bios, uint32_t offset);

#endif // NEWCORE_BIOS_H 