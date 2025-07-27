#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zonistation_common.h"
#include "zonistation_memory.h"

// Memory region structure for mapping
typedef struct {
    zs_u32 start_address;
    zs_u32 end_address;
    zs_u8* data;
    zs_size_t size;
    zs_bool read_only;
    zs_map_tag_t tag;
} zs_memory_region_t;

// Memory mapping functions (migrated from PCSX-ReARMed)
static void* zs_memory_map_default(zs_u32 addr, zs_size_t size, zs_map_tag_t tag, zs_bool* can_retry_addr) {
    ZS_UNUSED(addr);
    ZS_UNUSED(tag);
    
    *can_retry_addr = ZS_FALSE;
    void* ptr = calloc(1, size);
    return ptr ? ptr : (void*)-1;
}

static void zs_memory_unmap_default(void* ptr, zs_size_t size, zs_map_tag_t tag) {
    ZS_UNUSED(size);
    ZS_UNUSED(tag);
    free(ptr);
}

// Hook functions for memory mapping (can be overridden)
static void* (*zs_memory_map_hook)(zs_u32 addr, zs_size_t size, zs_map_tag_t tag, zs_bool* can_retry_addr) = zs_memory_map_default;
static void (*zs_memory_unmap_hook)(void* ptr, zs_size_t size, zs_map_tag_t tag) = zs_memory_unmap_default;

// Memory mapping function
void* zs_memory_map(zs_u32 addr, zs_size_t size, zs_bool is_fixed, zs_map_tag_t tag) {
    ZS_UNUSED(is_fixed);
    zs_bool can_retry_addr = ZS_FALSE;
    void* ret = (void*)-1;
    
    for (int try_ = 0; try_ < 3; try_++) {
        if (ret != (void*)-1) {
            zs_memory_unmap(ret, size, tag);
        }
        ret = zs_memory_map_hook(addr, size, tag, &can_retry_addr);
        if (ret == (void*)-1) {
            if (!can_retry_addr) {
                break;
            }
            // Try with different address alignment
            addr = (addr + 0x1000) & ~0xFFF;
        } else {
            break;
        }
    }
    
    return ret;
}

void zs_memory_unmap(void* ptr, zs_size_t size, zs_map_tag_t tag) {
    if (ptr != (void*)-1 && ptr != NULL) {
        zs_memory_unmap_hook(ptr, size, tag);
    }
}

// Address translation functions (migrated from PCSX-ReARMed)
static void* zs_memory_translate_address(zs_memory_t* memory, zs_u32 address, zs_bool is_write) {
    if (memory == NULL || !memory->initialized) {
        return NULL;
    }
    
    // RAM region (0x00000000 - 0x01FFFFFF)
    if (address >= ZS_PSX_RAM_BASE && address < ZS_PSX_RAM_BASE + memory->ram_size) {
        zs_u32 offset = address - ZS_PSX_RAM_BASE;
        return memory->ram + offset;
    }
    
    // BIOS region (0x1FC00000 - 0x1FFFFFFF) - read-only
    if (address >= ZS_PSX_BIOS_BASE && address < ZS_PSX_BIOS_BASE + memory->bios_size) {
        if (is_write) {
            return NULL; // BIOS is read-only
        }
        zs_u32 offset = address - ZS_PSX_BIOS_BASE;
        return memory->bios + offset;
    }
    
    // Scratchpad region (0x1F800000 - 0x1F8003FF)
    if (address >= ZS_PSX_SCRATCHPAD_BASE && address < ZS_PSX_SCRATCHPAD_BASE + memory->scratchpad_size) {
        zs_u32 offset = address - ZS_PSX_SCRATCHPAD_BASE;
        return memory->scratchpad + offset;
    }
    
    // Hardware registers region (0x1F801000 - 0x1F801FFF)
    if (address >= ZS_PSX_HARDWARE_REG_BASE && address < ZS_PSX_HARDWARE_REG_BASE + memory->hardware_regs_size) {
        zs_u32 offset = address - ZS_PSX_HARDWARE_REG_BASE;
        return memory->hardware_regs + offset;
    }
    
    // Memory mirroring (RAM mirrors every 2MB)
    if ((address & 0x1FFFFF) < memory->ram_size) {
        return memory->ram + (address & 0x1FFFFF);
    }
    
    return NULL;
}

// Enhanced memory mapping functions
zs_error_t zs_memory_map_region(zs_memory_t* memory, zs_u32 address, zs_size_t size, zs_u8* data) {
    if (memory == NULL || !memory->initialized || data == NULL) {
        return ZS_ERROR_INVALID_PARAMETER;
    }
    
    // This is a simplified implementation - in a full emulator, you'd want
    // to maintain a list of mapped regions and handle overlapping
    ZS_LOG_INFO("Mapping region at 0x%08X, size: %zu", address, size);
    
    // For now, we'll just validate the address range
    if (address + size < address) {
        return ZS_ERROR_INVALID_PARAMETER;
    }
    
    return ZS_SUCCESS;
}

zs_error_t zs_memory_unmap_region(zs_memory_t* memory, zs_u32 address) {
    if (memory == NULL || !memory->initialized) {
        return ZS_ERROR_INVALID_PARAMETER;
    }
    
    ZS_LOG_INFO("Unmapping region at 0x%08X", address);
    return ZS_SUCCESS;
}

// Fast memory access functions (migrated from PCSX-ReARMed)
zs_u8 zs_memory_read_byte_fast(zs_memory_t* memory, zs_u32 address) {
    void* ptr = zs_memory_translate_address(memory, address, ZS_FALSE);
    if (ptr == NULL) {
        ZS_LOG_WARN("Invalid memory read at address 0x%08X", address);
        return 0;
    }
    return *(zs_u8*)ptr;
}

zs_u16 zs_memory_read_halfword_fast(zs_memory_t* memory, zs_u32 address) {
    void* ptr = zs_memory_translate_address(memory, address, ZS_FALSE);
    if (ptr == NULL) {
        ZS_LOG_WARN("Invalid memory read at address 0x%08X", address);
        return 0;
    }
    // Handle alignment
    if (address & 1) {
        zs_u16 value = 0;
        zs_memory_read(memory, address, (zs_u8*)&value, 2);
        return value;
    }
    return *(zs_u16*)ptr;
}

zs_u32 zs_memory_read_word_fast(zs_memory_t* memory, zs_u32 address) {
    void* ptr = zs_memory_translate_address(memory, address, ZS_FALSE);
    if (ptr == NULL) {
        ZS_LOG_WARN("Invalid memory read at address 0x%08X", address);
        return 0;
    }
    // Handle alignment
    if (address & 3) {
        zs_u32 value = 0;
        zs_memory_read(memory, address, (zs_u8*)&value, 4);
        return value;
    }
    return *(zs_u32*)ptr;
}

zs_error_t zs_memory_write_byte_fast(zs_memory_t* memory, zs_u32 address, zs_u8 value) {
    void* ptr = zs_memory_translate_address(memory, address, ZS_TRUE);
    if (ptr == NULL) {
        ZS_LOG_WARN("Invalid memory write at address 0x%08X", address);
        return ZS_ERROR_INVALID_PARAMETER;
    }
    *(zs_u8*)ptr = value;
    return ZS_SUCCESS;
}

zs_error_t zs_memory_write_halfword_fast(zs_memory_t* memory, zs_u32 address, zs_u16 value) {
    void* ptr = zs_memory_translate_address(memory, address, ZS_TRUE);
    if (ptr == NULL) {
        ZS_LOG_WARN("Invalid memory write at address 0x%08X", address);
        return ZS_ERROR_INVALID_PARAMETER;
    }
    // Handle alignment
    if (address & 1) {
        return zs_memory_write(memory, address, (zs_u8*)&value, 2);
    }
    *(zs_u16*)ptr = value;
    return ZS_SUCCESS;
}

zs_error_t zs_memory_write_word_fast(zs_memory_t* memory, zs_u32 address, zs_u32 value) {
    void* ptr = zs_memory_translate_address(memory, address, ZS_TRUE);
    if (ptr == NULL) {
        ZS_LOG_WARN("Invalid memory write at address 0x%08X", address);
        return ZS_ERROR_INVALID_PARAMETER;
    }
    // Handle alignment
    if (address & 3) {
        return zs_memory_write(memory, address, (zs_u8*)&value, 4);
    }
    *(zs_u32*)ptr = value;
    return ZS_SUCCESS;
}

// Memory pointer function (migrated from PCSX-ReARMed)
void* zs_memory_get_pointer(zs_memory_t* memory, zs_u32 address) {
    return zs_memory_translate_address(memory, address, ZS_FALSE);
}

// Set memory mapping hooks
void zs_memory_set_map_hooks(
    void* (*map_hook)(zs_u32 addr, zs_size_t size, zs_map_tag_t tag, zs_bool* can_retry_addr),
    void (*unmap_hook)(void* ptr, zs_size_t size, zs_map_tag_t tag)
) {
    if (map_hook != NULL) {
        zs_memory_map_hook = map_hook;
    }
    if (unmap_hook != NULL) {
        zs_memory_unmap_hook = unmap_hook;
    }
} 