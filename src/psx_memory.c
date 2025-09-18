#include "../include/psx_memory.h"
#include "../include/psx_gpu.h"
#include "../include/psx_dma.h"
#include "../include/psx_timer.h"
#include "../include/psx_irq.h"
#include "../include/psx_spu.h"
#include "../include/psx_cdrom.h"
#include "../include/psx_sio.h"
#include "../include/psx_mdec.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Guide.tex: Simple memory implementation following PSX-SPX memory map

// Memory storage
static u8 ram[PSX_RAM_SIZE];           // 2MB Main RAM
static u8 scratchpad[PSX_SCRATCHPAD_SIZE]; // 1KB Scratchpad
static u8 bios[PSX_BIOS_SIZE];         // 512KB BIOS ROM

static bool bios_loaded = false;

void memory_init(void) {
    // Clear all memory regions
    memset(ram, 0, sizeof(ram));
    memset(scratchpad, 0, sizeof(scratchpad));
    memset(bios, 0, sizeof(bios));
    
    printf("[MEMORY] Memory subsystem initialized\n");
    printf("[MEMORY] RAM: %dKB at 0x%08X\n", PSX_RAM_SIZE/1024, PSX_RAM_BASE);
    printf("[MEMORY] Scratchpad: %dB at 0x%08X\n", PSX_SCRATCHPAD_SIZE, PSX_SCRATCHPAD_BASE);
    printf("[MEMORY] BIOS: %dKB at 0x%08X\n", PSX_BIOS_SIZE/1024, PSX_BIOS_BASE);
}

void memory_shutdown(void) {
    printf("[MEMORY] Memory subsystem shutdown\n");
}

bool memory_load_bios(const char* filename) {
    FILE* file = fopen(filename, "rb");
    if (!file) {
        printf("[MEMORY] ERROR: Cannot open BIOS file: %s\n", filename);
        return false;
    }
    
    // Get file size
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    if (size != PSX_BIOS_SIZE) {
        printf("[MEMORY] ERROR: Invalid BIOS size: %ld bytes (expected %d)\n", size, PSX_BIOS_SIZE);
        fclose(file);
        return false;
    }
    
    // Load BIOS
    size_t read = fread(bios, 1, PSX_BIOS_SIZE, file);
    fclose(file);
    
    if (read != PSX_BIOS_SIZE) {
        printf("[MEMORY] ERROR: Failed to read BIOS file\n");
        return false;
    }
    
    bios_loaded = true;
    printf("[MEMORY] BIOS loaded successfully: %s (%ld bytes)\n", filename, size);
    return true;
}

// Guide.tex: Simple address decoding following PSX-SPX memory map
u32 memory_read32(u32 addr) {
    // PSX-SPX: Handle address mirroring and decode regions
    
    // RAM: 00000000h-001FFFFFh (mirrored in multiple regions)
    if ((addr >= 0x00000000 && addr < 0x00200000) ||
        (addr >= 0x80000000 && addr < 0x80200000) ||
        (addr >= 0xA0000000 && addr < 0xA0200000)) {
        return ram_read32(addr & PSX_RAM_MASK);
    }
    
    // Scratchpad: 1F800000h-1F8003FFh
    if (addr >= PSX_SCRATCHPAD_BASE && addr < (PSX_SCRATCHPAD_BASE + PSX_SCRATCHPAD_SIZE)) {
        return scratchpad_read32(addr & PSX_SCRATCHPAD_MASK);
    }
    
    // Hardware Registers: 1F801000h-1F802FFFh
    if (addr >= PSX_HW_BASE && addr < 0x1F803000) {
        return hw_read32(addr);
    }
    
    // BIOS: BFC00000h-BFC7FFFFh
    if (addr >= PSX_BIOS_BASE && addr < (PSX_BIOS_BASE + PSX_BIOS_SIZE)) {
        if (!bios_loaded) {
            printf("[MEMORY] ERROR: BIOS read without loaded BIOS at 0x%08X\n", addr);
            return 0;
        }
        u32 offset = addr & PSX_BIOS_MASK;
        return (bios[offset] |
                (bios[offset + 1] << 8) |
                (bios[offset + 2] << 16) |
                (bios[offset + 3] << 24));
    }
    
    printf("[MEMORY] ERROR: Unmapped read32 at 0x%08X\n", addr);
    return 0;
}

void memory_write32(u32 addr, u32 value) {
    // PSX-SPX: Handle address mirroring and decode regions
    
    // RAM: 00000000h-001FFFFFh (mirrored in multiple regions)
    if ((addr >= 0x00000000 && addr < 0x00200000) ||
        (addr >= 0x80000000 && addr < 0x80200000) ||
        (addr >= 0xA0000000 && addr < 0xA0200000)) {
        ram_write32(addr & PSX_RAM_MASK, value);
        return;
    }
    
    // Scratchpad: 1F800000h-1F8003FFh
    if (addr >= PSX_SCRATCHPAD_BASE && addr < (PSX_SCRATCHPAD_BASE + PSX_SCRATCHPAD_SIZE)) {
        scratchpad_write32(addr & PSX_SCRATCHPAD_MASK, value);
        return;
    }
    
    // Hardware Registers: 1F801000h-1F802FFFh
    if (addr >= PSX_HW_BASE && addr < 0x1F803000) {
        hw_write32(addr, value);
        return;
    }
    
    // BIOS: Read-only, ignore writes
    if (addr >= PSX_BIOS_BASE && addr < (PSX_BIOS_BASE + PSX_BIOS_SIZE)) {
        printf("[MEMORY] WARNING: Write to BIOS ignored at 0x%08X\n", addr);
        return;
    }
    
    // Guide.tex: CACHE_CONTROL register - log and ignore since we don't implement caches yet
    if (addr == 0xFFFE0130) {
        printf("[MEMORY] CACHE_CONTROL write at 0x%08X = 0x%08X (ignored)\n", addr, value);
        return;
    }
    
    printf("[MEMORY] ERROR: Unmapped write32 at 0x%08X = 0x%08X\n", addr, value);
}

// 16-bit and 8-bit access (simplified for now)
u16 memory_read16(u32 addr) {
    u32 word = memory_read32(addr & ~3);
    return (word >> ((addr & 3) * 8)) & 0xFFFF;
}

u8 memory_read8(u32 addr) {
    u32 word = memory_read32(addr & ~3);
    return (word >> ((addr & 3) * 8)) & 0xFF;
}

void memory_write16(u32 addr, u16 value) {
    // TODO: Implement proper 16-bit writes
    printf("[MEMORY] WARNING: 16-bit write not fully implemented at 0x%08X = 0x%04X\n", addr, value);
}

void memory_write8(u32 addr, u8 value) {
    // TODO: Implement proper 8-bit writes
    printf("[MEMORY] WARNING: 8-bit write not fully implemented at 0x%08X = 0x%02X\n", addr, value);
}

// Memory region implementations
u32 ram_read32(u32 addr) {
    if (addr >= PSX_RAM_SIZE - 3) {
        printf("[MEMORY] ERROR: RAM read32 out of bounds: 0x%08X\n", addr);
        return 0;
    }
    
    return (ram[addr] |
            (ram[addr + 1] << 8) |
            (ram[addr + 2] << 16) |
            (ram[addr + 3] << 24));
}

void ram_write32(u32 addr, u32 value) {
    if (addr >= PSX_RAM_SIZE - 3) {
        printf("[MEMORY] ERROR: RAM write32 out of bounds: 0x%08X\n", addr);
        return;
    }
    
    ram[addr] = value & 0xFF;
    ram[addr + 1] = (value >> 8) & 0xFF;
    ram[addr + 2] = (value >> 16) & 0xFF;
    ram[addr + 3] = (value >> 24) & 0xFF;
}

u32 scratchpad_read32(u32 addr) {
    if (addr >= PSX_SCRATCHPAD_SIZE - 3) {
        printf("[MEMORY] ERROR: Scratchpad read32 out of bounds: 0x%08X\n", addr);
        return 0;
    }
    
    return (scratchpad[addr] |
            (scratchpad[addr + 1] << 8) |
            (scratchpad[addr + 2] << 16) |
            (scratchpad[addr + 3] << 24));
}

void scratchpad_write32(u32 addr, u32 value) {
    if (addr >= PSX_SCRATCHPAD_SIZE - 3) {
        printf("[MEMORY] ERROR: Scratchpad write32 out of bounds: 0x%08X\n", addr);
        return;
    }
    
    scratchpad[addr] = value & 0xFF;
    scratchpad[addr + 1] = (value >> 8) & 0xFF;
    scratchpad[addr + 2] = (value >> 16) & 0xFF;
    scratchpad[addr + 3] = (value >> 24) & 0xFF;
}

// Hardware register routing (Guide.tex: delegate to components)
u32 hw_read32(u32 addr) {
    // PSX-SPX: Route to appropriate hardware component
    
    if (addr >= PSX_DMA_BASE && addr < (PSX_DMA_BASE + 0x80)) {
        return dma_read32(addr);
    }
    
    if (addr >= PSX_TIMER_BASE && addr < (PSX_TIMER_BASE + 0x30)) {
        return timer_read32(addr);
    }
    
    if (addr >= PSX_IRQ_BASE && addr < (PSX_IRQ_BASE + 0x8)) {
        return irq_read32(addr);
    }
    
    if (addr >= PSX_GPU_BASE && addr < (PSX_GPU_BASE + 0x8)) {
        return gpu_read32(addr);
    }
    
    if (addr >= PSX_SPU_BASE && addr < (PSX_SPU_BASE + 0x280)) {
        return spu_read32(addr);
    }
    
    if (addr >= PSX_CDROM_BASE && addr < (PSX_CDROM_BASE + 0x4)) {
        return cdrom_read32(addr);
    }
    
    if (addr >= PSX_SIO_BASE && addr < (PSX_SIO_BASE + 0x20)) {
        return sio_read32(addr);
    }
    
    if (addr >= PSX_MDEC_BASE && addr < (PSX_MDEC_BASE + 0x8)) {
        return mdec_read32(addr);
    }
    
    printf("[MEMORY] ERROR: Unhandled hardware read32 at 0x%08X\n", addr);
    return 0;
}

void hw_write32(u32 addr, u32 value) {
    // PSX-SPX: Route to appropriate hardware component
    
    if (addr >= PSX_DMA_BASE && addr < (PSX_DMA_BASE + 0x80)) {
        dma_write32(addr, value);
        return;
    }
    
    if (addr >= PSX_TIMER_BASE && addr < (PSX_TIMER_BASE + 0x30)) {
        timer_write32(addr, value);
        return;
    }
    
    if (addr >= PSX_IRQ_BASE && addr < (PSX_IRQ_BASE + 0x8)) {
        irq_write32(addr, value);
        return;
    }
    
    if (addr >= PSX_GPU_BASE && addr < (PSX_GPU_BASE + 0x8)) {
        gpu_write32(addr, value);
        return;
    }
    
    if (addr >= PSX_SPU_BASE && addr < (PSX_SPU_BASE + 0x280)) {
        spu_write32(addr, value);
        return;
    }
    
    if (addr >= PSX_CDROM_BASE && addr < (PSX_CDROM_BASE + 0x4)) {
        cdrom_write32(addr, value);
        return;
    }
    
    if (addr >= PSX_SIO_BASE && addr < (PSX_SIO_BASE + 0x20)) {
        sio_write32(addr, value);
        return;
    }
    
    if (addr >= PSX_MDEC_BASE && addr < (PSX_MDEC_BASE + 0x8)) {
        mdec_write32(addr, value);
        return;
    }
    
    printf("[MEMORY] ERROR: Unhandled hardware write32 at 0x%08X = 0x%08X\n", addr, value);
}