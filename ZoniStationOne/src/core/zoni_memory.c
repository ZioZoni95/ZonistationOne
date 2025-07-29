/**
 * @file zoni_memory.c
 * @brief Memory management implementation for ZoniStationOne
 */

#include "zoni_memory.h"
#include "zoni_hardware.h"
#include "zoni_endian.h"
#include <string.h>

zoni_error_t zoni_memory_init(zoni_memory_t* memory) {
    if (!memory) {
        return ZONI_ERROR_INVALID_PARAMETER;
    }
    
    // Initialize memory structure
    memset(memory, 0, sizeof(zoni_memory_t));
    
    // Allocate main memory regions
    memory->ram = zoni_calloc(PSX_RAM_SIZE, 1);
    if (!memory->ram) {
        zoni_log(ZONI_LOG_ERROR, "Failed to allocate RAM");
        return ZONI_ERROR_OUT_OF_MEMORY;
    }
    
    memory->bios = zoni_calloc(PSX_BIOS_SIZE, 1);
    if (!memory->bios) {
        zoni_log(ZONI_LOG_ERROR, "Failed to allocate BIOS memory");
        zoni_free(memory->ram);
        return ZONI_ERROR_OUT_OF_MEMORY;
    }
    
    memory->scratchpad = zoni_calloc(PSX_SCRATCHPAD_SIZE, 1);
    if (!memory->scratchpad) {
        zoni_log(ZONI_LOG_ERROR, "Failed to allocate scratchpad");
        zoni_free(memory->ram);
        zoni_free(memory->bios);
        return ZONI_ERROR_OUT_OF_MEMORY;
    }
    
    // Initialize memory regions
    zoni_memory_map_region(memory, PSX_MEM_RAM, PSX_RAM_BASE, PSX_RAM_SIZE, 
                          memory->ram, true, true, "RAM");
    zoni_memory_map_region(memory, PSX_MEM_BIOS, PSX_BIOS_BASE, PSX_BIOS_SIZE, 
                          memory->bios, true, false, "BIOS");
    zoni_memory_map_region(memory, PSX_MEM_BIOS_KSEG1, 0xBFC00000, PSX_BIOS_SIZE, 
                          memory->bios, true, false, "BIOS KSEG1");
    zoni_memory_map_region(memory, PSX_MEM_SCRATCHPAD, PSX_SCRATCHPAD_BASE, PSX_SCRATCHPAD_SIZE, 
                          memory->scratchpad, true, true, "Scratchpad");
    
    // Initialize hardware regions
    zoni_memory_map_region(memory, PSX_MEM_HW_REG, PSX_HW_BASE, PSX_HW_END - PSX_HW_BASE + 1, 
                          NULL, true, true, "Hardware Registers");
    zoni_memory_map_region(memory, PSX_MEM_CDROM, PSX_CDROM_BASE, PSX_CDROM_END - PSX_CDROM_BASE + 1, 
                          NULL, true, false, "CDROM Controller");
    zoni_memory_map_region(memory, PSX_MEM_SPU, PSX_SPU_BASE, PSX_SPU_END - PSX_SPU_BASE + 1, 
                          NULL, true, false, "SPU");
    zoni_memory_map_region(memory, PSX_MEM_EXPANSION, PSX_EXPANSION_BASE, PSX_EXPANSION_END - PSX_EXPANSION_BASE + 1, 
                          NULL, true, false, "Expansion");
    zoni_memory_map_region(memory, PSX_MEM_CACHE_CTRL, PSX_CACHE_CTRL_BASE, PSX_CACHE_CTRL_END - PSX_CACHE_CTRL_BASE + 1, 
                          NULL, true, true, "Cache Control");
    
    // Allocate and initialize hardware
    memory->hardware = zoni_calloc(1, sizeof(struct zoni_hardware_s));
    if (!memory->hardware) {
        zoni_log(ZONI_LOG_ERROR, "Failed to allocate hardware structure");
        zoni_memory_shutdown(memory);
        return ZONI_ERROR_OUT_OF_MEMORY;
    }
    
    zoni_error_t hw_result = zoni_hardware_init(memory->hardware);
    if (hw_result != ZONI_SUCCESS) {
        zoni_log(ZONI_LOG_ERROR, "Failed to initialize hardware");
        zoni_memory_shutdown(memory);
        return hw_result;
    }
    
    // Connect hardware to memory system
    memory->hardware->memory = memory;
    
    zoni_log(ZONI_LOG_INFO, "Memory system initialized successfully");
    return ZONI_SUCCESS;
}

void zoni_memory_shutdown(zoni_memory_t* memory) {
    if (!memory) return;
    
    // Shutdown hardware
    if (memory->hardware) {
        zoni_hardware_shutdown(memory->hardware);
        zoni_free(memory->hardware);
    }
    
    // Free allocated memory
    zoni_free(memory->ram);
    zoni_free(memory->bios);
    zoni_free(memory->scratchpad);
    
    // Clear structure
    memset(memory, 0, sizeof(zoni_memory_t));
    
    zoni_log(ZONI_LOG_INFO, "Memory system shutdown");
}

void zoni_memory_reset(zoni_memory_t* memory) {
    if (!memory) return;
    
    // Clear RAM
    if (memory->ram) {
        memset(memory->ram, 0, PSX_RAM_SIZE);
    }
    
    // Clear scratchpad
    if (memory->scratchpad) {
        memset(memory->scratchpad, 0, PSX_SCRATCHPAD_SIZE);
    }
    
    // Reset hardware
    if (memory->hardware) {
        zoni_hardware_reset(memory->hardware);
    }
    
    // Reset statistics
    memory->read_count = 0;
    memory->write_count = 0;
    memory->cache_hits = 0;
    memory->cache_misses = 0;
    
    zoni_log(ZONI_LOG_INFO, "Memory system reset");
}

zoni_error_t zoni_memory_map_region(zoni_memory_t* memory, psx_memory_region_t region,
                                   u32 base_address, u32 size, u8* data,
                                   bool readable, bool writable, const char* name) {
    if (!memory || region >= PSX_MEMORY_REGIONS) {
        return ZONI_ERROR_INVALID_PARAMETER;
    }
    
    zoni_memory_region_t* reg = &memory->regions[region];
    reg->base_address = base_address;
    reg->size = size;
    reg->data = data;
    reg->readable = readable;
    reg->writable = writable;
    reg->name = name ? name : "Unknown";
    
    zoni_log(ZONI_LOG_DEBUG, "Mapped region %s: 0x%08X-0x%08X (%u bytes)", 
             reg->name, base_address, base_address + size - 1, size);
    
    return ZONI_SUCCESS;
}

zoni_error_t zoni_memory_unmap_region(zoni_memory_t* memory, psx_memory_region_t region) {
    if (!memory || region >= PSX_MEMORY_REGIONS) {
        return ZONI_ERROR_INVALID_PARAMETER;
    }
    
    zoni_memory_region_t* reg = &memory->regions[region];
    reg->base_address = 0;
    reg->size = 0;
    reg->data = NULL;
    reg->readable = false;
    reg->writable = false;
    reg->name = "Unmapped";
    
    zoni_log(ZONI_LOG_DEBUG, "Unmapped region %d", region);
    
    return ZONI_SUCCESS;
}

static const zoni_memory_region_t* zoni_memory_find_region(zoni_memory_t* memory, u32 address) {
    for (int i = 0; i < PSX_MEMORY_REGIONS; i++) {
        const zoni_memory_region_t* reg = &memory->regions[i];
        if (address >= reg->base_address && address < reg->base_address + reg->size) {
            return reg;
        }
    }
    return NULL;
}

zoni_error_t zoni_memory_read8(zoni_memory_t* memory, u32 address, u8* value) {
    if (!memory || !value) {
        return ZONI_ERROR_INVALID_PARAMETER;
    }
    
    const zoni_memory_region_t* reg = zoni_memory_find_region(memory, address);
    if (!reg || !reg->readable) {
        zoni_log(ZONI_LOG_WARNING, "Invalid read8 at address 0x%08X", address);
        *value = 0;
        return ZONI_ERROR_INVALID_PARAMETER;
    }
    
    if (reg->data) {
        u32 offset = address - reg->base_address;
        *value = reg->data[offset];
    } else {
        // Hardware register read - use hardware module
        *value = zoni_hw_read8(memory->hardware, address);
    }
    
    memory->read_count++;
    return ZONI_SUCCESS;
}

zoni_error_t zoni_memory_read16(zoni_memory_t* memory, u32 address, u16* value) {
    if (!memory || !value) {
        return ZONI_ERROR_INVALID_PARAMETER;
    }
    
    // Check alignment
    if (address & 1) {
        zoni_log(ZONI_LOG_WARNING, "Unaligned read16 at address 0x%08X", address);
        *value = 0;
        return ZONI_ERROR_INVALID_PARAMETER;
    }
    
    const zoni_memory_region_t* reg = zoni_memory_find_region(memory, address);
    if (!reg || !reg->readable) {
        zoni_log(ZONI_LOG_WARNING, "Invalid read16 at address 0x%08X", address);
        *value = 0;
        return ZONI_ERROR_INVALID_PARAMETER;
    }
    
    if (reg->data) {
        u32 offset = address - reg->base_address;
        // MIPS R3000A uses little-endian byte order
        *value = zoni_read_le16(&reg->data[offset]);
    } else {
        // Hardware register read - use hardware module
        *value = zoni_hw_read16(memory->hardware, address);
    }
    
    memory->read_count++;
    return ZONI_SUCCESS;
}

zoni_error_t zoni_memory_read32(zoni_memory_t* memory, u32 address, u32* value) {
    if (!memory || !value) {
        return ZONI_ERROR_INVALID_PARAMETER;
    }
    
    // Check alignment
    if (address & 3) {
        zoni_log(ZONI_LOG_WARNING, "Unaligned read32 at address 0x%08X", address);
        *value = 0;
        return ZONI_ERROR_INVALID_PARAMETER;
    }
    
    const zoni_memory_region_t* reg = zoni_memory_find_region(memory, address);
    if (!reg || !reg->readable) {
        zoni_log(ZONI_LOG_WARNING, "Invalid read32 at address 0x%08X", address);
        *value = 0;
        return ZONI_ERROR_INVALID_PARAMETER;
    }
    
    if (reg->data) {
        u32 offset = address - reg->base_address;
        // MIPS R3000A uses little-endian byte order
        *value = zoni_read_le32(&reg->data[offset]);
    } else {
        // Hardware register read - use hardware module
        *value = zoni_hw_read32(memory->hardware, address);
    }
    
    memory->read_count++;
    return ZONI_SUCCESS;
}

zoni_error_t zoni_memory_write8(zoni_memory_t* memory, u32 address, u8 value) {
    if (!memory) {
        return ZONI_ERROR_INVALID_PARAMETER;
    }
    
    const zoni_memory_region_t* reg = zoni_memory_find_region(memory, address);
    if (!reg || !reg->writable) {
        #ifdef ZONI_DEBUG
        zoni_log(ZONI_LOG_WARNING, "Invalid write8 at address 0x%08X", address);
        #endif
        return ZONI_ERROR_INVALID_PARAMETER;
    }
    
    if (reg->data) {
        u32 offset = address - reg->base_address;
        reg->data[offset] = value;
    } else {
        // Hardware register write - use hardware module
        zoni_error_t hw_result = zoni_hw_write8(memory->hardware, address, value);
        if (hw_result != ZONI_SUCCESS) {
            #ifdef ZONI_DEBUG
            zoni_log(ZONI_LOG_WARNING, "Hardware write8 failed: 0x%08X = 0x%02X", address, value);
            #endif
            return hw_result;
        }
    }
    
    memory->write_count++;
    return ZONI_SUCCESS;
}

zoni_error_t zoni_memory_write16(zoni_memory_t* memory, u32 address, u16 value) {
    if (!memory) {
        return ZONI_ERROR_INVALID_PARAMETER;
    }
    
    // Check alignment
    if (address & 1) {
        zoni_log(ZONI_LOG_WARNING, "Unaligned write16 at address 0x%08X", address);
        return ZONI_ERROR_INVALID_PARAMETER;
    }
    
    const zoni_memory_region_t* reg = zoni_memory_find_region(memory, address);
    if (!reg || !reg->writable) {
        #ifdef ZONI_DEBUG
        zoni_log(ZONI_LOG_WARNING, "Invalid write16 at address 0x%08X", address);
        #endif
        return ZONI_ERROR_INVALID_PARAMETER;
    }
    
    if (reg->data) {
        u32 offset = address - reg->base_address;
        // MIPS R3000A uses little-endian byte order
        zoni_write_le16(&reg->data[offset], value);
    } else {
        // Hardware register write - use hardware module
        zoni_error_t hw_result = zoni_hw_write16(memory->hardware, address, value);
        if (hw_result != ZONI_SUCCESS) {
            #ifdef ZONI_DEBUG
            zoni_log(ZONI_LOG_WARNING, "Hardware write16 failed: 0x%08X = 0x%04X", address, value);
            #endif
            return hw_result;
        }
    }
    
    memory->write_count++;
    return ZONI_SUCCESS;
}

zoni_error_t zoni_memory_write32(zoni_memory_t* memory, u32 address, u32 value) {
    if (!memory) {
        return ZONI_ERROR_INVALID_PARAMETER;
    }
    
    // Check alignment
    if (address & 3) {
        zoni_log(ZONI_LOG_WARNING, "Unaligned write32 at address 0x%08X", address);
        return ZONI_ERROR_INVALID_PARAMETER;
    }
    
    const zoni_memory_region_t* reg = zoni_memory_find_region(memory, address);
    if (!reg || !reg->writable) {
        #ifdef ZONI_DEBUG
        zoni_log(ZONI_LOG_WARNING, "Invalid write32 at address 0x%08X", address);
        #endif
        return ZONI_ERROR_INVALID_PARAMETER;
    }
    
    if (reg->data) {
        u32 offset = address - reg->base_address;
        // MIPS R3000A uses little-endian byte order
        zoni_write_le32(&reg->data[offset], value);
    } else {
        // Hardware register write - use hardware module
        zoni_error_t hw_result = zoni_hw_write32(memory->hardware, address, value);
        if (hw_result != ZONI_SUCCESS) {
            #ifdef ZONI_DEBUG
            zoni_log(ZONI_LOG_WARNING, "Hardware write32 failed: 0x%08X = 0x%08X", address, value);
            #endif
            return hw_result;
        }
    }
    
    memory->write_count++;
    return ZONI_SUCCESS;
}

zoni_error_t zoni_memory_read8s(zoni_memory_t* memory, u32 address, s8* value) {
    u8 temp;
    zoni_error_t result = zoni_memory_read8(memory, address, &temp);
    if (result == ZONI_SUCCESS) {
        *value = (s8)temp;
    }
    return result;
}

zoni_error_t zoni_memory_read16s(zoni_memory_t* memory, u32 address, s16* value) {
    u16 temp;
    zoni_error_t result = zoni_memory_read16(memory, address, &temp);
    if (result == ZONI_SUCCESS) {
        *value = (s16)temp;
    }
    return result;
}

zoni_error_t zoni_memory_read_block(zoni_memory_t* memory, u32 address, void* buffer, u32 size) {
    if (!memory || !buffer) {
        return ZONI_ERROR_INVALID_PARAMETER;
    }
    
    u8* buf = (u8*)buffer;
    for (u32 i = 0; i < size; i++) {
        u8 value;
        zoni_error_t result = zoni_memory_read8(memory, address + i, &value);
        if (result != ZONI_SUCCESS) {
            return result;
        }
        buf[i] = value;
    }
    
    return ZONI_SUCCESS;
}

zoni_error_t zoni_memory_write_block(zoni_memory_t* memory, u32 address, const void* buffer, u32 size) {
    if (!memory || !buffer) {
        return ZONI_ERROR_INVALID_PARAMETER;
    }
    
    const u8* buf = (const u8*)buffer;
    for (u32 i = 0; i < size; i++) {
        zoni_error_t result = zoni_memory_write8(memory, address + i, buf[i]);
        if (result != ZONI_SUCCESS) {
            return result;
        }
    }
    
    return ZONI_SUCCESS;
}

bool zoni_memory_is_valid_address(zoni_memory_t* memory, u32 address) {
    return zoni_memory_find_region(memory, address) != NULL;
}

u32 zoni_memory_get_region_index(zoni_memory_t* memory, u32 address) {
    for (u32 i = 0; i < PSX_MEMORY_REGIONS; i++) {
        const zoni_memory_region_t* reg = &memory->regions[i];
        if (address >= reg->base_address && address < reg->base_address + reg->size) {
            return i;
        }
    }
    return PSX_MEMORY_REGIONS; // Invalid region
}

const zoni_memory_region_t* zoni_memory_get_region(zoni_memory_t* memory, u32 address) {
    return zoni_memory_find_region(memory, address);
}

void zoni_memory_dump_region(zoni_memory_t* memory, psx_memory_region_t region, u32 offset, u32 size) {
    if (!memory || region >= PSX_MEMORY_REGIONS) {
        return;
    }
    
    const zoni_memory_region_t* reg = &memory->regions[region];
    if (!reg->data || offset >= reg->size) {
        return;
    }
    
    u32 end_offset = offset + size;
    if (end_offset > reg->size) {
        end_offset = reg->size;
    }
    
    zoni_log(ZONI_LOG_INFO, "Memory dump of %s (0x%08X-0x%08X):", 
             reg->name, reg->base_address + offset, reg->base_address + end_offset - 1);
    
    for (u32 i = offset; i < end_offset; i += 16) {
        printf("0x%08X: ", reg->base_address + i);
        
        // Hex dump
        for (u32 j = 0; j < 16 && i + j < end_offset; j++) {
            printf("%02X ", reg->data[i + j]);
        }
        
        // ASCII dump
        printf("  ");
        for (u32 j = 0; j < 16 && i + j < end_offset; j++) {
            u8 c = reg->data[i + j];
            printf("%c", (c >= 32 && c <= 126) ? c : '.');
        }
        printf("\n");
    }
}

void zoni_memory_dump_stats(zoni_memory_t* memory) {
    if (!memory) return;
    
    zoni_log(ZONI_LOG_INFO, "Memory statistics:");
    zoni_log(ZONI_LOG_INFO, "  Reads: %u", memory->read_count);
    zoni_log(ZONI_LOG_INFO, "  Writes: %u", memory->write_count);
    zoni_log(ZONI_LOG_INFO, "  Cache hits: %u", memory->cache_hits);
    zoni_log(ZONI_LOG_INFO, "  Cache misses: %u", memory->cache_misses);
}

void zoni_memory_validate_address(zoni_memory_t* memory, u32 address, zoni_mem_access_t access) {
    if (!memory) return;
    
    const zoni_memory_region_t* reg = zoni_memory_find_region(memory, address);
    if (!reg) {
        zoni_log(ZONI_LOG_ERROR, "Invalid memory access at address 0x%08X", address);
        return;
    }
    
    if (access == ZONI_MEM_ACCESS_READ && !reg->readable) {
        zoni_log(ZONI_LOG_ERROR, "Read access to non-readable region at address 0x%08X", address);
    } else if (access == ZONI_MEM_ACCESS_WRITE && !reg->writable) {
        zoni_log(ZONI_LOG_ERROR, "Write access to non-writable region at address 0x%08X", address);
    }
} 