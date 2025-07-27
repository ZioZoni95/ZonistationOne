#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zonistation_common.h"
#include "zonistation_memory.h"

// Memory initialization
zs_error_t zs_memory_init(zs_memory_t** memory_ptr) {
    ZS_ASSERT(memory_ptr != NULL);
    
    zs_memory_t* memory = (zs_memory_t*)malloc(sizeof(zs_memory_t));
    if (memory == NULL) {
        ZS_LOG_ERROR("Failed to allocate memory structure");
        return ZS_ERROR_OUT_OF_MEMORY;
    }
    
    // Initialize memory structure
    memset(memory, 0, sizeof(zs_memory_t));
    
    // Allocate memory regions
    memory->ram_size = ZS_PSX_RAM_SIZE;
    memory->bios_size = ZS_PSX_BIOS_SIZE;
    memory->scratchpad_size = ZS_PSX_SCRATCHPAD_SIZE;
    memory->hardware_regs_size = ZS_PSX_HARDWARE_REG_SIZE;
    
    // Allocate RAM
    memory->ram = (zs_u8*)calloc(memory->ram_size, 1);
    if (memory->ram == NULL) {
        ZS_LOG_ERROR("Failed to allocate RAM");
        free(memory);
        return ZS_ERROR_OUT_OF_MEMORY;
    }
    
    // Allocate BIOS
    memory->bios = (zs_u8*)calloc(memory->bios_size, 1);
    if (memory->bios == NULL) {
        ZS_LOG_ERROR("Failed to allocate BIOS memory");
        free(memory->ram);
        free(memory);
        return ZS_ERROR_OUT_OF_MEMORY;
    }
    
    // Allocate scratchpad
    memory->scratchpad = (zs_u8*)calloc(memory->scratchpad_size, 1);
    if (memory->scratchpad == NULL) {
        ZS_LOG_ERROR("Failed to allocate scratchpad");
        free(memory->bios);
        free(memory->ram);
        free(memory);
        return ZS_ERROR_OUT_OF_MEMORY;
    }
    
    // Allocate hardware registers
    memory->hardware_regs = (zs_u8*)calloc(memory->hardware_regs_size, 1);
    if (memory->hardware_regs == NULL) {
        ZS_LOG_ERROR("Failed to allocate hardware registers");
        free(memory->scratchpad);
        free(memory->bios);
        free(memory->ram);
        free(memory);
        return ZS_ERROR_OUT_OF_MEMORY;
    }
    
    memory->initialized = ZS_TRUE;
    *memory_ptr = memory;
    
    ZS_LOG_INFO("Memory initialized successfully");
    return ZS_SUCCESS;
}

zs_error_t zs_memory_shutdown(zs_memory_t* memory) {
    if (memory == NULL) {
        return ZS_ERROR_INVALID_PARAMETER;
    }
    
    ZS_LOG_INFO("Shutting down memory...");
    
    if (memory->ram) {
        free(memory->ram);
        memory->ram = NULL;
    }
    
    if (memory->bios) {
        free(memory->bios);
        memory->bios = NULL;
    }
    
    if (memory->scratchpad) {
        free(memory->scratchpad);
        memory->scratchpad = NULL;
    }
    
    if (memory->hardware_regs) {
        free(memory->hardware_regs);
        memory->hardware_regs = NULL;
    }
    
    free(memory);
    ZS_LOG_INFO("Memory shutdown complete");
    return ZS_SUCCESS;
}

zs_error_t zs_memory_reset(zs_memory_t* memory) {
    if (memory == NULL || !memory->initialized) {
        return ZS_ERROR_INVALID_PARAMETER;
    }
    
    ZS_LOG_INFO("Resetting memory...");
    
    // Clear RAM
    memset(memory->ram, 0, memory->ram_size);
    
    // Clear scratchpad
    memset(memory->scratchpad, 0, memory->scratchpad_size);
    
    // Clear hardware registers
    memset(memory->hardware_regs, 0, memory->hardware_regs_size);
    
    // Note: BIOS is not cleared on reset
    
    ZS_LOG_INFO("Memory reset complete");
    return ZS_SUCCESS;
}

// Memory access functions
zs_error_t zs_memory_read(zs_memory_t* memory, zs_u32 address, zs_u8* data, zs_size_t size) {
    if (memory == NULL || !memory->initialized || data == NULL) {
        return ZS_ERROR_INVALID_PARAMETER;
    }
    
    // Check for overflow
    if (address + size < address) {
        return ZS_ERROR_INVALID_PARAMETER;
    }
    
    // Map address to memory region
    if (address >= ZS_PSX_RAM_BASE && address < ZS_PSX_RAM_BASE + memory->ram_size) {
        // RAM access
        zs_u32 offset = address - ZS_PSX_RAM_BASE;
        if (offset + size <= memory->ram_size) {
            memcpy(data, memory->ram + offset, size);
            return ZS_SUCCESS;
        }
    }
    else if (address >= ZS_PSX_BIOS_BASE && address < ZS_PSX_BIOS_BASE + memory->bios_size) {
        // BIOS access
        zs_u32 offset = address - ZS_PSX_BIOS_BASE;
        if (offset + size <= memory->bios_size) {
            memcpy(data, memory->bios + offset, size);
            return ZS_SUCCESS;
        }
    }
    else if (address >= ZS_PSX_SCRATCHPAD_BASE && address < ZS_PSX_SCRATCHPAD_BASE + memory->scratchpad_size) {
        // Scratchpad access
        zs_u32 offset = address - ZS_PSX_SCRATCHPAD_BASE;
        if (offset + size <= memory->scratchpad_size) {
            memcpy(data, memory->scratchpad + offset, size);
            return ZS_SUCCESS;
        }
    }
    else if (address >= ZS_PSX_HARDWARE_REG_BASE && address < ZS_PSX_HARDWARE_REG_BASE + memory->hardware_regs_size) {
        // Hardware registers access
        zs_u32 offset = address - ZS_PSX_HARDWARE_REG_BASE;
        if (offset + size <= memory->hardware_regs_size) {
            memcpy(data, memory->hardware_regs + offset, size);
            return ZS_SUCCESS;
        }
    }
    
    // Invalid address
    ZS_LOG_WARN("Invalid memory read at address 0x%08X", address);
    return ZS_ERROR_INVALID_PARAMETER;
}

zs_error_t zs_memory_write(zs_memory_t* memory, zs_u32 address, const zs_u8* data, zs_size_t size) {
    if (memory == NULL || !memory->initialized || data == NULL) {
        return ZS_ERROR_INVALID_PARAMETER;
    }
    
    // Check for overflow
    if (address + size < address) {
        return ZS_ERROR_INVALID_PARAMETER;
    }
    
    // Map address to memory region
    if (address >= ZS_PSX_RAM_BASE && address < ZS_PSX_RAM_BASE + memory->ram_size) {
        // RAM access
        zs_u32 offset = address - ZS_PSX_RAM_BASE;
        if (offset + size <= memory->ram_size) {
            memcpy(memory->ram + offset, data, size);
            return ZS_SUCCESS;
        }
    }
    else if (address >= ZS_PSX_SCRATCHPAD_BASE && address < ZS_PSX_SCRATCHPAD_BASE + memory->scratchpad_size) {
        // Scratchpad access
        zs_u32 offset = address - ZS_PSX_SCRATCHPAD_BASE;
        if (offset + size <= memory->scratchpad_size) {
            memcpy(memory->scratchpad + offset, data, size);
            return ZS_SUCCESS;
        }
    }
    else if (address >= ZS_PSX_HARDWARE_REG_BASE && address < ZS_PSX_HARDWARE_REG_BASE + memory->hardware_regs_size) {
        // Hardware registers access
        zs_u32 offset = address - ZS_PSX_HARDWARE_REG_BASE;
        if (offset + size <= memory->hardware_regs_size) {
            memcpy(memory->hardware_regs + offset, data, size);
            return ZS_SUCCESS;
        }
    }
    
    // Note: BIOS is read-only, so we don't handle writes to it
    
    // Invalid address
    ZS_LOG_WARN("Invalid memory write at address 0x%08X", address);
    return ZS_ERROR_INVALID_PARAMETER;
}

// Convenience functions
zs_u8 zs_memory_read_byte(zs_memory_t* memory, zs_u32 address) {
    zs_u8 value = 0;
    zs_memory_read(memory, address, &value, 1);
    return value;
}

zs_u16 zs_memory_read_halfword(zs_memory_t* memory, zs_u32 address) {
    zs_u8 data[2];
    if (zs_memory_read(memory, address, data, 2) == ZS_SUCCESS) {
        return (data[0] << 8) | data[1]; // Big-endian
    }
    return 0;
}

zs_u32 zs_memory_read_word(zs_memory_t* memory, zs_u32 address) {
    zs_u8 data[4];
    if (zs_memory_read(memory, address, data, 4) == ZS_SUCCESS) {
        return (data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3]; // Big-endian
    }
    return 0;
}

zs_error_t zs_memory_write_byte(zs_memory_t* memory, zs_u32 address, zs_u8 value) {
    return zs_memory_write(memory, address, &value, 1);
}

zs_error_t zs_memory_write_halfword(zs_memory_t* memory, zs_u32 address, zs_u16 value) {
    zs_u8 data[2] = {(zs_u8)(value >> 8), (zs_u8)(value & 0xFF)}; // Big-endian
    return zs_memory_write(memory, address, data, 2);
}

zs_error_t zs_memory_write_word(zs_memory_t* memory, zs_u32 address, zs_u32 value) {
    zs_u8 data[4] = {
        (zs_u8)(value >> 24),
        (zs_u8)((value >> 16) & 0xFF),
        (zs_u8)((value >> 8) & 0xFF),
        (zs_u8)(value & 0xFF)
    }; // Big-endian
    return zs_memory_write(memory, address, data, 4);
}

// BIOS loading
zs_error_t zs_memory_load_bios(zs_memory_t* memory, const char* filename) {
    if (memory == NULL || !memory->initialized || filename == NULL) {
        return ZS_ERROR_INVALID_PARAMETER;
    }
    
    FILE* file = fopen(filename, "rb");
    if (file == NULL) {
        ZS_LOG_ERROR("Failed to open BIOS file: %s", filename);
        return ZS_ERROR_FILE_NOT_FOUND;
    }
    
    // Get file size
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    if (file_size > (long)memory->bios_size) {
        ZS_LOG_ERROR("BIOS file too large: %ld bytes (max %zu)", file_size, memory->bios_size);
        fclose(file);
        return ZS_ERROR_INVALID_BIOS;
    }
    
    // Read BIOS data
    size_t bytes_read = fread(memory->bios, 1, file_size, file);
    fclose(file);
    
    if (bytes_read != (size_t)file_size) {
        ZS_LOG_ERROR("Failed to read BIOS file: expected %ld bytes, got %zu", file_size, bytes_read);
        return ZS_ERROR_FILE_READ;
    }
    
    ZS_LOG_INFO("BIOS loaded successfully: %s (%zu bytes)", filename, bytes_read);
    return ZS_SUCCESS;
} 