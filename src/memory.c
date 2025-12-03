#include "memory.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

psx_result_t memory_init(psx_memory_t* memory) {
    // Clear all memory regions
    memset(memory->ram, 0, PSX_RAM_SIZE);
    memset(memory->bios, 0, PSX_BIOS_SIZE);
    memset(memory->scratchpad, 0, PSX_SCRATCHPAD_SIZE);
    
    memory->bios_loaded = false;
    
    printf("[Memory] Initialized %dKB RAM, %dKB BIOS, %dB scratchpad\n",
           PSX_RAM_SIZE / 1024, PSX_BIOS_SIZE / 1024, PSX_SCRATCHPAD_SIZE);
    
    return PSX_OK;
}

psx_result_t memory_load_bios(psx_memory_t* memory, const char* bios_path) {
    FILE* file = fopen(bios_path, "rb");
    if (!file) {
        printf("[Memory] Error: Could not open BIOS file: %s\n", bios_path);
        return PSX_ERROR_FILE_NOT_FOUND;
    }
    
    // Get file size
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    // Verify BIOS size
    if (file_size != PSX_BIOS_SIZE) {
        printf("[Memory] Error: Invalid BIOS size %ld (expected %d)\n", 
               file_size, PSX_BIOS_SIZE);
        fclose(file);
        return PSX_ERROR_FILE_NOT_FOUND;
    }
    
    // Load BIOS into memory
    size_t bytes_read = fread(memory->bios, 1, PSX_BIOS_SIZE, file);
    fclose(file);
    
    if (bytes_read != PSX_BIOS_SIZE) {
        printf("[Memory] Error: Failed to read complete BIOS file\n");
        return PSX_ERROR_FILE_NOT_FOUND;
    }
    
    memory->bios_loaded = true;
    printf("[Memory] Loaded BIOS: %s (%dKB)\n", bios_path, PSX_BIOS_SIZE / 1024);
    
    // Print first few bytes for verification
    printf("[Memory] BIOS header: %02X %02X %02X %02X %02X %02X %02X %02X\n",
           memory->bios[0], memory->bios[1], memory->bios[2], memory->bios[3],
           memory->bios[4], memory->bios[5], memory->bios[6], memory->bios[7]);
    
    return PSX_OK;
}

u32 memory_translate_address(u32 virtual_address) {
    // PlayStation 1 uses a simple memory mapping scheme
    // Remove upper bits to get physical address
    return virtual_address & 0x1FFFFFFF;
}

memory_region_t memory_get_region(u32 address) {
    u32 physical = memory_translate_address(address);
    
    if (physical >= PSX_RAM_BASE && physical <= PSX_RAM_END) {
        return MEMORY_REGION_RAM;
    }
    else if (physical >= PSX_BIOS_BASE && physical <= PSX_BIOS_END) {
        return MEMORY_REGION_BIOS;
    }
    else if (physical >= PSX_SCRATCHPAD_BASE && 
             physical < PSX_SCRATCHPAD_BASE + PSX_SCRATCHPAD_SIZE) {
        return MEMORY_REGION_SCRATCHPAD;
    }
    else if (physical >= PSX_IO_BASE && physical < PSX_EXPANSION2_BASE) {
        return MEMORY_REGION_IO;
    }
    
    return MEMORY_REGION_INVALID;
}

bool memory_is_valid_address(u32 address) {
    return memory_get_region(address) != MEMORY_REGION_INVALID;
}

u8 memory_read8(const psx_memory_t* memory, u32 address) {
    u32 physical = memory_translate_address(address);
    memory_region_t region = memory_get_region(address);
    
    switch (region) {
        case MEMORY_REGION_RAM:
            return memory->ram[physical & PSX_RAM_MASK];
            
        case MEMORY_REGION_BIOS:
            if (!memory->bios_loaded) {
                printf("[Memory] Warning: Reading from unloaded BIOS at 0x%08X\n", address);
                return 0xFF;
            }
            return memory->bios[physical & PSX_BIOS_MASK];
            
        case MEMORY_REGION_SCRATCHPAD:
            return memory->scratchpad[physical & PSX_SCRATCHPAD_MASK];
            
        case MEMORY_REGION_IO:
            // TODO: Implement I/O register handling
            printf("[Memory] Warning: Unimplemented I/O read at 0x%08X\n", address);
            return 0x00;
            
        default:
            printf("[Memory] Warning: Invalid read at 0x%08X\n", address);
            return 0xFF;
    }
}

u16 memory_read16(const psx_memory_t* memory, u32 address) {
    // Check alignment
    if (address & 1) {
        printf("[Memory] Warning: Unaligned 16-bit read at 0x%08X\n", address);
    }
    
    u8 low = memory_read8(memory, address);
    u8 high = memory_read8(memory, address + 1);
    
    return (u16)low | ((u16)high << 8);
}

u32 memory_read32(const psx_memory_t* memory, u32 address) {
    // Check alignment
    if (address & 3) {
        printf("[Memory] Warning: Unaligned 32-bit read at 0x%08X\n", address);
    }
    
    u32 physical = memory_translate_address(address);
    memory_region_t region = memory_get_region(address);
    
    switch (region) {
        case MEMORY_REGION_RAM: {
            u32 offset = physical & PSX_RAM_MASK;
            return *(u32*)&memory->ram[offset];
        }
        
        case MEMORY_REGION_BIOS: {
            if (!memory->bios_loaded) {
                printf("[Memory] Warning: Reading from unloaded BIOS at 0x%08X\n", address);
                return 0xFFFFFFFF;
            }
            u32 offset = physical & PSX_BIOS_MASK;
            return *(u32*)&memory->bios[offset];
        }
        
        case MEMORY_REGION_SCRATCHPAD: {
            u32 offset = physical & PSX_SCRATCHPAD_MASK;
            return *(u32*)&memory->scratchpad[offset];
        }
        
        case MEMORY_REGION_IO:
            // TODO: Implement I/O register handling
            printf("[Memory] Warning: Unimplemented I/O read32 at 0x%08X\n", address);
            return 0x00000000;
            
        default:
            printf("[Memory] Warning: Invalid read32 at 0x%08X\n", address);
            return 0xFFFFFFFF;
    }
}

void memory_write8(psx_memory_t* memory, u32 address, u8 value) {
    u32 physical = memory_translate_address(address);
    memory_region_t region = memory_get_region(address);
    
    switch (region) {
        case MEMORY_REGION_RAM:
            memory->ram[physical & PSX_RAM_MASK] = value;
            break;
            
        case MEMORY_REGION_BIOS:
            printf("[Memory] Warning: Attempt to write to BIOS at 0x%08X\n", address);
            break;
            
        case MEMORY_REGION_SCRATCHPAD:
            memory->scratchpad[physical & PSX_SCRATCHPAD_MASK] = value;
            break;
            
        case MEMORY_REGION_IO:
            // TODO: Implement I/O register handling
            printf("[Memory] Warning: Unimplemented I/O write at 0x%08X = 0x%02X\n", 
                   address, value);
            break;
            
        default:
            printf("[Memory] Warning: Invalid write at 0x%08X = 0x%02X\n", 
                   address, value);
            break;
    }
}

void memory_write16(psx_memory_t* memory, u32 address, u16 value) {
    // Check alignment
    if (address & 1) {
        printf("[Memory] Warning: Unaligned 16-bit write at 0x%08X\n", address);
    }
    
    memory_write8(memory, address, (u8)(value & 0xFF));
    memory_write8(memory, address + 1, (u8)(value >> 8));
}

void memory_write32(psx_memory_t* memory, u32 address, u32 value) {
    // Check alignment
    if (address & 3) {
        printf("[Memory] Warning: Unaligned 32-bit write at 0x%08X\n", address);
    }
    
    u32 physical = memory_translate_address(address);
    memory_region_t region = memory_get_region(address);
    
    switch (region) {
        case MEMORY_REGION_RAM: {
            u32 offset = physical & PSX_RAM_MASK;
            *(u32*)&memory->ram[offset] = value;
            break;
        }
        
        case MEMORY_REGION_BIOS:
            printf("[Memory] Warning: Attempt to write32 to BIOS at 0x%08X\n", address);
            break;
            
        case MEMORY_REGION_SCRATCHPAD: {
            u32 offset = physical & PSX_SCRATCHPAD_MASK;
            *(u32*)&memory->scratchpad[offset] = value;
            break;
        }
        
        case MEMORY_REGION_IO:
            // TODO: Implement I/O register handling
            printf("[Memory] Warning: Unimplemented I/O write32 at 0x%08X = 0x%08X\n",
                   address, value);
            break;
            
        default:
            printf("[Memory] Warning: Invalid write32 at 0x%08X = 0x%08X\n",
                   address, value);
            break;
    }
}

void memory_dump_region(const psx_memory_t* memory, u32 start_addr, u32 length) {
    printf("[Memory] Dump 0x%08X - 0x%08X:\n", start_addr, start_addr + length - 1);
    
    for (u32 i = 0; i < length; i += 16) {
        printf("0x%08X: ", start_addr + i);
        
        // Hex dump
        for (u32 j = 0; j < 16 && (i + j) < length; j++) {
            u8 byte = memory_read8(memory, start_addr + i + j);
            printf("%02X ", byte);
        }
        
        // Pad remaining hex
        for (u32 j = i + 16 - i; j < 16; j++) {
            printf("   ");
        }
        
        printf(" |");
        
        // ASCII dump
        for (u32 j = 0; j < 16 && (i + j) < length; j++) {
            u8 byte = memory_read8(memory, start_addr + i + j);
            printf("%c", (byte >= 32 && byte <= 126) ? byte : '.');
        }
        
        printf("|\n");
    }
}