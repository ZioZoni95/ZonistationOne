#include "interconnect.h" // Includes associated header and headers for components (gpu.h, dma.h etc.)
#include <stdio.h>
#include <stdbool.h>
#include "log.h"

// At the top, after #include "log.h":
#ifndef LOG_DMA_INFO
#define LOG_DMA_INFO(...)   log_component("dma", LOG_LEVEL_INFO, __VA_ARGS__)
#define LOG_DMA_DEBUG(...)  log_component("dma", LOG_LEVEL_DEBUG, __VA_ARGS__)
#define LOG_DMA_TRACE(...)  log_component("dma", LOG_LEVEL_TRACE, __VA_ARGS__)
#define LOG_DMA_WARN(...)   log_component("dma", LOG_LEVEL_WARN, __VA_ARGS__)
#define LOG_DMA_ERROR(...)  log_component("dma", LOG_LEVEL_ERROR, __VA_ARGS__)
#endif

// DMA and GPU region access logs are extremely frequent and only useful for deep debugging.
// Suppress at INFO/DEBUG, only log at TRACE, and rate-limit. Keep summary/activation logs at INFO/DEBUG.
static uint64_t dma_read32_count = 0;
static uint32_t last_dma_read32_addr = 0;
static uint32_t last_dma_read32_offset = 0;
static uint64_t dma_write32_count = 0;
static uint32_t last_dma_write32_addr = 0;
static uint32_t last_dma_write32_offset = 0;
static uint32_t last_dma_write32_value = 0;

// --- Rate-limited log counters for IO accesses ---
#if LOG_LEVEL >= LOG_LEVEL_INFO
static int io_read32_count = 0;
static int io_read16_count = 0;
static int io_read8_count = 0;
static int io_write32_count = 0;
static int io_write16_count = 0;
static int io_write8_count = 0;
static int cdrom_read8_count = 0;
static int cdrom_write8_count = 0;
#endif

// --- Memory Region Masking ---
// Array mapping the top 3 bits of a virtual address to a mask
// used to convert KUSEG/KSEG0/KSEG1 addresses to physical addresses.
// KSEG2 addresses are not masked. Based on Guide Section 2.38[cite: 512].
const uint32_t REGION_MASK[8] = {
    0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff, // KUSEG (0x00000000 - 0x7FFFFFFF) - No mask
    0x7fffffff,                                     // KSEG0 (0x80000000 - 0x9FFFFFFF) - Mask top bit
    0x1fffffff,                                     // KSEG1 (0xA0000000 - 0xBFFFFFFF) - Mask top 3 bits
    0xffffffff, 0xffffffff                          // KSEG2 (0xC0000000 - 0xFFFFFFFF) - No mask
};

/**
 * @brief Maps a CPU virtual address to a physical address by masking region bits.
 * KSEG2 addresses are returned unchanged.
 * @param addr The 32-bit virtual address.
 * @return The 32-bit physical address.
 */
uint32_t mask_region(uint32_t addr) {
    size_t index = (addr >> 29) & 0x7;
    uint32_t paddr = addr & REGION_MASK[index];
    static int mask_debug_count = 0;
    if (mask_debug_count < 1000) {
        LOG_INTERCONNECT_DEBUG("mask_region: vaddr=0x%08X -> paddr=0x%08X\n", addr, paddr);
        mask_debug_count++;
    }
    return paddr;
}

static void interconnect_perform_dma(Interconnect* inter, uint32_t channel_index);

// --- Initialization ---
/**
 * @brief Initializes the Interconnect struct.
 * Assigns component pointers, initializes embedded structs (GPU, DMA),
 * and resets interrupt controller state.
 * @param inter Pointer to the Interconnect struct.
 * @param bios Pointer to the loaded Bios struct.
 * @param ram Pointer to the initialized Ram struct.
 */
void interconnect_init(Interconnect* inter, Bios* bios, Ram* ram) {
    LOG_INTERCONNECT_INFO("Interconnect initialized");
    inter->bios = bios;
    inter->ram = ram;
    dma_init(&inter->dma); // Initialize DMA controller state
    gpu_init_full(&inter->gpu, inter); // Initialize GPU state (now contains Renderer)


    cdrom_init(&inter->cdrom,inter);

    // Initialize Interrupt Controller state
    inter->irq_status = 0; // No pending interrupts
    inter->irq_mask = 0x0000;   // Start with all interrupts masked, BIOS will enable as needed
    
    // Initialize Timer state <<< ADD THIS CALL
    timers_init(&inter->timers_state, inter);
    
    LOG_INTERCONNECT_INFO("Interconnect Initialized (BIOS, RAM, DMA, GPU, CDROM, IRQ states set).\n");
}


// --- Peripheral Interrupt Request ---
/**
 * @brief Allows peripherals to signal an interrupt request.
 * Sets the corresponding bit in the I_STAT register (irq_status).
 * Used by peripherals (e.g., CDROM) to request IRQ2. Logs the source.
 * @param inter Pointer to the Interconnect instance.
 * @param irq_line The interrupt line number (0-10).
 * @param source The source of the interrupt request.
 */
void interconnect_request_irq(Interconnect* inter, uint32_t irq_line, const char* source) {
    (void)source;
    uint16_t prev = inter->irq_status;
    inter->irq_status |= (1 << irq_line);
    if (irq_line == 0) {
        LOG_INTERCONNECT_INFO("[IRQ] IRQ0 requested by %s (I_STAT: 0x%04x -> 0x%04x)", source, prev, inter->irq_status);
    }
    if (irq_line <= 6) {
        LOG_INTERCONNECT_INFO("[IRQ] IRQ%u requested\n", irq_line);
    }
}

// Helper to clear an IRQ (for explicit logging, though BIOS usually does this via I_STAT write)
void interconnect_clear_irq(Interconnect* inter, uint32_t irq_line, const char* source) {
    (void)source;
    inter->irq_status &= ~(1 << irq_line);
}


// --- Load Operations ---

/**
 * @brief Handles 32-bit memory reads from the CPU.
 * @param inter The Interconnect instance.
 * @param address Virtual address to read from.
 * @return The 32-bit value read.
 */
uint32_t interconnect_load32(Interconnect* inter, uint32_t address) {
    uint32_t physical_addr = mask_region(address);
    LOG_INTERCONNECT_TRACE("[INTERCONNECT] IO READ32 at 0x%08x\n", physical_addr);
#if LOG_LEVEL >= LOG_LEVEL_INFO
    if (physical_addr >= 0x1f801000 && physical_addr < 0x1f802000) {
        if (++io_read32_count % 10000 == 0) {
            LOG_INTERCONNECT_INFO("[INTERCONNECT] IO READ32: %d accesses, last at 0x%08x\n", io_read32_count, physical_addr);
        }
    }
#endif
    // CDROM 32-bit access logging
    if (physical_addr >= 0x1f801800 && physical_addr <= 0x1f801803) {
        LOG_INTERCONNECT_INFO("[INTERCONNECT] CDROM register READ32 at 0x%08x (UNEXPECTED SIZE)\n", physical_addr);
    }
    // Check for 32-bit alignment (Word access)
    if (address % 4 != 0) {
        // TODO: This should trigger an Address Error Load exception in the CPU.
        LOG_INTERCONNECT_ERROR("Unaligned load32 address: 0x%08x\n", address);
        // For now, just return a garbage value, but an exception is correct.
        return 0xBADBAD32; // Placeholder for unaligned access
    }

    // --- Hardware Register Checks (Specific Addresses First) ---

// --- Check Timer Range --- <<< ADD THIS BLOCK
    if (physical_addr >= TIMERS_START && physical_addr <= TIMERS_END) {
        uint32_t timer_base_offset = physical_addr - TIMERS_START;
        int timer_index = timer_base_offset / 0x10; // Each timer block is 0x10 bytes wide
        uint32_t register_offset = physical_addr & 0xF; // Offset within the timer block (0, 4, 8)

        // LOG_INFO("~ Read32 from Timer %d Offset 0x%x\n", timer_index, register_offset);
        return timer_read32(&inter->timers_state, timer_index, register_offset);
    }
    
    // Interrupt Controller Registers
    if (physical_addr == IRQ_STATUS_ADDR) { // 0x1f801070 (I_STAT)
        LOG_INTERCONNECT_INFO("~ Read32 from IRQ_STATUS (0x1f801070): Returning 0x%04x\n", inter->irq_status);
        return (uint32_t)inter->irq_status;
    }
    if (physical_addr == IRQ_MASK_ADDR) { // 0x1f801074 (I_MASK)
        LOG_INTERCONNECT_INFO("~ Read32 from IRQ_MASK (0x1f801074): Returning 0x%04x\n", inter->irq_mask);
        return (uint32_t)inter->irq_mask;
    }

    // GPU Registers
    if (physical_addr == GPU_GPUREAD_ADDR) { // 0x1f801810 (Read = GPUREAD)
        // Reading GPUREAD should return data from VRAM transfers or command responses
        LOG_INTERCONNECT_INFO("~ Read32 from GPUREAD (0x1f801810)\n");
        return gpu_read_data(&inter->gpu);
    }
    if (physical_addr == GPU_GPUSTAT_ADDR) { // 0x1f801814 (Read = GPUSTAT)
        // Reading GPUSTAT returns the GPU status flags
        // LOG_INFO("~ Read32 from GPUSTAT (0x1f801814)\n"); // Often read, can be noisy
        return gpu_read_status(&inter->gpu);
    }

    // Timer Registers (Example: Read Timer 1 Counter)
    if (physical_addr == 0x1f801110) { // Timer 1 Counter Value (T1_COUNT)
         // TODO: Implement actual Timer read logic
         LOG_INTERCONNECT_INFO("~ Read32 from Timer 1 Counter (0x1f801110): Returning 0 (Placeholder)\n");
         return 0;
    }
    // Add reads for other Timer counters/modes/targets if needed


    // --- Region Checks (Broader Ranges) ---

    // DMA Region (0x1f801080 - 0x1f8010FF)
    if (physical_addr >= DMA_START && physical_addr <= DMA_END) {
        uint32_t offset = physical_addr - DMA_START;
        dma_read32_count++;
        if (log_get_level() >= LOG_LEVEL_TRACE) {
            if ((dma_read32_count % 1000 == 0) || (physical_addr != last_dma_read32_addr) || (offset != last_dma_read32_offset)) {
                LOG_DMA_TRACE("~ Read32 from DMA region: Addr=0x%08x Offset=0x%x (count=%llu)", physical_addr, offset, dma_read32_count);
                last_dma_read32_addr = physical_addr;
                last_dma_read32_offset = offset;
            }
        }
        return dma_read(&inter->dma, offset); // Delegate to DMA module
    }

    // BIOS Region (0x1fc00000 - 0x1fc7ffff)
    if (physical_addr >= BIOS_START && physical_addr <= BIOS_END) {
        uint32_t offset = physical_addr - BIOS_START;
        static int bios_load32_debug_count = 0;
        if (bios_load32_debug_count < 1000) {
            LOG_INTERCONNECT_DEBUG("interconnect_load32: vaddr=0x%08X paddr=0x%08X BIOS offset=0x%X\n", address, physical_addr, offset);
            bios_load32_debug_count++;
        }
        return bios_load32(inter->bios, offset); // Delegate to BIOS module
    }

    // Main RAM Region (0x00000000 - 0x001fffff)
    if (physical_addr <= RAM_END) {
        return ram_load32(inter->ram, physical_addr); // Delegate to RAM module
    }

    // Timer Region (General Check - 0x1f801100 - 0x1f80112F)
    if (physical_addr >= TIMERS_START && physical_addr <= TIMERS_END) {
        LOG_INTERCONNECT_WARN("Warning: Unhandled Timer read32 at 0x%08x\n", physical_addr);
        return 0; // Return 0 for unhandled timer reads
    }

    // SPU Region (0x1f801C00 - 0x1f801E7F)
    if (physical_addr >= SPU_START && physical_addr <= SPU_END) {
         // LOG_INFO("~ Read32 from SPU region: Address 0x%08x (Ignoring, returning 0)\n", physical_addr); // Often noisy
         return 0; // SPU not implemented yet
    }

    // Expansion 1 Region (0x1f000000 - 0x1f7fffff)
    if (physical_addr >= EXPANSION_1_START && physical_addr <= EXPANSION_1_END) {
         LOG_INTERCONNECT_INFO("~ Read32 from Expansion 1 region: Address 0x%08x (Returning 0xFFFFFFFF)\n", physical_addr);
         return 0xFFFFFFFF; // Expansion 1 returns all Fs when empty
    }

    // VRAM Region (0x1F000000 - 0x1F7FFFFF)
    if (physical_addr >= 0x1F000000 && physical_addr <= 0x1F7FFFFF) {
        LOG_INTERCONNECT_INFO("~ Read32 from VRAM region: Address 0x%08x (Returning 0xFFFFFFFF as open bus)\n", physical_addr);
        return 0xFFFFFFFF; // Open bus for unimplemented VRAM
    }

    // --- Fallback for Unhandled Addresses ---
    if ((physical_addr & 0xFFFF0000) == 0xFFFF0000) {
        // KSEG2 region: return 0 for unmapped addresses (per nocash/PSX-Spex)
        return 0;
    }
    LOG_INTERCONNECT_ERROR("Unhandled physical memory read32 at address: 0x%08x (Mapped from 0x%08x)\n",
            physical_addr, address);
    return 0; // Or a more distinct "garbage" value like 0xDEADBEEF
}

/**
 * @brief Handles 16-bit memory reads from the CPU.
 * @param inter The Interconnect instance.
 * @param address Virtual address to read from.
 * @return The 16-bit value read.
 */
uint16_t interconnect_load16(Interconnect* inter, uint32_t address) {
    uint32_t physical_addr = mask_region(address);
    LOG_INTERCONNECT_TRACE("[INTERCONNECT] IO READ16 at 0x%08x\n", physical_addr);
#if LOG_LEVEL >= LOG_LEVEL_INFO
    if (physical_addr >= 0x1f801000 && physical_addr < 0x1f802000) {
        if (++io_read16_count % 10000 == 0) {
            LOG_INTERCONNECT_INFO("[INTERCONNECT] IO READ16: %d accesses, last at 0x%08x\n", io_read16_count, physical_addr);
        }
    }
#endif
    // CDROM 16-bit access logging
    if (physical_addr >= 0x1f801800 && physical_addr <= 0x1f801803) {
        LOG_INTERCONNECT_INFO("[INTERCONNECT] CDROM register READ16 at 0x%08x (UNEXPECTED SIZE)\n", physical_addr);
    }
     // Check for 16-bit alignment (Halfword access)
     if (address % 2 != 0) {
        // TODO: Trigger Address Error Load exception
        LOG_INTERCONNECT_ERROR("Unaligned load16 address: 0x%08x\n", address);
        return 0xBADB; // Placeholder
    }
// --- Check Timer Range --- <<< ADD THIS BLOCK
    if (physical_addr >= TIMERS_START && physical_addr <= TIMERS_END) {
        uint32_t timer_base_offset = physical_addr - TIMERS_START;
        int timer_index = timer_base_offset / 0x10;
        uint32_t register_offset = physical_addr & 0xF;

        // LOG_INFO("~ Read16 from Timer %d Offset 0x%x\n", timer_index, register_offset);
        return timer_read16(&inter->timers_state, timer_index, register_offset);
    }

    // Interrupt Controller Registers
    if (physical_addr == IRQ_STATUS_ADDR) { // 0x1f801070 (I_STAT)
        LOG_INTERCONNECT_INFO("~ Read16 from IRQ_STATUS (0x1f801070): Returning 0x%04x\n", inter->irq_status);
        return inter->irq_status;
    }
     if (physical_addr == IRQ_MASK_ADDR) { // 0x1f801074 (I_MASK)
        LOG_INTERCONNECT_INFO("~ Read16 from IRQ_MASK (0x1f801074): Returning 0x%04x\n", inter->irq_mask);
        return inter->irq_mask;
     }

    // SPU Region (Reads usually return 0 or specific status)
    if (physical_addr >= SPU_START && physical_addr <= SPU_END) {
         // LOG_INFO("~ Read16 from SPU region: Address 0x%08x (Ignoring, returning 0)\n", physical_addr); // Often noisy
         return 0; // SPU reads often ignored initially
    }

    // Timer Region (Check specific registers if needed)
     if (physical_addr >= TIMERS_START && physical_addr <= TIMERS_END) {
        // Handle specific 16-bit Timer reads (Counter, Mode, Target)
        // Example:
        // if (physical_addr == 0x1F801100) return timer0_get_count();
        LOG_INTERCONNECT_WARN("Warning: Unhandled Timer read16 at 0x%08x\n", physical_addr);
        return 0;
    }

    // Main RAM Region
    if (physical_addr <= RAM_END) {
        // (Removed RAM Read/Write logs for performance)
        return ram_load16(inter->ram, physical_addr);
    }

    // BIOS Region (Unlikely, but check)
     if (physical_addr >= BIOS_START && physical_addr <= BIOS_END) {
        // (Removed BIOS ROM Read logs for performance)
        return bios_load16(inter->bios, physical_addr - BIOS_START);
    }

    // GPU Region (Unlikely 16-bit reads)
    if (physical_addr >= GPU_START && physical_addr <= GPU_END) {
         LOG_INTERCONNECT_WARN("Warning: Unhandled GPU read16 at 0x%08x\n", physical_addr);
         return 0;
    }

    // DMA Region (Unlikely 16-bit reads)
     if (physical_addr >= DMA_START && physical_addr <= DMA_END) {
        LOG_INTERCONNECT_WARN("Warning: Unhandled DMA read16 at 0x%08x\n", physical_addr);
        return 0;
    }

    // Expansion 1 Region
    if (physical_addr >= EXPANSION_1_START && physical_addr <= EXPANSION_1_END) {
         LOG_INTERCONNECT_INFO("~ Read16 from Expansion 1 region: Address 0x%08x (Returning 0xFFFF)\n", physical_addr);
         return 0xFFFF; // Expansion 1 returns all Fs when empty
    }

    // --- Fallback ---
    if ((physical_addr & 0xFFFF0000) == 0xFFFF0000) {
        if (physical_addr == 0x1f801040) return 0xFF; // JOY_DATA: controller present, idle
        if (physical_addr == 0x1f801044) return 0x00; // JOY_STAT: ready
        if (physical_addr == 0x1f80104a) return 0x00; // JOY_CTRL: default
        if (physical_addr == 0x1f801041) return 0xFF; // JOY_DATA2: memory card present, idle
        if (physical_addr == 0x1f801045) return 0x00; // JOY_STAT2: ready
        if (physical_addr == 0x1f80104b) return 0x00; // JOY_CTRL2: default
        return 0;
    }
    LOG_INTERCONNECT_ERROR("Unhandled physical memory read16 at address: 0x%08x (Mapped from 0x%08x)\n", physical_addr, address);
    return 0;
}

/**
 * @brief Handles 8-bit memory reads from the CPU.
 * @param inter The Interconnect instance.
 * @param address Virtual address to read from.
 * @return The 8-bit value read.
 */
uint8_t interconnect_load8(Interconnect* inter, uint32_t address) {
    uint32_t physical_addr = mask_region(address);
    LOG_INTERCONNECT_TRACE("[INTERCONNECT] IO READ8 at 0x%08x\n", physical_addr);
#if LOG_LEVEL >= LOG_LEVEL_INFO
    if (physical_addr >= 0x1f801000 && physical_addr < 0x1f802000) {
        if (++io_read8_count % 10000 == 0) {
            LOG_INTERCONNECT_INFO("[INTERCONNECT] IO READ8: %d accesses, last at 0x%08x\n", io_read8_count, physical_addr);
        }
    }
#endif
    // --- Check Timer Range --- <<< ADD THIS BLOCK
    if (physical_addr >= TIMERS_START && physical_addr <= TIMERS_END) {
        // 8-bit reads from timers are generally undefined or read partial registers.
        LOG_INTERCONNECT_WARN("Warning: Unhandled 8-bit read from Timer range: 0x%08x\n", physical_addr);
        return 0; // Return 0 for safety
    }
    // --- CDROM Register Access (Strict PSX-Spex) ---
    if (physical_addr >= 0x1f801800 && physical_addr <= 0x1f801803) {
        LOG_INTERCONNECT_DEBUG("[INTERCONNECT] CDROM register READ8 at 0x%08x\n", physical_addr);
#if LOG_LEVEL >= LOG_LEVEL_INFO
        if (++cdrom_read8_count % 10000 == 0) {
            LOG_INTERCONNECT_INFO("[INTERCONNECT] CDROM register READ8: %d accesses, last at 0x%08x\n", cdrom_read8_count, physical_addr);
        }
#endif
        return cdrom_read_register(&inter->cdrom, physical_addr);
    }

    // Expansion 1 Region
     if (physical_addr >= EXPANSION_1_START && physical_addr <= EXPANSION_1_END) {
         // Expansion 1 is used for parallel port devices. Reading when empty returns 0xFF. [cite: 575]
         // LOG_INFO("~ Read8 from Expansion 1 region: Address 0x%08x (Returning 0xFF)\n", physical_addr); // Noisy
         return 0xFF;
     }

    // BIOS Region
     if (physical_addr >= BIOS_START && physical_addr <= BIOS_END) {
        uint32_t offset = physical_addr - BIOS_START;
        if (offset < BIOS_SIZE) {
             // Implement bios_load8 if needed, or read directly:
             // LOG_INFO("~ Read8 from BIOS: Addr=0x%08x Offset=0x%x\n", physical_addr, offset); // Noisy
             return inter->bios->data[offset];
        } else {
             LOG_INTERCONNECT_ERROR("BIOS Load8 out of bounds: offset 0x%x\n", offset);
             return 0; // Error
        }
    }

    // Main RAM Region
    if (physical_addr <= RAM_END) {
        // (Removed RAM Read/Write logs for performance)
        return ram_load8(inter->ram, physical_addr);
    }

    // Other regions (SPU, Timers, GPU, DMA, Exp2, MemCtrl) are less likely for 8-bit reads

    // --- Fallback ---
    if ((physical_addr & 0xFFFF0000) == 0xFFFF0000) {
        if (physical_addr == 0x1f801040) return 0xFF; // JOY_DATA: controller present, idle
        if (physical_addr == 0x1f801044) return 0x00; // JOY_STAT: ready
        if (physical_addr == 0x1f80104a) return 0x00; // JOY_CTRL: default
        if (physical_addr == 0x1f801041) return 0xFF; // JOY_DATA2: memory card present, idle
        if (physical_addr == 0x1f801045) return 0x00; // JOY_STAT2: ready
        if (physical_addr == 0x1f80104b) return 0x00; // JOY_CTRL2: default
        return 0;
    }
    LOG_INTERCONNECT_ERROR("Unhandled physical memory read8 at address: 0x%08x (Mapped from 0x%08x)\n", physical_addr, address);
    return 0;
}


// --- Store Operations ---

/**
 * @brief Handles 32-bit memory writes from the CPU.
 * @param inter The Interconnect instance.
 * @param address Virtual address to write to.
 * @param value The 32-bit value to write.
 */
void interconnect_store32(Interconnect* inter, uint32_t address, uint32_t value) {
    uint32_t physical_addr = mask_region(address);
    // Remove or comment out noisy IO write logs for general IO region
    // LOG_TRACE("[INTERCONNECT] IO WRITE32 at 0x%08x: value=0x%08x\n", physical_addr, value);
#if LOG_LEVEL >= LOG_LEVEL_INFO
    // Suppress general IO write logs for 0x1f801000 - 0x1f802000
    // if (physical_addr >= 0x1f801000 && physical_addr < 0x1f802000) {
    //     if (++io_write32_count % 10000 == 0) {
    //         LOG_INFO("[INTERCONNECT] IO WRITE32: %d accesses, last at 0x%08x\n", io_write32_count, physical_addr);
    //     }
    // }
#endif
    // CDROM 32-bit access logging
    if (physical_addr >= 0x1f801800 && physical_addr <= 0x1f801803) {
        LOG_INTERCONNECT_INFO("[INTERCONNECT] CDROM register WRITE32 at 0x%08x = 0x%08x (UNEXPECTED SIZE)\n", physical_addr, value);
    }
    // Check alignment
    if (address % 4 != 0) {
        // TODO: Trigger Address Error Store exception
        LOG_INTERCONNECT_ERROR("Unaligned store32 address: 0x%08x = 0x%08x\n", address, value);
        return;
    }

    // --- Check Timer Range --- <<< ADD THIS BLOCK
    if (physical_addr >= TIMERS_START && physical_addr <= TIMERS_END) {
        uint32_t timer_base_offset = physical_addr - TIMERS_START;
        int timer_index = timer_base_offset / 0x10;
        uint32_t register_offset = physical_addr & 0xF;

        // LOG_INFO("~ Write32 to Timer %d Offset 0x%x = 0x%08x\n", timer_index, register_offset, value);
        timer_write32(&inter->timers_state, timer_index, register_offset, value);
        return; // Handled
    }
    // --- Hardware Register Checks (Specific Addresses First) ---

    // Interrupt Controller Registers
    if (physical_addr == IRQ_STATUS_ADDR) { // 0x1f801070 (I_STAT)
        LOG_DEBUG("[IRQ] Write to I_STAT (IRQ_STATUS_ADDR): Value=0x%04x, Before=0x%04x", value, inter->irq_status);
        // Clear bits in irq_status that are set in value
        inter->irq_status &= ~(value & 0xFFFF);
        LOG_DEBUG("[IRQ] I_STAT after clear: 0x%04x", inter->irq_status);
        return;
    }
    if (physical_addr == IRQ_MASK_ADDR) { // 0x1f801074 (I_MASK)
        // Writing sets the interrupt mask
        inter->irq_mask = (uint16_t)(value & 0x7FF); // Only bits 0-10 matter
        LOG_DEBUG("[IRQ] Write to I_MASK (IRQ_MASK_ADDR): Value=0x%04x, New I_MASK=0x%04x, IRQ0 enabled=%d", value, inter->irq_mask, (inter->irq_mask & 0x1) ? 1 : 0);
        return;
    }

    // Cache Control (KSEG2)
    if (physical_addr == CACHE_CONTROL_ADDR) {
        LOG_INTERCONNECT_INFO("~ Write32 to CACHE_CONTROL register (0x%08x) = 0x%08x (Ignoring)\n", physical_addr, value);
        // Cache not implemented yet
        return;
    }

    // GPU Registers
    if (physical_addr == GPU_GP0_ADDR) { // 0x1f801810 (Write = GP0)
        if (log_get_level() >= LOG_LEVEL_TRACE) {
            LOG_INTERCONNECT_TRACE("~ Write32 GP0 = 0x%08x", value);
        }
        gpu_gp0(&inter->gpu, value); // Delegate to GPU module
        return;
    }
    if (physical_addr == GPU_GP1_ADDR) { // 0x1f801814 (Write = GP1)
        if (log_get_level() >= LOG_LEVEL_TRACE) {
            LOG_INTERCONNECT_TRACE("~ Write32 GP1 = 0x%08x", value);
        }
        gpu_gp1(&inter->gpu, value); // Delegate to GPU module
        return;
    }


    // --- Region Checks (Broader Ranges) ---

    // DMA Region
    if (physical_addr >= DMA_START && physical_addr <= DMA_END) {
        uint32_t offset = physical_addr - DMA_START;
        dma_write32_count++;
        if (log_get_level() >= LOG_LEVEL_TRACE) {
            if ((dma_write32_count % 1000 == 0) || (physical_addr != last_dma_write32_addr) || (offset != last_dma_write32_offset) || (value != last_dma_write32_value)) {
                LOG_DMA_TRACE("~ Write32 to DMA region: Addr=0x%08x Offset=0x%x = 0x%08x (count=%llu)", physical_addr, offset, value, dma_write32_count);
                last_dma_write32_addr = physical_addr;
                last_dma_write32_offset = offset;
                last_dma_write32_value = value;
            }
        }
        LOG_DMA_INFO("~ Write32 to DMA region: Addr=0x%08x Offset=0x%x = 0x%08x\n", physical_addr, offset, value);
        bool channel_became_active = dma_write(&inter->dma, offset, value); // Delegate

        // If the write activated a channel control register, start the DMA transfer
        if (channel_became_active) {
             uint32_t channel_index = (offset >> 4) & 0x7;
             LOG_DMA_INFO("  DMA Channel %d activated by write to offset 0x%x.\n", channel_index, offset);
             interconnect_perform_dma(inter, channel_index);
        }
        return;
    }

    // Memory Control Region (Includes IRQ regs handled above, RAM_SIZE reg)
    if (physical_addr >= MEM_CONTROL_START && physical_addr <= MEM_CONTROL_END) {
        // Handle specific MemCtrl writes, ignore others silently for now
        switch (physical_addr) {
            case EXPANSION_1_BASE_ADDR: // 0x1f801000
                if (value != 0x1f000000) LOG_INTERCONNECT_WARN("Warning: Bad Expansion 1 base address write: 0x%08x\n", value);
                else LOG_INTERCONNECT_INFO("~ Write32 to EXP1_BASE_ADDR = 0x%08x\n", value);
                break;
            case EXPANSION_2_BASE_ADDR: // 0x1f801004
                 if (value != 0x1f802000) LOG_INTERCONNECT_WARN("Warning: Bad Expansion 2 base address write: 0x%08x\n", value);
                 else LOG_INTERCONNECT_INFO("~ Write32 to EXP2_BASE_ADDR = 0x%08x\n", value);
                break;
            case RAM_SIZE_ADDR: // 0x1f801060
                LOG_INTERCONNECT_INFO("~ Write32 to RAM_SIZE register (0x1f801060) = 0x%08x (Ignoring)\n", value);
                break;
            // IRQ regs handled above
            default:
                LOG_INTERCONNECT_INFO("~ Write32 to Unknown MEM_CONTROL addr 0x%08x = 0x%08x (Ignoring)\n", physical_addr, value);
                break;
        }
        return;
    }

    // Timer Region
    if (physical_addr >= TIMERS_START && physical_addr <= TIMERS_END) {
         LOG_INTERCONNECT_INFO("~ Write32 to TIMERS region: Addr 0x%08x = 0x%08x (Ignoring)\n", physical_addr, value);
         // TODO: Implement Timer register writes
         return;
    }

    // SPU Region
    if (physical_addr >= SPU_START && physical_addr <= SPU_END) {
         // LOG_INFO("~ Write32 to SPU region: Address 0x%08x = 0x%08x (Ignoring)\n", physical_addr, value); // Noisy
         return; // SPU not implemented
    }

    // Main RAM Region
    if (physical_addr <= RAM_END) {
        // (Removed RAM Read/Write logs for performance)
        ram_store32(inter->ram, physical_addr, value); // Delegate
        return;
    }

    // BIOS Region (Read-Only)
    if (physical_addr >= BIOS_START && physical_addr <= BIOS_END) {
        LOG_INTERCONNECT_ERROR("Error: Write attempt to BIOS ROM at address: 0x%08x = 0x%08x\n",
                physical_addr, value);
        return; // Writes to BIOS are ignored/prohibited
    }

    // Expansion Regions (Generally ignored)
    if ((physical_addr >= EXPANSION_1_START && physical_addr <= EXPANSION_1_END) ||
        (physical_addr >= EXPANSION_2_START && physical_addr <= EXPANSION_2_END)) {
        LOG_INTERCONNECT_INFO("~ Write32 to Expansion region: Address 0x%08x = 0x%08x (Ignoring)\n", physical_addr, value);
        return;
    }

    // --- Fallback ---
    LOG_INTERCONNECT_ERROR("Unhandled physical memory write32 at address: 0x%08x = 0x%08x (Mapped from 0x%08x)\n",
            physical_addr, value, address);
}

// Add static counters and last-value tracking for rate-limiting noisy IO WRITE16 logs
// 0x1f801da8 is an SPU register polled/written in tight BIOS/game loops. Logging is only useful for SPU debugging.
// Suppress all logs for this address except at TRACE level, and even then, rate-limit.
static uint64_t write16_da8_count = 0;
static uint16_t last_write16_da8_value = 0;

/**
 * @brief Handles 16-bit memory writes from the CPU.
 * @param inter The Interconnect instance.
 * @param address Virtual address to write to.
 * @param value The 16-bit value to write.
 */
void interconnect_store16(Interconnect* inter, uint32_t address, uint16_t value) {
    uint32_t physical_addr = mask_region(address);
    // Remove or comment out noisy IO write logs for general IO region
    // LOG_TRACE("[INTERCONNECT] IO WRITE16 at 0x%08x: value=0x%04x", physical_addr, value);
    if (physical_addr == 0x1f801da8) {
        write16_da8_count++;
        if (log_get_level() >= LOG_LEVEL_TRACE) {
            // In TRACE mode, print every 1000th access or when value changes
            if ((write16_da8_count % 1000 == 0) || (value != last_write16_da8_value)) {
                LOG_DMA_TRACE("[INTERCONNECT] IO WRITE16 at 0x1f801da8: value=0x%04x (count=%llu)", value, write16_da8_count);
                last_write16_da8_value = value;
            }
        }
        // Suppress at all other log levels
    } else {
        LOG_INTERCONNECT_TRACE("[INTERCONNECT] IO WRITE16 at 0x%08x: value=0x%04x", physical_addr, value);
    }
    // CDROM 16-bit access logging
    if (physical_addr >= 0x1f801800 && physical_addr <= 0x1f801803) {
        LOG_INTERCONNECT_INFO("[INTERCONNECT] CDROM register WRITE16 at 0x%08x = 0x%04x (UNEXPECTED SIZE)\n", physical_addr, value);
    }
    // Check alignment
    if (address % 2 != 0) {
        // TODO: Trigger Address Error Store exception
        LOG_INTERCONNECT_ERROR("Unaligned store16 address: 0x%08x = 0x%04x\n", address, value);
        return;
    }
    // --- Check Timer Range --- <<< ADD THIS BLOCK
    if (physical_addr >= TIMERS_START && physical_addr <= TIMERS_END) {
        uint32_t timer_base_offset = physical_addr - TIMERS_START;
        int timer_index = timer_base_offset / 0x10;
        uint32_t register_offset = physical_addr & 0xF;
        LOG_INTERCONNECT_INFO("[INTERCONNECT] Write16 to Timer%d: addr=0x%08x offset=0x%x value=0x%04x", timer_index, physical_addr, register_offset, value);
        timer_write16(&inter->timers_state, timer_index, register_offset, value);
        return; // Handled
     }
    // Interrupt Controller Registers
    if (physical_addr == IRQ_STATUS_ADDR) { // 0x1f801070 (I_STAT)
        // Writing acknowledges (clears) specified interrupt flags
        // According to PSX-Spex: Writing 1 to a bit clears that interrupt
        uint16_t ack_mask = value & 0x7FF;
        uint16_t prev_status = inter->irq_status;
        inter->irq_status &= ~ack_mask; // Clear the bits that were written as 1
        LOG_INTERCONNECT_INFO("[IRQ][I_STAT] Write16: Value=0x%04x, AckMask=0x%04x, I_STAT: 0x%04x -> 0x%04x (caller: %s)\n", value, ack_mask, prev_status, inter->irq_status, __func__);
        return;
    }
     if (physical_addr == IRQ_MASK_ADDR) { // 0x1f801074 (I_MASK)
        inter->irq_mask = value & 0x7FF;
        LOG_INTERCONNECT_INFO("~ Write16 to IRQ_MASK: Value=0x%04x -> I_MASK=0x%04x\n", value, inter->irq_mask);
        return;
     }

    // SPU Region
    if (physical_addr >= SPU_START && physical_addr <= SPU_END) {
         // LOG_INFO("~ Write16 to SPU region: Address 0x%08x = 0x%04x (Ignoring)\n", physical_addr, value); // Noisy
         return; // SPU not implemented
    }

    // Timer Region
    if (physical_addr >= TIMERS_START && physical_addr <= TIMERS_END) {
        uint32_t timer_base_offset = physical_addr - TIMERS_START;
        int timer_index = timer_base_offset / 0x10;
        uint32_t register_offset = physical_addr & 0xF;

        // LOG_INFO("~ Write16 to Timer %d Offset 0x%x = 0x%04x\n", timer_index, register_offset, value);
        timer_write16(&inter->timers_state, timer_index, register_offset, value);
        return; // Handled
    }

    // Main RAM Region
    if (physical_addr <= RAM_END) {
        // (Removed RAM Read/Write logs for performance)
        ram_store16(inter->ram, physical_addr, value); // Delegate
        return;
    }

    // Memory Control Region (General - unlikely 16-bit writes)
     if (physical_addr >= MEM_CONTROL_START && physical_addr <= MEM_CONTROL_END) {
         LOG_INTERCONNECT_INFO("~ Write16 to MEM_CONTROL region: Addr 0x%08x = 0x%04x (Ignoring)\n", physical_addr, value);
         return;
     }

    // BIOS Region (Read-Only)
    if (physical_addr >= BIOS_START && physical_addr <= BIOS_END) {
        LOG_INTERCONNECT_ERROR("Error: Write16 attempt to BIOS ROM at address: 0x%08x = 0x%04x\n",
                physical_addr, value);
        return;
    }

    // GPU Region (Unlikely 16-bit writes)
    if (physical_addr >= GPU_START && physical_addr <= GPU_END) {
         LOG_INTERCONNECT_WARN("Warning: Unhandled GPU write16 at 0x%08x = 0x%04x\n", physical_addr, value);
         return;
    }

    // DMA Region (Unlikely 16-bit writes)
     if (physical_addr >= DMA_START && physical_addr <= DMA_END) {
        LOG_INTERCONNECT_WARN("Warning: Unhandled DMA write16 at 0x%08x = 0x%04x\n", physical_addr, value);
        return;
    }

    // Expansion Regions
    if ((physical_addr >= EXPANSION_1_START && physical_addr <= EXPANSION_1_END) ||
        (physical_addr >= EXPANSION_2_START && physical_addr <= EXPANSION_2_END)) {
        LOG_INTERCONNECT_INFO("~ Write16 to Expansion region: Address 0x%08x = 0x%04x (Ignoring)\n", physical_addr, value);
        return;
    }

    // Logging: Suppress IO WRITE16 logs for polled SPU/controller addresses unless LOG_LEVEL_DEBUG is enabled.
    // This prevents log flooding from BIOS polling loops.
    if ((physical_addr >= 0x1f801d80 && physical_addr <= 0x1f801dbf) ||
        physical_addr == 0x1f801da8 || physical_addr == 0x1f801daa || physical_addr == 0x1f801dac || physical_addr == 0x1f801dae) {
        LOG_INTERCONNECT_DEBUG("[INTERCONNECT] IO WRITE16 at 0x%08x: value=0x%04x", physical_addr, value);
        return;
    }

    // --- Fallback ---
    LOG_INTERCONNECT_ERROR("Unhandled physical memory write16 at address: 0x%08x = 0x%04x (Mapped from 0x%08x)\n",
            physical_addr, value, address);
}

/**
 * @brief Handles 8-bit memory writes from the CPU.
 * @param inter The Interconnect instance.
 * @param address Virtual address to write to.
 * @param value The 8-bit value to write.
 */
void interconnect_store8(Interconnect* inter, uint32_t address, uint8_t value) {
    uint32_t physical_addr = mask_region(address);
    // Remove or comment out noisy IO write logs for general IO region
    // if (physical_addr >= 0x1f801000 && physical_addr < 0x1f802000) {
    //     LOG_INFO("[INTERCONNECT] IO WRITE8 at 0x%08x: value=0x%02x\n", physical_addr, value);
    // }
    // --- Check Timer Range --- <<< ADD THIS BLOCK
    if (physical_addr >= TIMERS_START && physical_addr <= TIMERS_END) {
        // 8-bit writes to timers are generally undefined or write partial registers.
        LOG_INTERCONNECT_WARN("Warning: Unhandled 8-bit write to Timer range: 0x%08x = 0x%02x\n", physical_addr, value);
        // Ignoring is safest for now.
        return;
    }
    // --- CDROM Register Access (Strict PSX-Spex) ---
    if (physical_addr >= 0x1f801800 && physical_addr <= 0x1f801803) {
        LOG_INTERCONNECT_DEBUG("[INTERCONNECT] CDROM register WRITE8 at 0x%08x = 0x%02x\n", physical_addr, value);
#if LOG_LEVEL >= LOG_LEVEL_INFO
        if (++cdrom_write8_count % 10000 == 0) {
            LOG_INTERCONNECT_INFO("[INTERCONNECT] CDROM register WRITE8: %d accesses, last at 0x%08x = 0x%02x\n", cdrom_write8_count, physical_addr, value);
        }
#endif
        cdrom_write_register(&inter->cdrom, physical_addr, value);
        return;
    }

     // Expansion 2 Region
    if (physical_addr >= EXPANSION_2_START && physical_addr <= EXPANSION_2_END) {
         // LOG_INFO("~ Write8 to Expansion 2 region: 0x%08x = 0x%02x (Ignoring)\n", physical_addr, value); // Noisy
         return; // Expansion 2 typically unused/debug
    }

    // SPU Region
    if (physical_addr >= SPU_START && physical_addr <= SPU_END) {
         // LOG_INFO("~ Write8 to SPU region: Address 0x%08x = 0x%02x (Ignoring)\n", physical_addr, value); // Noisy
         return; // SPU not implemented
    }

     // Main RAM Region
    if (physical_addr <= RAM_END) {
        // (Removed RAM Read/Write logs for performance)
        ram_store8(inter->ram, physical_addr, value); // Delegate
        return;
    }

    // Memory Control Region
    if (physical_addr >= MEM_CONTROL_START && physical_addr <= MEM_CONTROL_END) {
         LOG_INTERCONNECT_INFO("~ Write8 to MEM_CONTROL region: 0x%08x = 0x%02x (Ignoring)\n", physical_addr, value);
         return; // Unlikely target for 8-bit writes
    }

    // BIOS Region (Read-Only)
    if (physical_addr >= BIOS_START && physical_addr <= BIOS_END) {
        LOG_INTERCONNECT_ERROR("Error: Write8 attempt to BIOS ROM at address: 0x%08x = 0x%02x\n", physical_addr, value);
        return;
    }

    // Expansion 1 Region
    if (physical_addr >= EXPANSION_1_START && physical_addr <= EXPANSION_1_END) {
        LOG_INTERCONNECT_INFO("~ Write8 to Expansion 1 region: Address 0x%08x = 0x%02x (Ignoring)\n", physical_addr, value);
        return;
    }

    // --- Fallback ---
    LOG_INTERCONNECT_ERROR("Unhandled physical memory write8 at address: 0x%08x = 0x%02x (Mapped from 0x%08x)\n",
            physical_addr, value, address);
}


// --- DMA Transfer Logic ---
// (Based on Guide Section 3.7, 3.8, 3.9, 3.10)

// Helper to calculate transfer size for Block/Manual modes
static uint32_t dma_get_transfer_size_words(DmaChannel* ch) {
    if (ch->sync == LINKED_LIST) return 0; // Size is determined by list content

    uint32_t bs = (uint32_t)ch->block_size;
    // In Manual mode (Sync=0), BlockSize (BC/BA field) is the word count.
    // 0 means max size (0x10000 words).
    if (ch->sync == MANUAL) {
        return (bs == 0) ? 0x10000 : bs;
    }

    // In Request mode (Sync=1), size is BlockCount * BlockSize
    uint32_t bc = (uint32_t)ch->block_count;
    if (bs == 0 || bc == 0) {
        LOG_INTERCONNECT_WARN("Warning: DMA Request sync with zero size/count (BS=%u, BC=%u)\n", bs, bc);
        return 0; // Invalid size for Request mode
    }
    return bs * bc;
}

/**
 * @brief Executes a DMA transfer for the specified channel.
 * Called when a channel becomes active after a register write.
 * Handles OTC, GPU Linked List, and placeholder for other block transfers.
 * @param inter The Interconnect instance.
 * @param channel_index The DMA channel number (0-6).
 */
static void interconnect_perform_dma(Interconnect* inter, uint32_t channel_index) {
    if (channel_index >= 7) {
        LOG_INTERCONNECT_ERROR("Error: interconnect_perform_dma called with invalid channel index %u\n", channel_index);
        return;
    }

    LOG_DMA_INFO("--- Starting DMA Transfer for Channel %d ---\n", channel_index);
    DmaChannel* ch = &inter->dma.channels[channel_index];
    DmaSync sync_mode = ch->sync;

    switch (sync_mode) {
        case LINKED_LIST:
            // Primarily used for GPU Channel 2
            if (channel_index == 2 && ch->direction == FROM_RAM) {
                uint32_t addr = ch->base_addr & 0x00FFFFFC; // Start address from MADR
                LOG_DMA_INFO("DMA GPU Linked List: Starting at 0x%08x\n", addr);
                while(1) {
                    // Check address bounds before reading header
                    if (addr >= RAM_SIZE) {
                        LOG_DMA_ERROR("DMA GPU LL Error: Header address 0x%08x out of RAM bounds.\n", addr);
                        break;
                    }
                    // Read header: size in high byte, next address in low 24 bits
                    uint32_t header = interconnect_load32(inter, addr); // Use interconnect load
                    uint32_t num_words = header >> 24;
                    uint32_t next_addr = header & 0x00FFFFFC; // Mask to word boundary
                    // LOG_INFO("  LL Header @ 0x%08x: Value=0x%08x, Size=%u words, Next=0x%08x\n", addr, header, num_words, next_addr); // Debug

                    // Transfer packet words (if any)
                    if (num_words > 0) {
                         for (uint32_t i = 0; i < num_words; ++i) {
                            addr = (addr + 4) & 0x00FFFFFC; // Advance address for command word
                            if (addr >= RAM_SIZE) { // Check bounds before reading command
                                LOG_DMA_ERROR("DMA GPU LL Error: Command address 0x%08x out of RAM bounds.\n", addr);
                                next_addr = 0xFFFFFF; // Force stop after this packet
                                break; // Exit inner loop
                            }
                            uint32_t command_word = interconnect_load32(inter, addr); // Read command
                            gpu_gp0(&inter->gpu, command_word); // Send command to GPU GP0 port
                        }
                        if (next_addr == 0xFFFFFF) break; // Break outer loop if error occurred
                    }

                    // Check for end-of-list marker (Top bit of next_addr usually, or 0xFFFFFF) [cite: 1808]
                    if ((header & 0x800000) != 0) { // Check MSB of address field as per Mednafen comment
                        LOG_DMA_INFO("DMA GPU Linked List: End marker (0x800000) found in header 0x%08x.\n", header);
                        break;
                    }
                    // Check for explicit 0xFFFFFF marker (safer)
                    if (next_addr == 0xFFFFFF) {
                        LOG_DMA_INFO("DMA GPU Linked List: End marker (0xFFFFFF) found.\n");
                         break;
                    }

                    // Check next address validity before proceeding
                     if (next_addr >= RAM_SIZE) {
                         LOG_DMA_ERROR("DMA GPU LL Error: Next header address 0x%08x out of RAM bounds.\n", next_addr);
                         break;
                     }
                    // Move to the next header address
                    addr = next_addr;
                }
                LOG_DMA_INFO("DMA GPU Linked List: Finished.\n");
            } else {
                 LOG_DMA_ERROR("Error: Linked List DMA mode attempted on unsupported channel (%d) or direction (%d).\n", channel_index, ch->direction);
            }
            break;

        case MANUAL:
        case REQUEST:
            {
                uint32_t words_to_transfer = dma_get_transfer_size_words(ch);
                if (words_to_transfer == 0) {
                    LOG_DMA_WARN("Warning: DMA Block/Request transfer started with zero size for channel %d.\n", channel_index);
                    break; // Nothing to do
                }

                uint32_t addr = ch->base_addr & 0x00FFFFFC; // Start address
                int32_t step = (ch->step == INCREMENT) ? 4 : -4;
                LOG_DMA_INFO("DMA Block/Request: Chan=%d, Dir=%s, Sync=%s, Step=%d, Addr=0x%08x, Size=%u words\n",
                       channel_index, (ch->direction == FROM_RAM ? "FROM_RAM" : "TO_RAM"),
                       (sync_mode == MANUAL ? "MANUAL" : "REQUEST"), step, addr, words_to_transfer);

                for (uint32_t i = 0; i < words_to_transfer; ++i) {
                    // Ensure address stays within RAM bounds (mask low bits, check high bits)
                    uint32_t current_addr_masked = addr & 0x001FFFFC; // Mask address to stay within 2MB and word aligned
                    if (current_addr_masked >= RAM_SIZE) {
                         LOG_DMA_ERROR("DMA Block Error: Address 0x%08x (masked 0x%08x) out of RAM bounds on channel %d.\n", addr, current_addr_masked, channel_index);
                         break; // Stop transfer if address goes out of bounds
                    }

                    if (ch->direction == FROM_RAM) {
                        // RAM -> Peripheral
                        uint32_t data_word = interconnect_load32(inter, current_addr_masked); // Read from RAM
                        switch (channel_index) {
                            case 2: // GPU
                                gpu_gp0(&inter->gpu, data_word); // Send data word to GP0 (for Image Load etc.)
                                break;
                            // Add cases for other peripherals (CDROM, SPU, MDEC) here
                            default:
                                LOG_DMA_WARN("Warning: Unhandled DMA Block FROM_RAM transfer for channel %d, Addr=0x%08x, Data=0x%08x\n",
                                       channel_index, current_addr_masked, data_word);
                                break;
                        }
                    } else { // TO_RAM
                        // Peripheral -> RAM
                        uint32_t data_word = 0; // Default value if peripheral not handled
                        switch (channel_index) {
                            case 6: // OTC - Ordering Table Clear
                                // Value depends on position in transfer
                                data_word = (i == (words_to_transfer - 1)) // Is it the last word?
                                            ? 0x00FFFFFF                  // Yes: End marker
                                            : ((addr - 4) & 0x00FFFFFC); // No: Pointer to previous entry
                                break;
                            // Add cases for other peripherals reading TO RAM (CDROM, SPU, MDEC)
                            default:
                                LOG_DMA_WARN("Warning: Unhandled DMA Block TO_RAM transfer for channel %d, Addr=0x%08x\n",
                                       channel_index, current_addr_masked);
                                break;
                        }
                        interconnect_store32(inter, current_addr_masked, data_word); // Write to RAM
                    }

                    // Advance address for next word
                    addr = (uint32_t)((int32_t)addr + step); // Apply step
                }
                 LOG_DMA_INFO("DMA Block/Request: Finished transfer for channel %d.\n", channel_index);
            }
            break;

        default: // Should not happen if sync enum is correct
            LOG_DMA_ERROR("Error: Unknown DMA Sync mode %d encountered for channel %d.\n", sync_mode, channel_index);
            break;
    }

    // Mark the channel as finished (clears enable/trigger bits)
    dma_channel_done(ch);
    LOG_DMA_INFO("--- Finished DMA Transfer Processing for Channel %d ---\n", channel_index);

    // TODO: Trigger DMA interrupt here if enabled in DICR and channel IRQ was set
}

// --- BIOS Boot Helper: Force Interrupt Configuration ---
// Per PSX-Spex/nocash, if the BIOS doesn't configure I_MASK for IRQ0,
// we need to force it to allow VBlank IRQ0 processing.
static void interconnect_force_bios_boot_config(Interconnect* inter) {
    // Only force configuration if I_MASK is not already configured for IRQ0
    if ((inter->irq_mask & 0x0001) == 0) {
        LOG_INTERCONNECT_INFO("[INTERCONNECT] BIOS Boot Helper: Forcing I_MASK configuration for IRQ0");
        
        // Enable IRQ0 (VBlank) in I_MASK
        inter->irq_mask |= 0x0001;  // Bit 0: IRQ0 enable
        
        LOG_INTERCONNECT_INFO("[INTERCONNECT] Forced I_MASK=0x%04x [PSX-Spex: IRQ0 enabled]", inter->irq_mask);
    }
}

// In the main emulation loop or timer step function, call this helper
// This can be called from timers_step or main loop
void interconnect_check_bios_boot(Interconnect* inter) {
#if LOG_LEVEL >= LOG_LEVEL_DEBUG
    LOG_INTERCONNECT_DEBUG("[INTERCONNECT] check_bios_boot called");
#endif
    static int boot_helper_counter = 0;
    boot_helper_counter++;
    
    // After 1000 calls (about 30ms), force interrupt config if needed
    if (boot_helper_counter > 1000) {
        interconnect_force_bios_boot_config(inter);
        boot_helper_counter = 0; // Reset counter
    }
}

// --- IRQ10 (Lightgun/Scanline IRQ) ---
// Per PSX-Spex/nocash, IRQ10 should be prioritized in the BIOS/IRQ chain for lightgun/scanline timing.
// When IRQ10 is triggered, the handler should:
//   1. Acknowledge IRQ10 (write to I_STAT).
//   2. Optionally disable DMAs.
//   3. Wait for IRQ10 bit in I_STAT to be set again (for next scanline).
//   4. Read Timer0 and Timer1 for X/Y coordinates.
//   5. Use region-specific formulas:
//        NTSC: X = (Timer0 - 140) * 0.198166, Y = Timer1
//        PAL:  X = (Timer0 - 140) * 0.196358, Y = Timer1
//   6. For system clock mode, convert Timer0 to video clock (mul 11/div 7), then to dotclock (div 8 for 320px).
// Emulator should ensure IRQ10 is requested/cleared properly and that timer reads are accurate.