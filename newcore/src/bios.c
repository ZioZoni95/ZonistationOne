#include "../include/bios.h"
#include "../include/log.h"
#include <stdio.h>
#include <string.h>

// Load BIOS from file
bool nc_bios_load(NcBios* bios, const char* path) {
    FILE* file = fopen(path, "rb");
    if (!file) {
        NC_LOGE("Failed to open BIOS file: %s", path);
        return false;
    }
    size_t bytes_read = fread(bios->data, 1, NC_BIOS_SIZE, file);
    fclose(file);
    if (bytes_read != NC_BIOS_SIZE) {
        NC_LOGE("BIOS file size mismatch: read %zu bytes, expected %d", bytes_read, NC_BIOS_SIZE);
        return false;
    }
    NC_LOGI("BIOS loaded from %s (%d bytes)", path, NC_BIOS_SIZE);
    return true;
}

// 32-bit read from BIOS
uint32_t nc_bios_load32(NcBios* bios, uint32_t offset) {
    if (offset > NC_BIOS_SIZE - 4) {
        NC_LOGE("BIOS read out of bounds: offset 0x%x", offset);
        return 0;
    }
    uint32_t b0 = bios->data[offset + 0];
    uint32_t b1 = bios->data[offset + 1];
    uint32_t b2 = bios->data[offset + 2];
    uint32_t b3 = bios->data[offset + 3];
    return b0 | (b1 << 8) | (b2 << 16) | (b3 << 24);
}

// 16-bit read from BIOS
uint16_t nc_bios_load16(NcBios* bios, uint32_t offset) {
    if (offset > NC_BIOS_SIZE - 2) {
        NC_LOGE("BIOS read out of bounds: offset 0x%x", offset);
        return 0;
    }
    uint16_t b0 = bios->data[offset + 0];
    uint16_t b1 = bios->data[offset + 1];
    return b0 | (b1 << 8);
} 