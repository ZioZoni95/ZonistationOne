#include "../include/vram.h"
#include "../include/log.h"
#include <string.h>

// Initialize VRAM with zeros
void nc_vram_init(NcVram* vram) {
    NC_LOGI("Initializing VRAM");
    memset(vram->data, 0x00, NC_VRAM_SIZE);
    NC_LOGD("VRAM filled with 0x00 (%d bytes)", NC_VRAM_SIZE);
}

// Helper for bounds checking
static inline int nc_vram_oob(uint32_t offset, uint32_t size) {
    return offset > NC_VRAM_SIZE - size;
}

// 32-bit read
uint32_t nc_vram_load32(NcVram* vram, uint32_t offset) {
    if (offset % 4 != 0) {
        NC_LOGW("VRAM Load32 unaligned: offset 0x%x", offset);
    }
    if (nc_vram_oob(offset, 4)) {
        NC_LOGE("VRAM Load32 out of bounds: offset 0x%x", offset);
        return 0;
    }
    uint32_t b0 = vram->data[offset + 0];
    uint32_t b1 = vram->data[offset + 1];
    uint32_t b2 = vram->data[offset + 2];
    uint32_t b3 = vram->data[offset + 3];
    return b0 | (b1 << 8) | (b2 << 16) | (b3 << 24);
}

// 32-bit write
void nc_vram_store32(NcVram* vram, uint32_t offset, uint32_t value) {
    if (offset % 4 != 0) {
        NC_LOGW("VRAM Store32 unaligned: offset 0x%x", offset);
    }
    if (nc_vram_oob(offset, 4)) {
        NC_LOGE("VRAM Store32 out of bounds: offset 0x%x", offset);
        return;
    }
    vram->data[offset + 0] = (uint8_t)(value & 0xFF);
    vram->data[offset + 1] = (uint8_t)((value >> 8) & 0xFF);
    vram->data[offset + 2] = (uint8_t)((value >> 16) & 0xFF);
    vram->data[offset + 3] = (uint8_t)((value >> 24) & 0xFF);
}

// 16-bit read
uint16_t nc_vram_load16(NcVram* vram, uint32_t offset) {
    if (offset % 2 != 0) {
        NC_LOGW("VRAM Load16 unaligned: offset 0x%x", offset);
    }
    if (nc_vram_oob(offset, 2)) {
        NC_LOGE("VRAM Load16 out of bounds: offset 0x%x", offset);
        return 0;
    }
    uint16_t b0 = vram->data[offset + 0];
    uint16_t b1 = vram->data[offset + 1];
    return b0 | (b1 << 8);
}

// 16-bit write
void nc_vram_store16(NcVram* vram, uint32_t offset, uint16_t value) {
    if (offset % 2 != 0) {
        NC_LOGW("VRAM Store16 unaligned: offset 0x%x", offset);
    }
    if (nc_vram_oob(offset, 2)) {
        NC_LOGE("VRAM Store16 out of bounds: offset 0x%x", offset);
        return;
    }
    vram->data[offset + 0] = (uint8_t)(value & 0xFF);
    vram->data[offset + 1] = (uint8_t)((value >> 8) & 0xFF);
}

// 8-bit read
uint8_t nc_vram_load8(NcVram* vram, uint32_t offset) {
    if (nc_vram_oob(offset, 1)) {
        NC_LOGE("VRAM Load8 out of bounds: offset 0x%x", offset);
        return 0;
    }
    return vram->data[offset];
}

// 8-bit write
void nc_vram_store8(NcVram* vram, uint32_t offset, uint8_t value) {
    if (nc_vram_oob(offset, 1)) {
        NC_LOGE("VRAM Store8 out of bounds: offset 0x%x", offset);
        return;
    }
    vram->data[offset] = value;
} 