#include "../include/interconnect.h"
#include "../include/log.h"
#include <stdio.h>
#include "../include/emulator.h" // For EmulatorContext cast

// RAM region handlers
uint32_t ram_read32(void* ctx, uint32_t addr) {
    EmulatorContext* ectx = (EmulatorContext*)ctx;
    uint32_t offset = addr & (sizeof(ectx->ram.data) - 1);
    uint8_t* data = ectx->ram.data;
    uint32_t b0 = data[offset + 0];
    uint32_t b1 = data[offset + 1];
    uint32_t b2 = data[offset + 2];
    uint32_t b3 = data[offset + 3];
    uint32_t value = b0 | (b1 << 8) | (b2 << 16) | (b3 << 24);
    NC_LOGI("RAM read32: addr=0x%08x value=0x%08x", addr, value);
    return value;
}
void ram_write32(void* ctx, uint32_t addr, uint32_t value) {
    EmulatorContext* ectx = (EmulatorContext*)ctx;
    uint32_t offset = addr & (sizeof(ectx->ram.data) - 1);
    uint8_t* data = ectx->ram.data;
    data[offset + 0] = (uint8_t)(value & 0xFF);
    data[offset + 1] = (uint8_t)((value >> 8) & 0xFF);
    data[offset + 2] = (uint8_t)((value >> 16) & 0xFF);
    data[offset + 3] = (uint8_t)((value >> 24) & 0xFF);
    NC_LOGI("RAM write32: addr=0x%08x value=0x%08x", addr, value);
}

// BIOS region handlers
uint32_t bios_read32(void* ctx, uint32_t addr) {
    EmulatorContext* ectx = (EmulatorContext*)ctx;
    uint32_t offset = addr & (sizeof(ectx->bios.data) - 1);
    uint8_t* data = ectx->bios.data;
    uint32_t b0 = data[offset + 0];
    uint32_t b1 = data[offset + 1];
    uint32_t b2 = data[offset + 2];
    uint32_t b3 = data[offset + 3];
    uint32_t value = b0 | (b1 << 8) | (b2 << 16) | (b3 << 24);
    NC_LOGI("BIOS read32: addr=0x%08x value=0x%08x", addr, value);
    return value;
}
void bios_write32(void* ctx, uint32_t addr, uint32_t value) {
    // BIOS is read-only
    NC_LOGI("BIOS write32 IGNORED: addr=0x%08x value=0x%08x", addr, value);
}

// DMA region handlers
uint32_t dma_read32(void* ctx, uint32_t addr) {
    NC_LOGI("DMA read32: addr=0x%08x", addr);
    // TODO: Implement real DMA register reads
    return 0;
}
void dma_write32(void* ctx, uint32_t addr, uint32_t value) {
    NC_LOGI("DMA write32: addr=0x%08x value=0x%08x", addr, value);
    // TODO: Implement real DMA register writes
}

// Scratchpad region handlers (1KB data cache RAM)
uint32_t scratchpad_read32(void* ctx, uint32_t addr) {
    EmulatorContext* ectx = (EmulatorContext*)ctx;
    uint32_t offset = addr & 0x3FF; // 1KB = 0x400, mask to 0x3FF
    uint8_t* data = ectx->scratchpad.data;
    uint32_t b0 = data[offset + 0];
    uint32_t b1 = data[offset + 1];
    uint32_t b2 = data[offset + 2];
    uint32_t b3 = data[offset + 3];
    uint32_t value = b0 | (b1 << 8) | (b2 << 16) | (b3 << 24);
    NC_LOGI("SCRATCHPAD read32: addr=0x%08x offset=0x%03x value=0x%08x", addr, offset, value);
    return value;
}
void scratchpad_write32(void* ctx, uint32_t addr, uint32_t value) {
    EmulatorContext* ectx = (EmulatorContext*)ctx;
    uint32_t offset = addr & 0x3FF;
    uint8_t* data = ectx->scratchpad.data;
    data[offset + 0] = (uint8_t)(value & 0xFF);
    data[offset + 1] = (uint8_t)((value >> 8) & 0xFF);
    data[offset + 2] = (uint8_t)((value >> 16) & 0xFF);
    data[offset + 3] = (uint8_t)((value >> 24) & 0xFF);
    NC_LOGI("SCRATCHPAD write32: addr=0x%08x offset=0x%03x value=0x%08x", addr, offset, value);
}

// Timer region handlers
uint32_t timer_read32(void* ctx, uint32_t addr) {
    EmulatorContext* ectx = (EmulatorContext*)ctx;
    uint32_t offset = addr & 0x2F; // Timer registers 0x1f801100-0x1f80112F
    uint32_t value = 0;
    
    // Return incrementing values for timer counters to simulate time passing
    switch (offset) {
        case 0x00: // Timer 0 Counter
        case 0x10: // Timer 1 Counter  
        case 0x20: // Timer 2 Counter
            value = (ectx->cycle_count / 1000) & 0xFFFF; // Simple time simulation
            break;
        case 0x04: // Timer 0 Mode
        case 0x14: // Timer 1 Mode
        case 0x24: // Timer 2 Mode
            value = 0x0000; // Default mode
            break;
        case 0x08: // Timer 0 Target
        case 0x18: // Timer 1 Target
        case 0x28: // Timer 2 Target
            value = 0xFFFF; // Default target
            break;
        default:
            value = 0;
            break;
    }
    
    NC_LOGI("TIMER read32: addr=0x%08x offset=0x%02x value=0x%08x", addr, offset, value);
    return value;
}
void timer_write32(void* ctx, uint32_t addr, uint32_t value) {
    EmulatorContext* ectx = (EmulatorContext*)ctx;
    uint32_t offset = addr & 0x2F;
    NC_LOGI("TIMER write32: addr=0x%08x offset=0x%02x value=0x%08x", addr, offset, value);
    // TODO: Implement actual timer register writes
}

// Interrupt controller handlers
uint32_t irq_read32(void* ctx, uint32_t addr) {
    EmulatorContext* ectx = (EmulatorContext*)ctx;
    uint32_t offset = addr & 0x7; // IRQ registers 0x1f801070-0x1f801077
    uint32_t value = 0;
    
    switch (offset) {
        case 0x00: // I_STAT (Interrupt Status)
            value = ectx->irq_status;
            break;
        case 0x04: // I_MASK (Interrupt Mask)
            value = ectx->irq_mask;
            break;
        default:
            value = 0;
            break;
    }
    
    NC_LOGI("IRQ read32: addr=0x%08x offset=0x%02x value=0x%08x", addr, offset, value);
    return value;
}
void irq_write32(void* ctx, uint32_t addr, uint32_t value) {
    EmulatorContext* ectx = (EmulatorContext*)ctx;
    uint32_t offset = addr & 0x7;
    
    switch (offset) {
        case 0x00: // I_STAT - Writing clears bits (acknowledge interrupts)
            ectx->irq_status &= ~(value & 0x7FF); // Only bits 0-10 matter
            NC_LOGI("IRQ write32: I_STAT clear=0x%04x, new status=0x%04x", value, ectx->irq_status);
            break;
        case 0x04: // I_MASK - Writing sets the interrupt mask
            ectx->irq_mask = value & 0x7FF;
            NC_LOGI("IRQ write32: I_MASK=0x%04x", ectx->irq_mask);
            break;
        default:
            NC_LOGW("IRQ write32: unknown offset 0x%02x = 0x%08x", offset, value);
            break;
    }
}

// SIO (Serial I/O) handlers
uint32_t sio_read32(void* ctx, uint32_t addr) {
    EmulatorContext* ectx = (EmulatorContext*)ctx;
    uint32_t offset = addr & 0x1F; // SIO registers 0x1f801040-0x1f80105F
    uint32_t value = 0;
    
    switch (offset) {
        case 0x00: // JOY_DATA
            value = 0xFF; // Controller present, idle
            break;
        case 0x04: // JOY_STAT
            value = 0x0000; // Ready
            break;
        case 0x08: // JOY_MODE
            value = 0x0000; // Default mode
            break;
        case 0x0A: // JOY_CTRL
            value = 0x0000; // Default control
            break;
        case 0x0C: // JOY_BAUD
            value = 0x0000; // Default baud rate
            break;
        default:
            value = 0;
            break;
    }
    
    NC_LOGI("SIO read32: addr=0x%08x offset=0x%02x value=0x%08x", addr, offset, value);
    return value;
}
void sio_write32(void* ctx, uint32_t addr, uint32_t value) {
    EmulatorContext* ectx = (EmulatorContext*)ctx;
    uint32_t offset = addr & 0x1F;
    NC_LOGI("SIO write32: addr=0x%08x offset=0x%02x value=0x%08x", addr, offset, value);
    // TODO: Implement actual SIO register writes
}

// CDROM handlers
uint32_t cdrom_read32(void* ctx, uint32_t addr) {
    EmulatorContext* ectx = (EmulatorContext*)ctx;
    uint32_t offset = addr & 0x3; // CDROM registers 0x1f801800-0x1f801803
    uint32_t value = 0;
    
    switch (offset) {
        case 0x00: // CDROM Index/Status
            value = 0x1B; // Ready, no disc
            break;
        case 0x01: // CDROM Response
            value = 0x00; // No response
            break;
        case 0x02: // CDROM Flag
            value = 0x00; // No flag
            break;
        case 0x03: // CDROM Data
            value = 0x00; // No data
            break;
        default:
            value = 0;
            break;
    }
    
    NC_LOGI("CDROM read32: addr=0x%08x offset=0x%02x value=0x%08x", addr, offset, value);
    return value;
}
void cdrom_write32(void* ctx, uint32_t addr, uint32_t value) {
    EmulatorContext* ectx = (EmulatorContext*)ctx;
    uint32_t offset = addr & 0x3;
    NC_LOGI("CDROM write32: addr=0x%08x offset=0x%02x value=0x%08x", addr, offset, value);
    // TODO: Implement actual CDROM register writes
}

// SPU (Sound Processing Unit) handlers
uint32_t spu_read32(void* ctx, uint32_t addr) {
    EmulatorContext* ectx = (EmulatorContext*)ctx;
    uint32_t offset = addr & 0x1FF; // SPU registers 0x1f801C00-0x1f801DFF
    uint32_t value = 0;
    
    // Return plausible SPU register values
    switch (offset) {
        case 0x1AA: // SPU Status
            value = 0x0000; // Ready
            break;
        case 0x1AC: // SPU Control
            value = 0x0000; // Default control
            break;
        default:
            value = 0;
            break;
    }
    
    NC_LOGI("SPU read32: addr=0x%08x offset=0x%03x value=0x%08x", addr, offset, value);
    return value;
}
void spu_write32(void* ctx, uint32_t addr, uint32_t value) {
    EmulatorContext* ectx = (EmulatorContext*)ctx;
    uint32_t offset = addr & 0x1FF;
    NC_LOGI("SPU write32: addr=0x%08x offset=0x%03x value=0x%08x", addr, offset, value);
    // TODO: Implement actual SPU register writes
}

// Memory control handlers
uint32_t memctrl_read32(void* ctx, uint32_t addr) {
    EmulatorContext* ectx = (EmulatorContext*)ctx;
    uint32_t offset = addr & 0x7F; // Memory control registers 0x1f801000-0x1f80107F
    uint32_t value = 0;
    
    switch (offset) {
        case 0x00: // Expansion 1 Base Address
            value = 0x1F000000;
            break;
        case 0x04: // Expansion 2 Base Address
            value = 0x1F802000;
            break;
        case 0x60: // RAM Size
            value = 0x00000000; // 2MB
            break;
        default:
            value = 0;
            break;
    }
    
    NC_LOGI("MEMCTRL read32: addr=0x%08x offset=0x%02x value=0x%08x", addr, offset, value);
    return value;
}
void memctrl_write32(void* ctx, uint32_t addr, uint32_t value) {
    EmulatorContext* ectx = (EmulatorContext*)ctx;
    uint32_t offset = addr & 0x7F;
    NC_LOGI("MEMCTRL write32: addr=0x%08x offset=0x%02x value=0x%08x", addr, offset, value);
    // TODO: Implement actual memory control register writes
}

// Hardware register region handler (fallback for unmapped registers)
uint32_t hwreg_read32(void* ctx, uint32_t addr) {
    NC_LOGW("[HWREG] Read32 from 0x%08x (stub, returns 0)", addr);
    // Add detailed logging for register access
    static FILE* reglog = NULL;
    if (!reglog) reglog = fopen("hwreg_access.log", "a");
    if (reglog) fprintf(reglog, "READ  0x%08x\n", addr);
    return 0;
}
void hwreg_write32(void* ctx, uint32_t addr, uint32_t value) {
    NC_LOGW("[HWREG] Write32 to 0x%08x = 0x%08x (stub)", addr, value);
    // Add detailed logging for register access
    static FILE* reglog = NULL;
    if (!reglog) reglog = fopen("hwreg_access.log", "a");
    if (reglog) fprintf(reglog, "WRITE 0x%08x = 0x%08x\n", addr, value);
}

// VRAM region handlers (minimal stub)
uint32_t vram_read32(void* ctx, uint32_t addr) {
    EmulatorContext* ectx = (EmulatorContext*)ctx;
    uint32_t offset = addr & (NC_VRAM_SIZE - 1);
    uint32_t value = nc_vram_load32(&ectx->gpu.vram, offset);
    NC_LOGI("VRAM read32: addr=0x%08x offset=0x%06x value=0x%08x", addr, offset, value);
    return value;
}
void vram_write32(void* ctx, uint32_t addr, uint32_t value) {
    EmulatorContext* ectx = (EmulatorContext*)ctx;
    uint32_t offset = addr & (NC_VRAM_SIZE - 1);
    nc_vram_store32(&ectx->gpu.vram, offset, value);
    NC_LOGI("VRAM write32: addr=0x%08x offset=0x%06x value=0x%08x", addr, offset, value);
}

// GPU command region handlers
uint32_t gpu_read32(void* ctx, uint32_t addr) {
    EmulatorContext* ectx = (EmulatorContext*)ctx;
    uint32_t offset = addr & 0x1F;
    uint32_t value = 0;
    
    switch (offset) {
        case 0x00: // GPUREAD - Read data from GPU
            value = 0; // TODO: Implement GPU data reading
            break;
        case 0x04: // GPUSTAT - GPU status
            value = 0x1C000000; // Basic status: ready, DMA idle
            break;
        default:
            value = 0;
            break;
    }
    
    NC_LOGI("GPU read32: addr=0x%08x offset=0x%02x value=0x%08x", addr, offset, value);
    return value;
}

void gpu_write32(void* ctx, uint32_t addr, uint32_t value) {
    EmulatorContext* ectx = (EmulatorContext*)ctx;
    uint32_t offset = addr & 0x1F;
    
    switch (offset) {
        case 0x00: // GP0 - GPU command/data
            // TODO: Implement GPU command processing
            NC_LOGI("GPU GP0 command: 0x%08x", value);
            break;
        case 0x04: // GP1 - GPU control
            // TODO: Implement GPU control commands
            NC_LOGI("GPU GP1 control: 0x%08x", value);
            break;
        default:
            NC_LOGW("GPU write32: unknown offset 0x%02x = 0x%08x", offset, value);
            break;
    }
}

// Memory region table - Expanded based on pcsx_rearmed_reference
#undef NUM_REGIONS
#define NUM_REGIONS 20
static NcMemRegion memory_map[NUM_REGIONS] = {
    // Main memory regions
    { 0x00000000, 0x001FFFFF, ram_read32, ram_write32 },      // RAM (2MB)
    { 0x80000000, 0x801FFFFF, ram_read32, ram_write32 },      // RAM alias (KSEG0)
    { 0xA0000000, 0xA01FFFFF, ram_read32, ram_write32 },      // RAM alias (KSEG1)
    
    // BIOS regions
    { 0x1FC00000, 0x1FC7FFFF, bios_read32, bios_write32 },    // BIOS (512KB)
    { 0xBFC00000, 0xBFC7FFFF, bios_read32, bios_write32 },    // BIOS alias (reset vector)
    { 0x9FC00000, 0x9FC7FFFF, bios_read32, bios_write32 },    // BIOS alias (cached)
    
    // Scratchpad (1KB data cache RAM)
    { 0x1F800000, 0x1F8003FF, scratchpad_read32, scratchpad_write32 },
    { 0x9F800000, 0x9F8003FF, scratchpad_read32, scratchpad_write32 },
    
    // Hardware registers (organized by subsystem)
    { 0x1F801000, 0x1F80107F, memctrl_read32, memctrl_write32 },  // Memory control
    { 0x1F801070, 0x1F801077, irq_read32, irq_write32 },          // Interrupt controller
    { 0x1F801080, 0x1F8010FF, dma_read32, dma_write32 },          // DMA registers
    { 0x1F801100, 0x1F80112F, timer_read32, timer_write32 },      // Timer registers
    { 0x1F801040, 0x1F80105F, sio_read32, sio_write32 },          // SIO registers
    { 0x1F801800, 0x1F801803, cdrom_read32, cdrom_write32 },      // CDROM registers
    { 0x1F801810, 0x1F80181F, gpu_read32, gpu_write32 },          // GPU registers
    { 0x1F801C00, 0x1F801DFF, spu_read32, spu_write32 },          // SPU registers
    
    // VRAM
    { 0x1F000000, 0x1F1FFFFF, vram_read32, vram_write32 },        // VRAM (2MB)
    
    // Fallback hardware register regions
    { 0x1F801000, 0x1F801FFF, hwreg_read32, hwreg_write32 },      // General hardware registers
    { 0x9F801000, 0x9F801FFF, hwreg_read32, hwreg_write32 },      // Hardware registers (cached)
    { 0xBF801000, 0xBF801FFF, hwreg_read32, hwreg_write32 },      // Hardware registers (uncached)
};

uint32_t nc_interconnect_read32(struct NcInterconnect* inter, uint32_t addr) {
    NC_LOGI("[TRACE] Interconnect read32: addr=0x%08x", addr);
    NC_LOGI("[TRACE] Interconnect pointer: %p", (void*)inter);
    EmulatorContext* ctx = (EmulatorContext*)((char*)inter - offsetof(EmulatorContext, interconnect));
    NC_LOGI("[TRACE] EmulatorContext pointer: %p", (void*)ctx);
    for (int i = 0; i < NUM_REGIONS; ++i) {
        NC_LOGI("[TRACE] Checking region %d: 0x%08x-0x%08x", i, memory_map[i].start, memory_map[i].end);
        if (addr >= memory_map[i].start && addr <= memory_map[i].end) {
            NC_LOGI("[TRACE] Found matching region %d", i);
            return memory_map[i].read32(ctx, addr);
        }
    }
    NC_LOGI("Unmapped read32: addr=0x%08x", addr);
    return 0xFFFFFFFF;
}

void nc_interconnect_write32(struct NcInterconnect* inter, uint32_t addr, uint32_t value) {
    EmulatorContext* ctx = (EmulatorContext*)((char*)inter - offsetof(EmulatorContext, interconnect));
    for (int i = 0; i < NUM_REGIONS; ++i) {
        if (addr >= memory_map[i].start && addr <= memory_map[i].end) {
            memory_map[i].write32(ctx, addr, value);
            return;
        }
    }
    NC_LOGI("Unmapped write32: addr=0x%08x value=0x%08x", addr, value);
} 