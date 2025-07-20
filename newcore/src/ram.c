#include "../include/ram.h"
#include "../include/log.h"
#include <string.h>

// Initialize RAM with a known pattern (0xCA)
void nc_ram_init(NcRam* ram) {
    NC_LOGI("Initializing RAM");
    memset(ram->data, 0xCA, NC_RAM_SIZE);
    NC_LOGD("RAM filled with 0xCA (%d bytes)", NC_RAM_SIZE);
}

// Helper for bounds checking
static inline int nc_ram_oob(uint32_t offset, uint32_t size) {
    if (offset + size > NC_RAM_SIZE) {
        NC_LOGW("RAM out-of-bounds: offset=0x%08x, size=%u", offset, size);
        return 1;
    }
    return 0;
}

// 32-bit read
uint32_t nc_ram_load32(NcRam* ram, uint32_t offset) {
    if (nc_ram_oob(offset, 4)) return 0;
    uint32_t b0 = ram->data[offset + 0];
    uint32_t b1 = ram->data[offset + 1];
    uint32_t b2 = ram->data[offset + 2];
    uint32_t b3 = ram->data[offset + 3];
    return b0 | (b1 << 8) | (b2 << 16) | (b3 << 24);
}

// 32-bit write
void nc_ram_store32(NcRam* ram, uint32_t offset, uint32_t value) {
    if (nc_ram_oob(offset, 4)) return;
    ram->data[offset + 0] = (uint8_t)(value & 0xFF);
    ram->data[offset + 1] = (uint8_t)((value >> 8) & 0xFF);
    ram->data[offset + 2] = (uint8_t)((value >> 16) & 0xFF);
    ram->data[offset + 3] = (uint8_t)((value >> 24) & 0xFF);
}

// 16-bit read
uint16_t nc_ram_load16(NcRam* ram, uint32_t offset) {
    if (nc_ram_oob(offset, 2)) return 0;
    uint16_t b0 = ram->data[offset + 0];
    uint16_t b1 = ram->data[offset + 1];
    return b0 | (b1 << 8);
}

// 16-bit write
void nc_ram_store16(NcRam* ram, uint32_t offset, uint16_t value) {
    if (nc_ram_oob(offset, 2)) return;
    ram->data[offset + 0] = (uint8_t)(value & 0xFF);
    ram->data[offset + 1] = (uint8_t)((value >> 8) & 0xFF);
}

// 8-bit read
uint8_t nc_ram_load8(NcRam* ram, uint32_t offset) {
    if (nc_ram_oob(offset, 1)) return 0;
    return ram->data[offset];
}

// 8-bit write
void nc_ram_store8(NcRam* ram, uint32_t offset, uint8_t value) {
    if (nc_ram_oob(offset, 1)) return;
    ram->data[offset] = value;
} 