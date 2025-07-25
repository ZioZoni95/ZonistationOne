#include "../include/interconnect.h"
#include "../include/log.h"
#include <stdio.h>
#include "../include/emulator.h" // For EmulatorContext cast
#include <stdint.h>
#define MAX_UNMAPPED_REGIONS 32
static uint32_t unmapped_regions[MAX_UNMAPPED_REGIONS];
static int unmapped_region_count = 0;
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
    NC_LOGT("[RAM] read32: addr=0x%08x value=0x%08x", addr, value); // TRACE level only
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
    NC_LOGT("RAM write32: addr=0x%08x value=0x%08x", addr, value); // TRACE level only
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
    uint32_t value = (b0 << 24) | (b1 << 16) | (b2 << 8) | b3;
    NC_LOGI("[BIOS] read32: addr=0x%08x value=0x%08x", addr, value);
    return value;
}
void bios_write32(void* ctx, uint32_t addr, uint32_t value) {
    // BIOS is read-only
    NC_LOGT("BIOS write32 IGNORED: addr=0x%08x value=0x%08x", addr, value);
}

// --- Enhanced hardware register emulation (PCSX ReARMed reference) ---
// IRQ (Interrupt Controller) 0x1F801070-0x1F801077
uint32_t irq_read32(void* ctx, uint32_t addr) {
    EmulatorContext* ectx = (EmulatorContext*)ctx;
    uint32_t offset = addr & 0x7;
    switch (offset) {
        case 0x00: // I_STAT
            NC_LOGI("[IRQ] read I_STAT: 0x%08x", ectx->irq_status);
            return ectx->irq_status;
        case 0x04: // I_MASK
            NC_LOGI("[IRQ] read I_MASK: 0x%08x", ectx->irq_mask);
            return ectx->irq_mask;
        default:
            NC_LOGW("[IRQ] read unknown offset 0x%02x", offset);
            return 0;
    }
}
void irq_write32(void* ctx, uint32_t addr, uint32_t value) {
    EmulatorContext* ectx = (EmulatorContext*)ctx;
    uint32_t offset = addr & 0x7;
    switch (offset) {
        case 0x00: // I_STAT (acknowledge interrupts)
            NC_LOGI("[IRQ] write I_STAT: clear=0x%08x (before=0x%08x)", value, ectx->irq_status);
            ectx->irq_status &= ~(value & 0x7FF); // Only clear bits set in value
            NC_LOGI("[IRQ] write I_STAT: new status=0x%08x", ectx->irq_status);
            break;
        case 0x04: // I_MASK (set mask)
            NC_LOGI("[IRQ] write I_MASK: set=0x%08x (before=0x%08x)", value, ectx->irq_mask);
            ectx->irq_mask = value & 0x7FF;
            NC_LOGI("[IRQ] write I_MASK: new mask=0x%08x", ectx->irq_mask);
            break;
        default:
            NC_LOGW("[IRQ] write unknown offset 0x%02x = 0x%08x", offset, value);
            break;
    }
}
// DMA 0x1F801080-0x1F8010FF (add all documented registers, return plausible values)
uint32_t dma_read32(void* ctx, uint32_t addr) {
    EmulatorContext* ectx = (EmulatorContext*)ctx;
    uint32_t offset = addr & 0x7F;
    // Per-channel registers: 0x00-0x6F (7 channels, 0x10 per channel)
    if (offset < 0x70) {
        int ch = offset / 0x10;
        int reg = offset % 0x10;
        if (ch < 7) {
            switch (reg) {
                case 0x0: // MADR
                    NC_LOGI("[DMA] read CH%d_MADR: 0x%08x", ch, ectx->dma.channels[ch].madr);
                    return ectx->dma.channels[ch].madr;
                case 0x4: // BCR
                    NC_LOGI("[DMA] read CH%d_BCR: 0x%08x", ch, ectx->dma.channels[ch].bcr);
                    return ectx->dma.channels[ch].bcr;
                case 0x8: // CHCR
                    NC_LOGI("[DMA] read CH%d_CHCR: 0x%08x", ch, ectx->dma.channels[ch].control);
                    return ectx->dma.channels[ch].control;
                default:
                    NC_LOGW("[DMA] read unknown CH%d reg 0x%02x", ch, reg);
                    return 0;
            }
        }
    }
    switch (offset) {
        case 0x70: NC_LOGI("[DMA] read CONTROL: 0x%08x", ectx->dma.control); return ectx->dma.control;
        case 0x74: NC_LOGI("[DMA] read INTERRUPT: 0x%08x", ectx->dma.interrupt); return ectx->dma.interrupt;
        default: NC_LOGW("[DMA] read unknown offset 0x%02x", offset); return 0;
    }
}
void dma_write32(void* ctx, uint32_t addr, uint32_t value) {
    EmulatorContext* ectx = (EmulatorContext*)ctx;
    uint32_t offset = addr & 0x7F;
    // Per-channel registers: 0x00-0x6F (7 channels, 0x10 per channel)
    if (offset < 0x70) {
        int ch = offset / 0x10;
        int reg = offset % 0x10;
        if (ch < 7) {
            switch (reg) {
                case 0x0: // MADR
                    NC_LOGI("[DMA] write CH%d_MADR: 0x%08x (before=0x%08x)", ch, value, ectx->dma.channels[ch].madr);
                    ectx->dma.channels[ch].madr = value;
                    break;
                case 0x4: // BCR
                    NC_LOGI("[DMA] write CH%d_BCR: 0x%08x (before=0x%08x)", ch, value, ectx->dma.channels[ch].bcr);
                    ectx->dma.channels[ch].bcr = value;
                    break;
                case 0x8: // CHCR
                    NC_LOGI("[DMA] write CH%d_CHCR: 0x%08x (before=0x%08x)", ch, value, ectx->dma.channels[ch].control);
                    ectx->dma.channels[ch].control = value;
                    break;
                default:
                    NC_LOGW("[DMA] write unknown CH%d reg 0x%02x = 0x%08x", ch, reg, value);
                    break;
            }
            return;
        }
    }
    switch (offset) {
        case 0x70: NC_LOGI("[DMA] write CONTROL: 0x%08x (before=0x%08x)", value, ectx->dma.control); ectx->dma.control = value; break;
        case 0x74: NC_LOGI("[DMA] write INTERRUPT: 0x%08x (before=0x%08x)", value, ectx->dma.interrupt); ectx->dma.interrupt = value; break;
        default: NC_LOGW("[DMA] write unknown offset 0x%02x = 0x%08x", offset, value); break;
    }
}
// Timers 0x1F801100-0x1F80112F (simulate incrementing counters, plausible modes/targets)
uint32_t timer_read32(void* ctx, uint32_t addr) {
    EmulatorContext* ectx = (EmulatorContext*)ctx;
    uint32_t offset = addr & 0x2F;
    switch (offset) {
        case 0x00: case 0x10: case 0x20: // Timer counters
            return (ectx->cycle_count / 1000) & 0xFFFF;
        case 0x04: case 0x14: case 0x24: // Timer modes
            return 0x0000;
        case 0x08: case 0x18: case 0x28: // Timer targets
            return 0xFFFF;
        default:
            return 0;
    }
}
void timer_write32(void* ctx, uint32_t addr, uint32_t value) {
    EmulatorContext* ectx = (EmulatorContext*)ctx;
    uint32_t offset = addr & 0x2F;
    // For now, just log the write and optionally update a dummy state
    NC_LOGI("[TIMER] write32: addr=0x%08x value=0x%08x", addr, value);
    // TODO: Implement real timer state update if needed
}
// SPU 0x1F801C00-0x1F801DFF (return plausible values for status/control)
uint32_t spu_read32(void* ctx, uint32_t addr) {
    uint32_t offset = addr & 0x1FF;
    switch (offset) {
        case 0x1AA: return 0x0000; // SPU Status
        case 0x1AC: return 0x0000; // SPU Control
        default: return 0;
    }
}
void spu_write32(void* ctx, uint32_t addr, uint32_t value) {
    // For now, just log the write
    NC_LOGI("[SPU] write32: addr=0x%08x value=0x%08x", addr, value);
    // TODO: Implement real SPU state update if needed
}
// GPU 0x1F801810-0x1F80181F (GPUREAD, GPUSTAT)
uint32_t gpu_read32(void* ctx, uint32_t addr) {
    uint32_t offset = addr & 0x1F;
    switch (offset) {
        case 0x00: return 0; // GPUREAD (TODO: implement)
        case 0x04: return 0x1C000000; // GPUSTAT (ready, DMA idle)
        default: return 0;
    }
}
void gpu_write32(void* ctx, uint32_t addr, uint32_t value) {
    uint32_t offset = addr & 0x1F;
    switch (offset) {
        case 0x00: /* GP0 */ break; // TODO: Implement GPU command
        case 0x04: /* GP1 */ break; // TODO: Implement GPU control
        default: break;
    }
}
// CDROM 0x1F801800-0x1F801803 (return plausible values)
uint32_t cdrom_read32(void* ctx, uint32_t addr) {
    uint32_t offset = addr & 0x3;
    switch (offset) {
        case 0x00: return 0x2E; // Status: drive present, ready (bit 0=busy=0, bit 1=ready=1, bit 5=seek error=0, bit 6=seek complete=1)
        case 0x01: return 0x00; // Response
        case 0x02: return 0x00; // Flag
        case 0x03: return 0x00; // Data
        default: return 0;
    }
}
void cdrom_write32(void* ctx, uint32_t addr, uint32_t value) {
    // For now, just log the write
    NC_LOGI("[CDROM] write32: addr=0x%08x value=0x%08x", addr, value);
    // TODO: Implement real CDROM state update if needed
}
// SIO 0x1F801040-0x1F80105F (return plausible values)
uint32_t sio_read32(void* ctx, uint32_t addr) {
    uint32_t offset = addr & 0x1F;
    switch (offset) {
        case 0x00: return 0xFF;   // JOY_DATA: controller present, idle
        case 0x04: return 0x0024; // JOY_STAT: TX ready, RX ready, no error (bit 2=TX ready, bit 5=RX ready)
        case 0x08: return 0x0000; // JOY_MODE
        case 0x0A: return 0x0001; // JOY_CTRL: SIO enabled
        case 0x0C: return 0x0000; // JOY_BAUD
        default: return 0;
    }
}
void sio_write32(void* ctx, uint32_t addr, uint32_t value) {
    // For now, just log the write
    NC_LOGI("[SIO] write32: addr=0x%08x value=0x%08x", addr, value);
    // TODO: Implement real SIO state update if needed
}
// MemCtrl 0x1F801000-0x1F80107F (return plausible values)
uint32_t memctrl_read32(void* ctx, uint32_t addr) {
    uint32_t offset = addr & 0x7F;
    switch (offset) {
        case 0x00: return 0x1F000000; // Expansion 1 Base
        case 0x04: return 0x1F802000; // Expansion 2 Base
        case 0x60: return 0x00000000; // RAM Size (2MB)
        default: return 0;
    }
}
void memctrl_write32(void* ctx, uint32_t addr, uint32_t value) {
    // For now, just log the write
    NC_LOGI("[MEMCTRL] write32: addr=0x%08x value=0x%08x", addr, value);
    // TODO: Implement real MemCtrl state update if needed
}

// Hardware register region handler (fallback for unmapped registers)
// Add static to track last unmapped region and count

// Unmapped warning suppression: only log the first access per 16MB region

uint32_t hwreg_read32(void* ctx, uint32_t addr) {
    // Suppress logging for expansion region probes; always return 0xFFFFFFFF for unmapped
    (void)ctx; (void)addr;
    return 0xFFFFFFFF;
}
void hwreg_write32(void* ctx, uint32_t addr, uint32_t value) {
    NC_LOGW("[HWREG] Write32 to 0x%08x = 0x%08x (stub)", addr, value);
    static FILE* reglog = NULL;
    if (!reglog) reglog = fopen("./hwreg_access.log", "a");
    if (reglog) fprintf(reglog, "WRITE 0x%08x = 0x%08x\n", addr, value);
}

// VRAM region handlers (minimal stub)
uint32_t vram_read32(void* ctx, uint32_t addr) {
    EmulatorContext* ectx = (EmulatorContext*)ctx;
    uint32_t offset = addr & (NC_VRAM_SIZE - 1);
    uint32_t value = nc_vram_load32(&ectx->gpu.vram, offset);
    NC_LOGT("VRAM read32: addr=0x%08x offset=0x%06x value=0x%08x", addr, offset, value);
    return value;
}
void vram_write32(void* ctx, uint32_t addr, uint32_t value) {
    EmulatorContext* ectx = (EmulatorContext*)ctx;
    uint32_t offset = addr & (NC_VRAM_SIZE - 1);
    nc_vram_store32(&ectx->gpu.vram, offset, value);
    NC_LOGT("VRAM write32: addr=0x%08x offset=0x%06x value=0x%08x", addr, offset, value);
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
}

// Add a handler for 0xb4800000–0xb480ffff that returns 0xFFFFFFFF for all reads
uint32_t expansionb4_read32(void* ctx, uint32_t addr) {
    // Patch for BIOS probe: return JR $RA at 0xb48020f0, NOP at 0xb48020f4
    if (addr == 0xb48020f0) return 0x03E00008; // JR $RA
    if (addr == 0xb48020f4) return 0x00000000; // NOP
    (void)ctx; (void)addr;
    return 0xFFFFFFFF;
}
void expansionb4_write32(void* ctx, uint32_t addr, uint32_t value) {
    (void)ctx; (void)addr; (void)value;
    // Ignore writes
}

// Handler for 0xbc000000–0xbcffffff (expansion region, returns 0xFFFFFFFF)
uint32_t expansionbc_read32(void* ctx, uint32_t addr) {
    (void)ctx; (void)addr;
    return 0xFFFFFFFF;
}
void expansionbc_write32(void* ctx, uint32_t addr, uint32_t value) {
    (void)ctx; (void)addr; (void)value;
    // Ignore writes
}

// Memory region table - Expanded based on pcsx_rearmed_reference
#undef NUM_REGIONS
#define NUM_REGIONS 30
static NcMemRegion memory_map[NUM_REGIONS] = {
    // Main RAM and mirrors
    { 0x00000000, 0x001FFFFF, ram_read32, ram_write32 },      // RAM (2MB)
    { 0x80000000, 0x801FFFFF, ram_read32, ram_write32 },      // RAM alias (KSEG0)
    { 0xA0000000, 0xA01FFFFF, ram_read32, ram_write32 },      // RAM alias (KSEG1)
    { 0x00200000, 0x003FFFFF, ram_read32, ram_write32 },      // RAM mirror
    { 0x00400000, 0x005FFFFF, ram_read32, ram_write32 },      // RAM mirror
    { 0x00600000, 0x007FFFFF, ram_read32, ram_write32 },      // RAM mirror

    // Parallel port and mirror
    { 0x1F000000, 0x1F00FFFF, hwreg_read32, hwreg_write32 },  // Parallel port
    { 0x1FA00000, 0x1FA0FFFF, hwreg_read32, hwreg_write32 },  // Parallel port mirror (PS2/PS3 BIOS)

    // BIOS and mirrors
    { 0x1FC00000, 0x1FC7FFFF, bios_read32, bios_write32 },    // BIOS (512KB)
    { 0xBFC00000, 0xBFC7FFFF, bios_read32, bios_write32 },    // BIOS alias (reset vector)
    { 0x9FC00000, 0x9FC7FFFF, bios_read32, bios_write32 },    // BIOS alias (cached)

    // Scratchpad and mirrors
    { 0x1F800000, 0x1F8003FF, scratchpad_read32, scratchpad_write32 },
    { 0x9F800000, 0x9F8003FF, scratchpad_read32, scratchpad_write32 },

    // Hardware registers (main and mirrors)
    { 0x1F801000, 0x1F801FFF, hwreg_read32, hwreg_write32 },  // Hardware registers
    { 0x9F801000, 0x9F801FFF, hwreg_read32, hwreg_write32 },  // Hardware registers (cached)
    { 0xBF801000, 0xBF801FFF, hwreg_read32, hwreg_write32 },  // Hardware registers (uncached)

    // Subsystem-specific hardware registers
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

    // Expansion regions (must be before fallback hardware regions)
    { 0xB0000000, 0xB0FFFFFF, hwreg_read32, hwreg_write32 },
    { 0xB4800000, 0xB480FFFF, expansionb4_read32, expansionb4_write32 },
    { 0xBC000000, 0xBCFFFFFF, expansionbc_read32, expansionbc_write32 },
};

uint32_t nc_interconnect_read32(struct NcInterconnect* inter, uint32_t addr) {
    NC_LOGT("[TRACE] Interconnect read32: addr=0x%08x", addr);
    NC_LOGT("[TRACE] Interconnect pointer: %p", (void*)inter);
    EmulatorContext* ctx = (EmulatorContext*)((char*)inter - offsetof(EmulatorContext, interconnect));
    NC_LOGT("[TRACE] EmulatorContext pointer: %p", (void*)ctx);
    for (int i = 0; i < NUM_REGIONS; ++i) {
        NC_LOGT("[TRACE] Checking region %d: 0x%08x-0x%08x", i, memory_map[i].start, memory_map[i].end);
        if (addr >= memory_map[i].start && addr <= memory_map[i].end) {
            NC_LOGT("[TRACE] Found matching region %d", i);
            return memory_map[i].read32(ctx, addr);
        }
    }
    NC_LOGW("[UNMAPPED] read32: addr=0x%08x (returns 0)", addr);
    return 0; // Return 0 for unmapped regions (PCSX ReARMed behavior)
}

void nc_interconnect_write32(struct NcInterconnect* inter, uint32_t addr, uint32_t value) {
    EmulatorContext* ctx = (EmulatorContext*)((char*)inter - offsetof(EmulatorContext, interconnect));
    for (int i = 0; i < NUM_REGIONS; ++i) {
        if (addr >= memory_map[i].start && addr <= memory_map[i].end) {
            memory_map[i].write32(ctx, addr, value);
            return;
        }
    }
    NC_LOGT("Unmapped write32: addr=0x%08x value=0x%08x", addr, value);
} 