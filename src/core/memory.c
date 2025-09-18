/*
 * ZonistationOne - PlayStation One Emulator
 * Memory Management System Implementation
 */

#include "memory.h"
#include "logger.h"
#include <stdlib.h>
#include <string.h>

struct psx_memory_s {
    /* Main system RAM - 2MB */
    uint8_t *ram;
    
    /* BIOS ROM - 512KB */
    uint8_t *bios;
    
    /* Scratchpad RAM - 1KB */
    uint8_t *scratchpad;
    
    /* Hardware registers */
    uint32_t hw_regs[0x2000/4]; /* 8KB of HW registers */
    
    /* Memory control registers */
    uint32_t mem_control[8];
    
    /* State */
    int initialized;
    int bios_loaded;
};

psx_memory_t *memory_create(void) {
    psx_memory_t *mem = calloc(1, sizeof(psx_memory_t));
    if (!mem) {
        log_error("Failed to allocate memory structure");
        return NULL;
    }
    
    log_debug("Memory structure created");
    return mem;
}

int memory_init(psx_memory_t *mem) {
    if (!mem) {
        log_error("Invalid memory instance");
        return -1;
    }
    
    if (mem->initialized) {
        log_warn("Memory already initialized");
        return 0;
    }
    
    /* Allocate main RAM */
    mem->ram = calloc(1, PSX_RAM_SIZE);
    if (!mem->ram) {
        log_error("Failed to allocate main RAM");
        return -1;
    }
    
    /* Allocate BIOS ROM */
    mem->bios = calloc(1, PSX_BIOS_SIZE);
    if (!mem->bios) {
        log_error("Failed to allocate BIOS ROM");
        free(mem->ram);
        return -1;
    }
    
    /* Allocate scratchpad */
    mem->scratchpad = calloc(1, PSX_SCRATCHPAD_SIZE);
    if (!mem->scratchpad) {
        log_error("Failed to allocate scratchpad");
        free(mem->ram);
        free(mem->bios);
        return -1;
    }
    
    /* Initialize hardware registers to default values */
    memset(mem->hw_regs, 0, sizeof(mem->hw_regs));
    
    /* Initialize memory control registers */
    mem->mem_control[0] = 0x0013243F; /* Expansion 1 base */
    mem->mem_control[1] = 0x00003022; /* Expansion 2 base */
    mem->mem_control[2] = 0x0013243F; /* Expansion 1 delay/size */
    mem->mem_control[3] = 0x00003022; /* Expansion 3 delay/size */
    mem->mem_control[4] = 0x0013243F; /* BIOS ROM delay/size */
    mem->mem_control[5] = 0x00003022; /* SPU delay/size */
    mem->mem_control[6] = 0x0013243F; /* CD-ROM delay/size */
    mem->mem_control[7] = 0x00003022; /* Expansion 2 delay/size */
    
    mem->initialized = 1;
    
    log_info("Memory system initialized (RAM: %dKB, BIOS: %dKB, Scratchpad: %dB)", 
             PSX_RAM_SIZE / 1024, PSX_BIOS_SIZE / 1024, PSX_SCRATCHPAD_SIZE);
    
    return 0;
}

int memory_load_bios(psx_memory_t *mem, const uint8_t *data, size_t size) {
    if (!mem || !data || size != PSX_BIOS_SIZE) {
        log_error("Invalid parameters for BIOS loading");
        return -1;
    }
    
    if (!mem->initialized) {
        log_error("Memory not initialized");
        return -1;
    }
    
    memcpy(mem->bios, data, PSX_BIOS_SIZE);
    mem->bios_loaded = 1;
    
    log_info("BIOS loaded into memory (%zu bytes)", size);
    return 0;
}

uint32_t memory_translate_address(uint32_t virtual_addr) {
    /* PlayStation memory map translation */
    
    /* Remove segment bits (upper 3 bits) for KUSEG/KSEG0/KSEG1 */
    uint32_t physical = virtual_addr & 0x1FFFFFFF;
    
    /* Handle specific address ranges */
    if (physical < 0x00800000) {
        /* Main RAM (mirrored every 2MB up to 8MB) */
        return physical & PSX_RAM_SIZE_MASK;
    } else if (physical >= 0x1F800000 && physical < 0x1F800400) {
        /* Scratchpad */
        return physical;
    } else if (physical >= 0x1F801000 && physical < 0x1F803000) {
        /* Hardware registers */
        return physical;
    } else if (physical >= 0x1FC00000 && physical < 0x1FC80000) {
        /* BIOS ROM */
        return physical;
    }
    
    /* Default: pass through */
    return physical;
}

int memory_is_valid_address(uint32_t address) {
    uint32_t physical = memory_translate_address(address);
    
    /* Check if address falls within valid ranges */
    if (physical < 0x00800000) return 1; /* Main RAM area */
    if (physical >= 0x1F800000 && physical < 0x1F800400) return 1; /* Scratchpad */
    if (physical >= 0x1F801000 && physical < 0x1F803000) return 1; /* Hardware registers */
    if (physical >= 0x1FC00000 && physical < 0x1FC80000) return 1; /* BIOS ROM */
    
    return 0; /* Invalid address */
}

uint8_t memory_read8(psx_memory_t *mem, uint32_t address) {
    if (!mem || !mem->initialized) {
        log_error("Memory not initialized");
        return 0xFF;
    }
    
    uint32_t physical = memory_translate_address(address);
    
    /* Main RAM */
    if (physical < PSX_RAM_SIZE) {
        return mem->ram[physical];
    }
    
    /* Scratchpad */
    if (physical >= PSX_SCRATCHPAD_BASE && physical < PSX_SCRATCHPAD_BASE + PSX_SCRATCHPAD_SIZE) {
        return mem->scratchpad[physical - PSX_SCRATCHPAD_BASE];
    }
    
    /* Hardware registers */
    if (physical >= PSX_IO_PORTS_BASE && physical < PSX_IO_PORTS_BASE + 0x2000) {
        uint32_t reg_addr = (physical - PSX_IO_PORTS_BASE) / 4;
        uint32_t byte_offset = (physical - PSX_IO_PORTS_BASE) % 4;
        return (mem->hw_regs[reg_addr] >> (byte_offset * 8)) & 0xFF;
    }
    
    /* BIOS ROM */
    if (physical >= PSX_BIOS_BASE && physical < PSX_BIOS_BASE + PSX_BIOS_SIZE) {
        if (!mem->bios_loaded) {
            log_warn("Reading from unloaded BIOS at 0x%08X", address);
            return 0xFF;
        }
        return mem->bios[physical - PSX_BIOS_BASE];
    }
    
    log_warn("Invalid memory read8 at address 0x%08X", address);
    return 0xFF;
}

uint16_t memory_read16(psx_memory_t *mem, uint32_t address) {
    if (address & 1) {
        log_warn("Unaligned 16-bit read at 0x%08X", address);
    }
    
    uint8_t lo = memory_read8(mem, address);
    uint8_t hi = memory_read8(mem, address + 1);
    return lo | (hi << 8);
}

uint32_t memory_read32(psx_memory_t *mem, uint32_t address) {
    if (address & 3) {
        log_warn("Unaligned 32-bit read at 0x%08X", address);
    }
    
    uint16_t lo = memory_read16(mem, address);
    uint16_t hi = memory_read16(mem, address + 2);
    return lo | (hi << 16);
}

void memory_write8(psx_memory_t *mem, uint32_t address, uint8_t value) {
    if (!mem || !mem->initialized) {
        log_error("Memory not initialized");
        return;
    }
    
    uint32_t physical = memory_translate_address(address);
    
    /* Main RAM */
    if (physical < PSX_RAM_SIZE) {
        mem->ram[physical] = value;
        return;
    }
    
    /* Scratchpad */
    if (physical >= PSX_SCRATCHPAD_BASE && physical < PSX_SCRATCHPAD_BASE + PSX_SCRATCHPAD_SIZE) {
        mem->scratchpad[physical - PSX_SCRATCHPAD_BASE] = value;
        return;
    }
    
    /* Hardware registers */
    if (physical >= PSX_IO_PORTS_BASE && physical < PSX_IO_PORTS_BASE + 0x2000) {
        uint32_t reg_addr = (physical - PSX_IO_PORTS_BASE) / 4;
        uint32_t byte_offset = (physical - PSX_IO_PORTS_BASE) % 4;
        uint32_t mask = 0xFF << (byte_offset * 8);
        mem->hw_regs[reg_addr] = (mem->hw_regs[reg_addr] & ~mask) | ((value << (byte_offset * 8)) & mask);
        return;
    }
    
    /* BIOS ROM (read-only) */
    if (physical >= PSX_BIOS_BASE && physical < PSX_BIOS_BASE + PSX_BIOS_SIZE) {
        log_warn("Attempted write to BIOS ROM at 0x%08X", address);
        return;
    }
    
    log_warn("Invalid memory write8 at address 0x%08X", address);
}

void memory_write16(psx_memory_t *mem, uint32_t address, uint16_t value) {
    if (address & 1) {
        log_warn("Unaligned 16-bit write at 0x%08X", address);
    }
    
    memory_write8(mem, address, value & 0xFF);
    memory_write8(mem, address + 1, (value >> 8) & 0xFF);
}

void memory_write32(psx_memory_t *mem, uint32_t address, uint32_t value) {
    if (address & 3) {
        log_warn("Unaligned 32-bit write at 0x%08X", address);
    }
    
    memory_write16(mem, address, value & 0xFFFF);
    memory_write16(mem, address + 2, (value >> 16) & 0xFFFF);
}

void *memory_get_ptr(psx_memory_t *mem, uint32_t address, size_t size) {
    if (!mem || !mem->initialized) {
        log_error("Memory not initialized");
        return NULL;
    }
    
    uint32_t physical = memory_translate_address(address);
    
    /* Main RAM */
    if (physical < PSX_RAM_SIZE && physical + size <= PSX_RAM_SIZE) {
        return &mem->ram[physical];
    }
    
    /* Scratchpad */
    if (physical >= PSX_SCRATCHPAD_BASE && 
        physical < PSX_SCRATCHPAD_BASE + PSX_SCRATCHPAD_SIZE &&
        physical + size <= PSX_SCRATCHPAD_BASE + PSX_SCRATCHPAD_SIZE) {
        return &mem->scratchpad[physical - PSX_SCRATCHPAD_BASE];
    }
    
    /* BIOS ROM */
    if (mem->bios_loaded && 
        physical >= PSX_BIOS_BASE && 
        physical < PSX_BIOS_BASE + PSX_BIOS_SIZE &&
        physical + size <= PSX_BIOS_BASE + PSX_BIOS_SIZE) {
        return &mem->bios[physical - PSX_BIOS_BASE];
    }
    
    log_warn("Cannot get pointer for address 0x%08X (size %zu)", address, size);
    return NULL;
}

void memory_reset(psx_memory_t *mem) {
    if (!mem || !mem->initialized) {
        return;
    }
    
    /* Clear main RAM */
    memset(mem->ram, 0, PSX_RAM_SIZE);
    
    /* Clear scratchpad */
    memset(mem->scratchpad, 0, PSX_SCRATCHPAD_SIZE);
    
    /* Reset hardware registers */
    memset(mem->hw_regs, 0, sizeof(mem->hw_regs));
    
    log_info("Memory system reset");
}

void memory_shutdown(psx_memory_t *mem) {
    if (!mem) return;
    
    if (mem->ram) {
        free(mem->ram);
        mem->ram = NULL;
    }
    
    if (mem->bios) {
        free(mem->bios);
        mem->bios = NULL;
    }
    
    if (mem->scratchpad) {
        free(mem->scratchpad);
        mem->scratchpad = NULL;
    }
    
    mem->initialized = 0;
    mem->bios_loaded = 0;
    
    log_info("Memory system shutdown");
}

void memory_destroy(psx_memory_t *mem) {
    if (mem) {
        memory_shutdown(mem);
        free(mem);
    }
}