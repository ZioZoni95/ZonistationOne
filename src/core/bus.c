#include "interconnect.h" // Includes associated header and headers for components (gpu.h, dma.h etc.)
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include "log.h"
#include "dma.h"
#include "gpu.h"
#include "ram.h"
#include "cpu.h"  // For cpu_exception() and ExceptionCause

// Instrumentation: BIOS patch-check monitoring range
#define BIOS_PATCH_START 0x80059DC0U
#define BIOS_PATCH_END   0x80059E20U

// Using new PCSX ReARMed-style logging system


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

// --- Hardware Register Block (0x1f801000 - 0x1f801fff) ---
// Adapted from PCSX ReARMed (https://github.com/notaz/pcsx_rearmed)
// Copyright (c) PCSX ReARMed authors. Used under open source license for compatibility.
static uint8_t hwregs[0x1000] = {0};

uint32_t mask_region(uint32_t addr) {
    size_t index = (addr >> 29) & 0x7;
    return addr & REGION_MASK[index];
}

static void interconnect_perform_dma(Interconnect* inter, uint32_t channel_index);


// --- Forward Declarations ---
static void interconnect_force_bios_boot_config(Interconnect* inter);
// --- Load Operations ---

/**
 * @brief Handles 32-bit memory reads from the CPU.
 * @param inter The Interconnect instance.
 * @param address Virtual address to read from.
 * @return The 32-bit value read.
 */
uint32_t interconnect_load32(Interconnect* inter, uint32_t address) {
    uint32_t physical_addr = mask_region(address);

    if (physical_addr >= 0x1f801800 && physical_addr <= 0x1f801803) {
        LOG_CDROM_WARN("[INTERCONNECT] CDROM register READ32 at 0x%08x (UNEXPECTED SIZE)", physical_addr);
    }
    if (address % 4 != 0) {
        LOG_INTERCONNECT_ERROR("[INTERCONNECT] Unaligned load32 address: 0x%08x", address);
        // Trigger Address Error Load exception directly if CPU pointer is set
        if (inter->cpu) {
            inter->cpu->badvaddr = address;
            cpu_exception(inter->cpu, EXCEPTION_LOAD_ADDRESS_ERROR);
        }
        return 0; // Return 0 (exception already triggered)
    }

    // --- Hardware Register Checks (Specific Addresses First) ---

// --- Check Timer Range --- <<< ADD THIS BLOCK
    if (physical_addr >= TIMERS_START && physical_addr <= TIMERS_END) {
        uint32_t timer_base_offset = physical_addr - TIMERS_START;
        int timer_index = timer_base_offset / 0x10; // Each timer block is 0x10 bytes wide
        uint32_t register_offset = physical_addr & 0xF; // Offset within the timer block (0, 4, 8)

        return timer_read32(&inter->timers_state, timer_index, register_offset);
    }
    
    // Memory Control Registers (0x1f801000 - 0x1f801020)
    // PSX-SPEX: These configure expansion base/size, delays, and BIOS ROM size
    if (physical_addr == 0x1f801000) {
        // Expansion 1 Base Address (default: 1F000000h)
return 0x1F000000;
    }
    if (physical_addr == 0x1f801004) {
        // Expansion 2 Base Address (default: 1F802000h)
return 0x1F802000;
    }
    if (physical_addr == 0x1f801008) {
        // Expansion 1 Delay/Size (default: 0013243Fh)
return 0x0013243F;
    }
    if (physical_addr == 0x1f80100C) {
        // Expansion 3 Delay/Size (default: 00003022h)
return 0x00003022;
    }
    if (physical_addr == 0x1f801010) {
        // BIOS ROM Delay/Size (default: 0013243Fh)
return 0x0013243F;
    }
    if (physical_addr == 0x1f801014) {
        // SPU_DELAY Delay/Size (default: 200931E1h)
return 0x200931E1;
    }
    if (physical_addr == 0x1f801018) {
        // CDROM_DELAY Delay/Size (default: 00020843h or 00020943h)
return 0x00020843;
    }
    if (physical_addr == 0x1f80101C) {
        // Expansion 2 Delay/Size (default: 00070777h)
return 0x00070777;
    }
    if (physical_addr == 0x1f801020) {
        // COM_DELAY / COMMON_DELAY (default: 00031125h or 0000132Ch)
return 0x00031125;
    }
    if (physical_addr == 0x1f801030) {
        LOG_INTERCONNECT_DEBUG("[INTERCONNECT] BIOS reading 32-bit from 0x1f801030 (unknown register) - returning 0x00");
        return 0x00; // Return 0 for now to see if BIOS continues
    }
    
    // Interrupt Controller Registers
    if (physical_addr == IRQ_STATUS_ADDR) { // 0x1f801070 (I_STAT)
return (uint32_t)inter->irq_status;
    }
    if (physical_addr == IRQ_MASK_ADDR) { // 0x1f801074 (I_MASK)
return (uint32_t)inter->irq_mask;
    }

    // GPU Registers
    if (physical_addr == GPU_GPUREAD_ADDR) { // 0x1f801810 (Read = GPUREAD)
        // Reading GPUREAD should return data from VRAM transfers or command responses
return gpu_read_data(&inter->gpu);
    }
    if (physical_addr == GPU_GPUSTAT_ADDR) { // 0x1f801814 (Read = GPUSTAT)
        // Reading GPUSTAT returns the GPU status flags
        return gpu_read_status(&inter->gpu);
    }

    // Timer Registers (Example: Read Timer 1 Counter)
    if (physical_addr == 0x1f801110) { // Timer 1 Counter Value (T1_COUNT)
         // TODO: Implement actual Timer read logic
return 0;
    }
    // Add reads for other Timer counters/modes/targets if needed


    // --- Region Checks (Broader Ranges) ---

    // DMA Region (0x1f801080 - 0x1f8010FF)
    if (physical_addr >= DMA_START && physical_addr <= DMA_END) {
        uint32_t offset = physical_addr - DMA_START;
        return dma_read(&inter->dma, offset);
    }

    // --- Scratchpad Region (0x1F800000–0x1F8003FF, mirrored in KUSEG/KSEG0 only) ---
    // DOCS: memorymap.md, "Scratchpad is mirrored only in KUSEG and KSEG0, but not in KSEG1"
    if ((physical_addr >= SCRATCHPAD_START && physical_addr <= SCRATCHPAD_END) &&
        ((address < 0xA0000000) || (address < 0x80000000))) {
        uint32_t offset = physical_addr - SCRATCHPAD_START;
        if (offset + 3 < SCRATCHPAD_SIZE) {
            uint32_t value = inter->scratchpad[offset] |
                           (inter->scratchpad[offset + 1] << 8) |
                           (inter->scratchpad[offset + 2] << 16) |
                           (inter->scratchpad[offset + 3] << 24);
return value;
        }
    }

    // --- BIOS Region (0x1FC00000–0x1FC7FFFF, mirrored in last 4MB) ---
    // DOCS: memorymap.md, "512K BIOS ROM can be mirrored to the last 4MB (disabled by default)"
    static int bios_load32_debug_count = 0;
    if ((physical_addr >= BIOS_START && physical_addr <= BIOS_END) ||
        (physical_addr >= 0x1FC00000 && physical_addr < 0x20000000)) {
        uint32_t bios_offset = (physical_addr - BIOS_START) % (BIOS_END - BIOS_START + 1);
        if (log_get_level() >= LOG_LEVEL_TRACE && bios_load32_debug_count < 10) {
bios_load32_debug_count++;
        }
        return bios_load32(inter->bios, bios_offset);
    }

    // --- Main RAM Region (0x00000000 - 0x001FFFFF, mirrored in first 8MB) ---
    // DOCS: memorymap.md, "2MB RAM can be mirrored to the first 8MB"
    if (physical_addr <= RAM_END || (physical_addr < 0x00800000)) {
        // Mirror RAM in first 8MB
        uint32_t ram_offset = physical_addr % (RAM_END + 1);
        return ram_load32(inter->ram, ram_offset);
    }

        // Expansion 3 Region alias helper: some BIOS routines access both 0x1FAxxxxx (spec) and
        // 0x0FAxxxxx (after region masking removes bit 28). Treat both as the same empty slot.
        const bool is_expansion3 =
            (physical_addr >= 0x1FA00000 && physical_addr < 0x1FC00000) ||
            (physical_addr >= 0x0FA00000 && physical_addr < 0x0FC00000);

        // Expansion 3 Region (Physical 0x1FA00000-0x1FBFFFFF) – typically unpopulated.
        // BIOS polls addresses such as 0xAFA40028/2C during the security check; returning
        // deterministic zeros keeps the BIOS from turning garbage data into bogus jump targets
        // (observed crash at PC=0x00000068).
        if (is_expansion3) {
            return 0x00000000;
        }

    if (physical_addr >= 0x00200000 && physical_addr < 0x1FA00000) {
        LOG_INTERCONNECT_ERROR("[INTERCONNECT] Unhandled physical memory read32 at address: 0x%08x (Mapped from 0x%08x)",
                physical_addr, address);
        return 0xFFFFFFFF;
    }

    // Timer Region (General Check - 0x1f801100 - 0x1f80112F)
    if (physical_addr >= TIMERS_START && physical_addr <= TIMERS_END) {
        LOG_INTERCONNECT_WARN("[INTERCONNECT] Unhandled Timer read32 at 0x%08x", physical_addr);
        return 0; // Return 0 for unhandled timer reads
    }

        // SPU Region (0x1f801C00 - 0x1f801E7F)
        if (physical_addr >= SPU_START && physical_addr <= SPU_END) {
            return spu_read32(inter, physical_addr);
        }

    if (physical_addr >= EXPANSION_1_START && physical_addr <= EXPANSION_1_END) {
        return 0x00000000;
    }

    // VRAM Region (0x1F000000 - 0x1F7FFFFF)
    if (physical_addr >= 0x1F000000 && physical_addr <= 0x1F7FFFFF) {
return 0xFFFFFFFF; // Open bus for unimplemented VRAM
    }

    // --- I/O Ports and Peripheral Registers (0x1F801000–0x1F801FFF) ---
    // DOCS: iomap.md, map all ports and registers
    if (physical_addr >= 0x1f801000 && physical_addr <= 0x1f801fff) {
        uint32_t offset = physical_addr - 0x1f801000;
        // Map all I/O ports as per DOCS/iomap.md
        // Example: Joypad/Memory Card, SIO, DMA, Timers, CDROM, GPU, SPU, MDEC
        // Add stubs or delegate to respective modules
        if (physical_addr >= 0x1f801040 && physical_addr <= 0x1f80104f) {
            // Joypad/Memory Card - offset relative to SIO base 0x1f801040
            return sio_read32(&inter->sio, physical_addr - 0x1f801040);
        }
        if (physical_addr >= 0x1f801050 && physical_addr <= 0x1f80105f) {
            // SIO (Serial Port) - offset relative to SIO base 0x1f801050
            return sio_read32(&inter->sio, physical_addr - 0x1f801050);
        }
        if (physical_addr >= 0x1f801060 && physical_addr <= 0x1f801063) {
            // RAM_SIZE
            // DOCS: iomap.md, "RAM_SIZE"
            return 0x00000B88;
        }
        if (physical_addr >= 0x1f801070 && physical_addr <= 0x1f801077) {
            // Interrupt Controller
            // DOCS: iomap.md, "I_STAT", "I_MASK"
            if (physical_addr == IRQ_STATUS_ADDR) return inter->irq_status;
            if (physical_addr == IRQ_MASK_ADDR) return inter->irq_mask;
        }
        if (physical_addr >= 0x1f801080 && physical_addr <= 0x1f8010ff) {
            // DMA
            // DOCS: iomap.md, "DMA0"–"DMA6", "DPCR", "DICR"
            return dma_read(&inter->dma, offset);
        }
        if (physical_addr >= 0x1f801100 && physical_addr <= 0x1f80112f) {
            // Timers
            // DOCS: iomap.md, "Timer 0", "Timer 1", "Timer 2"
            int timer_index = (physical_addr - 0x1f801100) / 0x10;
            uint32_t reg_offset = physical_addr & 0xF;
            return timer_read32(&inter->timers_state, timer_index, reg_offset);
        }
        if (physical_addr >= 0x1f801800 && physical_addr <= 0x1f801803) {
            // CDROM - 8-bit registers, return combined 32-bit
            // DOCS: iomap.md, "CD Index/Status", "CD Response Fifo", etc.
            uint32_t result = 0;
            for (int i = 0; i < 4; i++) {
                result |= ((uint32_t)cdrom_read8(&inter->cdrom, physical_addr + i)) << (i * 8);
            }
            return result;
        }
        if (physical_addr >= 0x1f801810 && physical_addr <= 0x1f801817) {
            // GPU
            // DOCS: iomap.md, "GP0", "GP1", "GPUREAD", "GPUSTAT"
            if (physical_addr == GPU_GPUREAD_ADDR) return gpu_read_data(&inter->gpu);
            if (physical_addr == GPU_GPUSTAT_ADDR) return gpu_read_status(&inter->gpu);
        }
        if (physical_addr >= 0x1f801820 && physical_addr <= 0x1f801827) {
            // MDEC (Macroblock Decoder) - Not yet implemented
            LOG_INTERCONNECT_WARN("[INTERCONNECT] MDEC read at 0x%08x (stub)", physical_addr);
            return 0;
        }
        if (physical_addr >= 0x1f801c00 && physical_addr <= 0x1f801e7f) {
            // SPU
            // DOCS: iomap.md, "SPU Voice", "SPU Control", "SPU Reverb"
            return 0; // Stub for now
        }
        // Default: return hardware register value
        return hwregs[offset];
    }

    // --- Fallback for Unhandled Addresses ---
    if ((physical_addr & 0xFFFF0000) == 0xFFFF0000) {
        // KSEG2 region: return 0 for unmapped addresses (per nocash/PSX-Spex)
        return 0;
    }
    
    // --- PCSX ReARMed-style Memory Region Handling ---
    // Handle the 0x24xxxxxx range that's causing your errors
    if (physical_addr >= 0x20000000 && physical_addr <= 0x2FFFFFFF) {
        // This is the unmapped memory region causing the BIOS errors
        // Return 0 for unmapped memory (PlayStation open bus behavior)
return 0;
    }
    
    // Handle other unmapped regions (0x30xxxxxx - 0x7xxxxxxx)
    if (physical_addr >= 0x30000000 && physical_addr <= 0x7FFFFFFF) {
return 0;
    }
    
         // --- PCSX ReARMed-style Memory Region Handling ---
     // Handle the 0x24xxxxxx range that's causing your errors
     if (physical_addr >= 0x20000000 && physical_addr <= 0x2FFFFFFF) {
         // This is the unmapped memory region causing the BIOS errors
         // Return 0 for unmapped memory (PlayStation open bus behavior)
return 0;
     }
     
     // Handle other unmapped regions (0x30xxxxxx - 0x7xxxxxxx)
     if (physical_addr >= 0x30000000 && physical_addr <= 0x7FFFFFFF) {
return 0;
     }
     
    if (physical_addr >= 0xf0000000) {
        LOG_INTERCONNECT_TRACE("[INTERCONNECT] Unmapped memory read32 0x%08x", physical_addr);
        return 0;
    }
    
    // Only log as error if we reach here (truly unhandled)
    LOG_INTERCONNECT_ERROR("[INTERCONNECT] Unhandled physical memory read32 at address: 0x%08x (Mapped from 0x%08x)",
            physical_addr, address);
    return 0; // Return 0 for unmapped memory
}

/**
 * @brief Handles 16-bit memory reads from the CPU.
 * @param inter The Interconnect instance.
 * @param address Virtual address to read from.
 * @return The 16-bit value read.
 */
uint16_t interconnect_load16(Interconnect* inter, uint32_t address) {
    uint32_t physical_addr = mask_region(address);

    if (physical_addr >= 0x1f801800 && physical_addr <= 0x1f801803) {
        LOG_INTERCONNECT_DEBUG("[INTERCONNECT] CDROM register READ16 at 0x%08x (UNEXPECTED SIZE)", physical_addr);
    }
    if (address % 2 != 0) {
        LOG_INTERCONNECT_ERROR("[INTERCONNECT] Unaligned load16 address: 0x%08x", address);
        if (inter->cpu) {
            inter->cpu->badvaddr = address;
            cpu_exception(inter->cpu, EXCEPTION_LOAD_ADDRESS_ERROR);
        }
        return 0;
    }
// --- Check Timer Range --- <<< ADD THIS BLOCK
    if (physical_addr >= TIMERS_START && physical_addr <= TIMERS_END) {
        uint32_t timer_base_offset = physical_addr - TIMERS_START;
        int timer_index = timer_base_offset / 0x10; // Each timer block is 0x10 bytes wide
        uint32_t register_offset = physical_addr & 0xF; // Offset within the timer block (0, 4, 8)

        return timer_read16(&inter->timers_state, timer_index, register_offset);
    }
    
    // TEST: Add missing hardware register read handlers for 16-bit access
    if (physical_addr == 0x1f801010) {
        LOG_INTERCONNECT_DEBUG("[INTERCONNECT] BIOS reading 16-bit from 0x1f801010 (unknown register) - returning 0x0000");
        return 0x0000; // Return 0 for now to see if BIOS continues
    }
    if (physical_addr == 0x1f801020) {
        LOG_INTERCONNECT_DEBUG("[INTERCONNECT] BIOS reading 16-bit from 0x1f801020 (unknown register) - returning 0x0000");
        return 0x0000; // Return 0 for now to see if BIOS continues
    }
    if (physical_addr == 0x1f801030) {
        LOG_INTERCONNECT_DEBUG("[INTERCONNECT] BIOS reading 16-bit from 0x1f801030 (unknown register) - returning 0x0000");
        return 0x0000; // Return 0 for now to see if BIOS continues
    }
    
    // Interrupt Controller Registers
    if (physical_addr == IRQ_STATUS_ADDR) { // 0x1f801070 (I_STAT)
return inter->irq_status; // Always return current value, no masking or filtering
    }
     if (physical_addr == IRQ_MASK_ADDR) { // 0x1f801074 (I_MASK)
return inter->irq_mask;
     }

    // SPU Region (Reads usually return 0 or specific status)
    if (physical_addr >= SPU_START && physical_addr <= SPU_END) {
            return spu_read16(inter, physical_addr);
    }

    // Timer Region (Check specific registers if needed)
     if (physical_addr >= TIMERS_START && physical_addr <= TIMERS_END) {
        // Handle specific 16-bit Timer reads (Counter, Mode, Target)
        // Example:
        // if (physical_addr == 0x1F801100) return timer0_get_count();
        LOG_INTERCONNECT_WARN("[INTERCONNECT] Unhandled Timer read16 at 0x%08x", physical_addr);
        return 0;
    }

    // Main RAM Region
    if (physical_addr <= RAM_END) {
        return ram_load16(inter->ram, physical_addr);
    }

    // BIOS Region (Unlikely, but check)
     if (physical_addr >= BIOS_START && physical_addr <= BIOS_END) {
        // (Removed BIOS ROM Read logs for performance)
        return bios_load16(inter->bios, physical_addr - BIOS_START);
    }

    // GPU Region (Unlikely 16-bit reads)
    if (physical_addr >= GPU_START && physical_addr <= GPU_END) {
         LOG_INTERCONNECT_WARN("[INTERCONNECT] Unhandled GPU read16 at 0x%08x", physical_addr);
         return 0;
    }

    // DMA Region (Unlikely 16-bit reads)
     if (physical_addr >= DMA_START && physical_addr <= DMA_END) {
        LOG_DMA_WARN("[DMA] Unhandled read16 at 0x%08x", physical_addr);
        return 0;
    }

    const bool is_expansion3 =
        (physical_addr >= 0x1FA00000 && physical_addr < 0x1FC00000) ||
        (physical_addr >= 0x0FA00000 && physical_addr < 0x0FC00000);

    if (is_expansion3) {
        static uint32_t exp3_read16_count = 0;
        exp3_read16_count++;
        if (exp3_read16_count <= 5) {
}
        return 0x0000; // POST3 latch idle value
    }

    // Expansion 1 Region
    if (physical_addr >= EXPANSION_1_START && physical_addr <= EXPANSION_1_END) {
         static uint32_t exp1_read16_count = 0;
         exp1_read16_count++;
         if (exp1_read16_count <= 10) {
}
         return 0x0000; // Return 0 to prevent BIOS from misinterpreting as jump target
    }

    // SIO (Serial I/O) - JOY registers 0x1F801040-0x1F80104F
    // MUST be before the generic hwregs catch-all below.
    if (physical_addr >= 0x1f801040 && physical_addr <= 0x1f80104f) {
        uint32_t offset = physical_addr - 0x1f801040;
        return sio_read16(&inter->sio, offset);
    }

    // --- Hardware Register Checks (Specific Addresses First) ---
    if (physical_addr >= 0x1f801000 && physical_addr <= 0x1f801fff) {
        uint32_t offset = physical_addr - 0x1f801000;
        return *(uint16_t *)&hwregs[offset];
    }

    // --- Fallback ---
    if ((physical_addr & 0xFFFF0000) == 0xFFFF0000) {
        return 0;
    }
    
    // --- PCSX ReARMed-style Memory Region Handling for 16-bit ---
     // Handle the 0x24xxxxxx range that's causing your errors
     if (physical_addr >= 0x20000000 && physical_addr <= 0x2FFFFFFF) {
return 0;
     }
     
     // Handle other unmapped regions (0x30xxxxxx - 0x7xxxxxxx)
     if (physical_addr >= 0x30000000 && physical_addr <= 0x7FFFFFFF) {
return 0;
     }
     
    if (physical_addr >= 0xf0000000) {
        LOG_INTERCONNECT_TRACE("[INTERCONNECT] Unmapped memory read16 0x%08x", physical_addr);
        return 0;
    }

    LOG_INTERCONNECT_ERROR("[INTERCONNECT] Unhandled physical memory read16 at address: 0x%08x (Mapped from 0x%08x)", physical_addr, address);
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

    // --- Check Timer Range ---
    if (physical_addr >= TIMERS_START && physical_addr <= TIMERS_END) {
        // 8-bit reads from timers are generally undefined or read partial registers.
        LOG_INTERCONNECT_WARN("[INTERCONNECT] Unhandled 8-bit read from Timer range: 0x%08x", physical_addr);
        return 0; // Return 0 for safety
    }
    
    // TEST: Add missing hardware register read handlers for 8-bit access
    if (physical_addr == 0x1f801010) {
        LOG_INTERCONNECT_DEBUG("[INTERCONNECT] BIOS reading 8-bit from 0x1f801010 (unknown register) - returning 0x00");
        return 0x00; // Return 0 for now to see if BIOS continues
    }
    if (physical_addr == 0x1f801020) {
        LOG_INTERCONNECT_DEBUG("[INTERCONNECT] BIOS reading 8-bit from 0x1f801020 (unknown register) - returning 0x00");
        return 0x00; // Return 0 for now to see if BIOS continues
    }
    if (physical_addr == 0x1f801030) {
        LOG_INTERCONNECT_DEBUG("[INTERCONNECT] BIOS reading 8-bit from 0x1f801030 (unknown register) - returning 0x00");
        return 0x00; // Return 0 for now to see if BIOS continues
    }
    
    if (physical_addr >= 0x1f801800 && physical_addr <= 0x1f801803) {
        return cdrom_read8(&inter->cdrom, physical_addr);
    }

    const bool is_expansion3 =
        (physical_addr >= 0x1FA00000 && physical_addr < 0x1FC00000) ||
        (physical_addr >= 0x0FA00000 && physical_addr < 0x0FC00000);

    if (is_expansion3) {
        return 0x00;
    }

    if (physical_addr >= EXPANSION_1_START && physical_addr <= EXPANSION_1_END) {
         return 0x00;
     }

    // BIOS Region
     if (physical_addr >= BIOS_START && physical_addr <= BIOS_END) {
        uint32_t offset = physical_addr - BIOS_START;
        if (offset < BIOS_SIZE) {
             // Implement bios_load8 if needed, or read directly:
             return inter->bios->data[offset];
        } else {
             LOG_INTERCONNECT_ERROR("[INTERCONNECT] BIOS Load8 out of bounds: offset 0x%x", offset);
             return 0; // Error
        }
    }

    // Main RAM Region
    if (physical_addr <= RAM_END) {
        return ram_load8(inter->ram, physical_addr);
    }

    // Other regions (SPU, Timers, GPU, DMA, Exp2, MemCtrl) are less likely for 8-bit reads

    // SIO (Serial I/O) - JOY registers 0x1F801040-0x1F80104F
    // MUST be before the generic hwregs catch-all below.
    if (physical_addr >= 0x1f801040 && physical_addr <= 0x1f80104f) {
        uint32_t offset = physical_addr - 0x1f801040;
        return sio_read8(&inter->sio, offset);
    }

    // --- Hardware Register Checks (Specific Addresses First) ---
    if (physical_addr >= 0x1f801000 && physical_addr <= 0x1f801fff) {
        uint32_t offset = physical_addr - 0x1f801000;
        return hwregs[offset];
    }

    // --- Fallback ---
    if ((physical_addr & 0xFFFF0000) == 0xFFFF0000) {
        return 0;
    }
    
    // --- PCSX ReARMed-style Memory Region Handling for 8-bit ---
     // Handle the 0x24xxxxxx range that's causing your errors
     if (physical_addr >= 0x20000000 && physical_addr <= 0x2FFFFFFF) {
return 0;
     }
     
     // Handle other unmapped regions (0x30xxxxxx - 0x7xxxxxxx)
     if (physical_addr >= 0x30000000 && physical_addr <= 0x7FFFFFFF) {
return 0;
     }
     
    if (physical_addr >= 0xf0000000) {
        LOG_INTERCONNECT_TRACE("[INTERCONNECT] Unmapped memory read8 0x%08x", physical_addr);
        return 0;
    }

    LOG_INTERCONNECT_ERROR("[INTERCONNECT] Unhandled physical memory read8 at address: 0x%08x (Mapped from 0x%08x)", physical_addr, address);
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

    if (physical_addr >= 0x1f801800 && physical_addr <= 0x1f801803) {
        LOG_CDROM_WARN("[INTERCONNECT] CDROM register WRITE32 at 0x%08x = 0x%08x (UNEXPECTED SIZE)", physical_addr, value);
    }
    if (address % 4 != 0) {
        LOG_INTERCONNECT_ERROR("[INTERCONNECT] Unaligned store32 address: 0x%08x = 0x%08x", address, value);
        // Trigger Address Error Store exception directly if CPU pointer is set
        if (inter->cpu) {
            inter->cpu->badvaddr = address;
            cpu_exception(inter->cpu, EXCEPTION_STORE_ADDRESS_ERROR);
        }
        return;
    }

    // --- Check Timer Range --- <<< ADD THIS BLOCK
    if (physical_addr >= TIMERS_START && physical_addr <= TIMERS_END) {
        uint32_t timer_base_offset = physical_addr - TIMERS_START;
        int timer_index = timer_base_offset / 0x10;
        uint32_t register_offset = physical_addr & 0xF;

        timer_write32(&inter->timers_state, timer_index, register_offset, value);
        return; // Handled
    }
    // --- Hardware Register Checks (Specific Addresses First) ---

    // Interrupt Controller Registers
    if (physical_addr == IRQ_STATUS_ADDR) { // 0x1f801070 (I_STAT)
        static const char* const irq_names[] = {
            "VBLANK","GPU","CDROM","DMA","TMR0","TMR1","TMR2","PAD","SIO","SPU","IRQ10"
        };
        const uint16_t write_value = (uint16_t)(value & 0x7FF);
        const uint16_t cleared_bits = (uint16_t)(inter->irq_status & ~write_value);
        for (uint32_t i = 0; i < 11; i++) {
            if (cleared_bits & (1u << i))
                LOG_IRQ_DEBUG("%s IRQ cleared", irq_names[i]);
        }
        inter->irq_status &= write_value;
        inter->irq_line_state &= write_value;
        if ((cleared_bits & (1u << IRQ_SPU)) && inter->spu.irq9_flag) {
            inter->spu.irq9_flag = false;
            inter->spu.status &= ~SPU_STATUS_IRQ9_FLAG;
            interconnect_set_irq_line(inter, IRQ_SPU, false);
            LOG_IRQ_DEBUG("SPU IRQ9 edge-trigger reset (I_STAT[9] cleared)");
        }
        if (inter->cpu) inter->cpu->downcount = 0;
        return;
    }
    if (physical_addr == IRQ_MASK_ADDR) { // 0x1f801074 (I_MASK)
        inter->irq_mask = (uint16_t)(value & 0x7FF);
        LOG_IRQ_DEBUG("I_MASK <- 0x%03x", value & 0x7FF);
        if (inter->cpu) inter->cpu->downcount = 0;
        return;
    }

    // Memory Control Registers (0x1f801000 - 0x1f801020)
    // PSX-SPEX: BIOS writes to these to configure memory timings and sizes
    // For now, we acknowledge writes but don't implement actual delay/size changes
    if (physical_addr >= 0x1f801000 && physical_addr <= 0x1f801020) {
// TODO: Implement actual memory configuration changes if needed
        return;
    }

    // Cache Control (KSEG2)
    if (physical_addr == CACHE_CONTROL_ADDR) {
// Cache not implemented yet
        return;
    }

    // GPU Registers
    if (physical_addr == GPU_GP0_ADDR) { // 0x1f801810 (Write = GP0)
        if (log_get_level() >= LOG_LEVEL_TRACE) {
}
        gpu_gp0(&inter->gpu, value); // Delegate to GPU module
        return;
    }
    if (physical_addr == GPU_GP1_ADDR) { // 0x1f801814 (Write = GP1)
        if (log_get_level() >= LOG_LEVEL_TRACE) {
}
        gpu_gp1(&inter->gpu, value); // Delegate to GPU module
        return;
    }

    // SIO (Serial I/O) - JOY registers 0x1F801040-0x1F80104F
    if (physical_addr >= 0x1f801040 && physical_addr <= 0x1f80104f) {
        uint32_t offset = physical_addr - 0x1f801040;
        sio_write32(&inter->sio, offset, value);
        if (inter->sio.pending_irq) {
            inter->sio.pending_irq = false;
            interconnect_set_irq_line(inter, IRQ_CTRLMEMCARD, true);
            interconnect_set_irq_line(inter, IRQ_CTRLMEMCARD, false);
        }
        return;
    }


    // --- Region Checks (Broader Ranges) ---

    // MDEC Region (0x1f801820 - 0x1f801827) - Not yet implemented
    if (physical_addr >= 0x1f801820 && physical_addr <= 0x1f801827) {
        LOG_INTERCONNECT_WARN("[INTERCONNECT] MDEC write at 0x%08x = 0x%08x (stub)", physical_addr, value);
        return;
    }

    // DMA Region
    if (physical_addr >= DMA_START && physical_addr <= DMA_END) {
        uint32_t offset = physical_addr - DMA_START;
        bool channel_became_active = dma_write(&inter->dma, offset, value);
        if (channel_became_active) {
            uint32_t channel_index = (offset >> 4) & 0x7;
            LOG_DMA_DEBUG("[DMA] Channel %d activated (offset=0x%x)", channel_index, offset);
            interconnect_perform_dma(inter, channel_index);
        }
        return;
    }

    // Scratchpad Region (0x1f800000 - 0x1f8003ff) - 1KB Fast RAM
    if (physical_addr >= SCRATCHPAD_START && physical_addr <= SCRATCHPAD_END) {
        uint32_t offset = physical_addr - SCRATCHPAD_START;
        if (offset + 3 < SCRATCHPAD_SIZE) {  // Ensure aligned 32-bit write within bounds
            // Write 32-bit value to scratchpad (little-endian)
            inter->scratchpad[offset] = (uint8_t)(value);
            inter->scratchpad[offset + 1] = (uint8_t)(value >> 8);
            inter->scratchpad[offset + 2] = (uint8_t)(value >> 16);
            inter->scratchpad[offset + 3] = (uint8_t)(value >> 24);
}
        return;
    }

    // Memory Control Region (Includes IRQ regs handled above, RAM_SIZE reg)
    if (physical_addr >= MEM_CONTROL_START && physical_addr <= MEM_CONTROL_END) {
        // Handle specific MemCtrl writes, ignore others silently for now
        switch (physical_addr) {
            case EXPANSION_1_BASE_ADDR: // 0x1f801000
                if ((uint32_t)value != 0x1f000000) LOG_INTERCONNECT_WARN("[INTERCONNECT] Bad Expansion 1 base address write: 0x%08x", value);
                else 
                break;
            case EXPANSION_2_BASE_ADDR: // 0x1f801004
                 if (value != 0x1f802000) LOG_INTERCONNECT_WARN("[INTERCONNECT] Bad Expansion 2 base address write: 0x%08x", value);
                 else 
                break;
            case RAM_SIZE_ADDR: // 0x1f801060
break;
            // IRQ regs handled above
            default:
                break;
        }
        return;
    }

    // Timer Region
    if (physical_addr >= TIMERS_START && physical_addr <= TIMERS_END) {
// TODO: Implement Timer register writes
         return;
    }

    // SPU Region
        if (physical_addr >= SPU_START && physical_addr <= SPU_END) {
            spu_write32(inter, physical_addr, value);
            return;
        }

    // Main RAM Region
    if (physical_addr <= RAM_END) {
        // DEBUG: Log writes to exception handler region
        if (physical_addr <= 0x100) {
}
        // DEBUG: Log writes to menu graphics RAM area
        static uint32_t menu_gfx_write_count = 0;
        if (physical_addr >= 0x00074c70 && physical_addr < 0x00080000) {
            if (menu_gfx_write_count < 100) {
                LOG_CPU_INFO("[INTERCONNECT] STORE32: addr=0x%08x value=0x%08x @ 0x%08x", 
                            physical_addr, value, inter->cpu ? inter->cpu->current_pc : 0);
                menu_gfx_write_count++;
            }
        }
        ram_store32(inter->ram, physical_addr, value); // Delegate
        return;
    }

    // BIOS Region (Read-Only)
    if (physical_addr >= BIOS_START && physical_addr <= BIOS_END) {
        LOG_INTERCONNECT_ERROR("[INTERCONNECT] Error: Write attempt to BIOS ROM at address: 0x%08x = 0x%08x",
                physical_addr, value);
        return; // Writes to BIOS are ignored/prohibited
    }

    // Expansion Regions (Generally ignored)
    if ((physical_addr >= EXPANSION_1_START && physical_addr <= EXPANSION_1_END) ||
        (physical_addr >= EXPANSION_2_START && physical_addr <= EXPANSION_2_END)) {
return;
    }

    // --- Hardware Register Checks (Specific Addresses First) ---
    if (physical_addr >= 0x1f801000 && physical_addr <= 0x1f801fff) {
        uint32_t offset = physical_addr - 0x1f801000;
        
        // TEST: Add missing hardware register write handlers
        if (physical_addr == 0x1f801010) {
            LOG_INTERCONNECT_DEBUG("[INTERCONNECT] BIOS writing 0x%08x to 0x1f801010 (unknown register)", value);
            return; // Don't store, just log
        }
        if (physical_addr == 0x1f801020) {
            LOG_INTERCONNECT_DEBUG("[INTERCONNECT] BIOS writing 0x%08x to 0x1f801020 (unknown register)", value);
            return; // Don't store, just log
        }
        if (physical_addr == 0x1f801030) {
            LOG_INTERCONNECT_DEBUG("[INTERCONNECT] BIOS writing 0x%08x to 0x1f801030 (unknown register)", value);
            return; // Don't store, just log
        }
        
        hwregs[offset] = value;
        return;
    }

    // --- then generic MEM_CONTROL region handler ...
    if (physical_addr >= MEM_CONTROL_START && physical_addr <= MEM_CONTROL_END) {
        switch (physical_addr) {
            case EXPANSION_1_BASE_ADDR: // 0x1f801000
                if ((uint32_t)value != 0x1f000000) LOG_INTERCONNECT_WARN("[INTERCONNECT] Bad Expansion 1 base address write: 0x%08x", value);
                else 
                break;
            case EXPANSION_2_BASE_ADDR: // 0x1f801004
                 if (value != 0x1f802000) LOG_INTERCONNECT_WARN("[INTERCONNECT] Bad Expansion 2 base address write: 0x%08x", value);
                 else 
                break;
            case RAM_SIZE_ADDR: // 0x1f801060
break;
            // IRQ regs handled above
            default:
                break;
        }
        return;
    }

    // --- PCSX ReARMed-style Memory Region Handling for write32 ---
    // Handle the 0x24xxxxxx range that's causing your errors
    if (physical_addr >= 0x20000000 && physical_addr <= 0x2FFFFFFF) {
return; // Ignore writes to unmapped memory
    }
    
    // Handle other unmapped regions (0x30xxxxxx - 0x7xxxxxxx)
    if (physical_addr >= 0x30000000 && physical_addr <= 0x7FFFFFFF) {
return; // Ignore writes to unmapped memory
    }
    
    // Handle the 0xf0000000 range that's causing the infinite loop
    if (physical_addr >= 0xf0000000 && physical_addr <= 0xffffffff) {
        // Only log the first few times to avoid spam
        static uint32_t f000_write_count = 0;
        f000_write_count++;
        if (f000_write_count <= 5) {
            LOG_INTERCONNECT_WARN("[INTERCONNECT] Unmapped memory write (32-bit): 0x%08x = 0x%08x (ignoring, count=%u)", physical_addr, value, f000_write_count);
        }
        return; // Ignore writes to unmapped memory
    }

    // Expansion 3 Region (aliases 0x1FAxxxxx and 0x0FAxxxxx) - usually unpopulated
    if ((physical_addr >= 0x1FA00000 && physical_addr < 0x1FC00000) ||
        (physical_addr >= 0x0FA00000 && physical_addr < 0x0FC00000)) {
        static uint32_t exp3_write_count = 0;
        exp3_write_count++;
        if (exp3_write_count <= 5) {
}
        return; // Ignore writes to unpopulated expansion slot
    }
    
    // --- Fallback ---
    LOG_INTERCONNECT_ERROR("[INTERCONNECT] Unhandled physical memory write32 at address: 0x%08x = 0x%08x (Mapped from 0x%08x)",
            physical_addr, value, address);
}

void interconnect_store16(Interconnect* inter, uint32_t address, uint16_t value) {
    uint32_t physical_addr = mask_region(address);

    if (physical_addr == 0x1f801da8) {
        /* SPU transfer data — handled by SPU write below */
    }
    if (physical_addr >= 0x1f801800 && physical_addr <= 0x1f801803) {
        LOG_INTERCONNECT_DEBUG("[INTERCONNECT] CDROM register WRITE16 at 0x%08x = 0x%04x (UNEXPECTED SIZE)", physical_addr, value);
    }
    if (address % 2 != 0) {
        LOG_INTERCONNECT_ERROR("[INTERCONNECT] Unaligned store16 address: 0x%08x = 0x%04x", address, value);
        // Trigger Address Error Store exception directly if CPU pointer is set
        if (inter->cpu) {
            inter->cpu->badvaddr = address;
            cpu_exception(inter->cpu, EXCEPTION_STORE_ADDRESS_ERROR);
        }
        return;
    }
    if (physical_addr >= TIMERS_START && physical_addr <= TIMERS_END) {
        uint32_t timer_base_offset = physical_addr - TIMERS_START;
        int timer_index = timer_base_offset / 0x10;
        uint32_t register_offset = physical_addr & 0xF;
        timer_write16(&inter->timers_state, timer_index, register_offset, value);
        return;
     }
     
     // TEST: Add missing hardware register write handlers for 16-bit access
     if (physical_addr == 0x1f801010) {
         LOG_INTERCONNECT_DEBUG("[INTERCONNECT] BIOS writing 16-bit 0x%04x to 0x1f801010 (unknown register)", value);
         return; // Don't store, just log
     }
     if (physical_addr == 0x1f801020) {
         LOG_INTERCONNECT_DEBUG("[INTERCONNECT] BIOS writing 16-bit 0x%04x to 0x1f801020 (unknown register)", value);
         return; // Don't store, just log
     }
     if (physical_addr == 0x1f801030) {
         LOG_INTERCONNECT_DEBUG("[INTERCONNECT] BIOS writing 16-bit 0x%04x to 0x1f801030 (unknown register)", value);
         return; // Don't store, just log
     }
     
    // Interrupt Controller Registers
    if (physical_addr == IRQ_STATUS_ADDR) { // 0x1f801070 (I_STAT)
        static const char* const irq_names[] = {
            "VBLANK","GPU","CDROM","DMA","TMR0","TMR1","TMR2","PAD","SIO","SPU","IRQ10"
        };
        const uint16_t cleared_bits = (uint16_t)(inter->irq_status & ~value);
        for (uint32_t i = 0; i < 11; i++) {
            if (cleared_bits & (1u << i))
                LOG_IRQ_DEBUG("%s IRQ cleared", irq_names[i]);
        }
        inter->irq_status &= value;
        inter->irq_line_state &= value;
        if ((cleared_bits & (1u << IRQ_SPU)) && inter->spu.irq9_flag) {
            inter->spu.irq9_flag = false;
            inter->spu.status &= ~SPU_STATUS_IRQ9_FLAG;
            interconnect_set_irq_line(inter, IRQ_SPU, false);
            LOG_IRQ_DEBUG("SPU IRQ9 edge-trigger reset (I_STAT[9] cleared)");
        }
        if (inter->cpu) inter->cpu->downcount = 0;
        if ((value & (1 << TIMER0_IRQ)) == 0) {
            inter->timers_state.timers[0].interrupt_requested = false;
            inter->timers_state.timers[0].mode &= ~(1 << 10);
        }
        if ((value & (1 << TIMER1_IRQ)) == 0) {
            inter->timers_state.timers[1].interrupt_requested = false;
            inter->timers_state.timers[1].mode &= ~(1 << 10);
        }
        if ((value & (1 << TIMER2_IRQ)) == 0) {
            inter->timers_state.timers[2].interrupt_requested = false;
            inter->timers_state.timers[2].mode &= ~(1 << 10);
        }
        return;
    }
    if (physical_addr == IRQ_MASK_ADDR) { // 0x1f801074 (I_MASK)
        inter->irq_mask = value & 0x7FF;
        LOG_IRQ_DEBUG("I_MASK <- 0x%03x", value & 0x7FF);
        if (inter->cpu) inter->cpu->downcount = 0;
        return;
    }

    // SPU Region
        if (physical_addr >= SPU_START && physical_addr <= SPU_END) {
            spu_write16(inter, physical_addr, value);
            return;
        }

    // Timer Region
    if (physical_addr >= TIMERS_START && physical_addr <= TIMERS_END) {
        uint32_t timer_base_offset = physical_addr - TIMERS_START;
        int timer_index = timer_base_offset / 0x10;
        uint32_t register_offset = physical_addr & 0xF;

        timer_write16(&inter->timers_state, timer_index, register_offset, value);
        return; // Handled
    }

    if ((physical_addr >= 0x1FA00000 && physical_addr < 0x1FC00000) ||
        (physical_addr >= 0x0FA00000 && physical_addr < 0x0FC00000)) {
        static uint32_t exp3_store16_count = 0;
        exp3_store16_count++;
        if (exp3_store16_count <= 5) {
}
        return;
    }

    // Main RAM Region
    if (physical_addr <= RAM_END) {
        // DEBUG: Log writes to exception handler region
        if (physical_addr <= 0x100) {
}
        ram_store16(inter->ram, physical_addr, value); // Delegate
        return;
    }

    // SIO (Serial I/O) - JOY registers 0x1F801040-0x1F80104F
    // MUST be checked before MEM_CONTROL_END (0x1F80107F) which otherwise swallows this range.
    if (physical_addr >= 0x1f801040 && physical_addr <= 0x1f80104f) {
        uint32_t offset = physical_addr - 0x1f801040;
        sio_write16(&inter->sio, offset, value);
        if (inter->sio.pending_irq) {
            inter->sio.pending_irq = false;
            interconnect_set_irq_line(inter, IRQ_CTRLMEMCARD, true);
            interconnect_set_irq_line(inter, IRQ_CTRLMEMCARD, false);
        }
        return;
    }

    // Memory Control Region (General - unlikely 16-bit writes)
     if (physical_addr >= MEM_CONTROL_START && physical_addr <= MEM_CONTROL_END) {
return;
     }

    // BIOS Region (Read-Only)
    if (physical_addr >= BIOS_START && physical_addr <= BIOS_END) {
        LOG_INTERCONNECT_ERROR("[INTERCONNECT] Error: Write16 attempt to BIOS ROM at address: 0x%08x = 0x%04x",
                physical_addr, value);
        return;
    }

    // GPU Region (Unlikely 16-bit writes)
    if (physical_addr >= GPU_START && physical_addr <= GPU_END) {
         LOG_INTERCONNECT_WARN("[INTERCONNECT] Unhandled GPU write16 at 0x%08x = 0x%04x", physical_addr, value);
         return;
    }

    // DMA Region (Unlikely 16-bit writes)
     if (physical_addr >= DMA_START && physical_addr <= DMA_END) {
        LOG_DMA_WARN("[DMA] Unhandled write16 at 0x%08x = 0x%04x", physical_addr, value);
        return;
    }

    // Expansion Regions
    if ((physical_addr >= EXPANSION_1_START && physical_addr <= EXPANSION_1_END) ||
        (physical_addr >= EXPANSION_2_START && physical_addr <= EXPANSION_2_END)) {
return;
    }

    // Logging: Suppress IO WRITE16 logs for polled SPU/controller addresses unless LOG_LEVEL_TRACE is enabled.
    // This prevents log flooding from BIOS polling loops.
    if ((physical_addr >= 0x1f801d80 && physical_addr <= 0x1f801dbf) ||
        physical_addr == 0x1f801da8 || physical_addr == 0x1f801daa || physical_addr == 0x1f801dac || physical_addr == 0x1f801dae) {
return;
    }

    // Handle MEM_CONTROL before generic region
    if (physical_addr >= 0x1f801000 && physical_addr <= 0x1f801fff) {
        uint32_t offset = physical_addr - 0x1f801000;
        *(uint16_t *)&hwregs[offset] = value;
        return;
    }

    // --- then generic MEM_CONTROL region handler ...
    if (physical_addr >= MEM_CONTROL_START && physical_addr <= MEM_CONTROL_END) {
        switch (physical_addr) {
            case EXPANSION_1_BASE_ADDR: // 0x1f801000
                if ((uint32_t)value != 0x1f000000) LOG_INTERCONNECT_WARN("[INTERCONNECT] Bad Expansion 1 base address write: 0x%08x", value);
                else 
                break;
            case EXPANSION_2_BASE_ADDR: // 0x1f801004
                 if (value != 0x1f802000) LOG_INTERCONNECT_WARN("[INTERCONNECT] Bad Expansion 2 base address write: 0x%08x", value);
                 else 
                break;
            case RAM_SIZE_ADDR: // 0x1f801060
break;
            // IRQ regs handled above
            default:
                break;
        }
        return;
    }

    LOG_INTERCONNECT_ERROR("[INTERCONNECT] Unhandled physical memory write16 at address: 0x%08x = 0x%04x (Mapped from 0x%08x)",
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

    // --- Check Timer Range ---
    if (physical_addr >= TIMERS_START && physical_addr <= TIMERS_END) {
        // 8-bit writes to timers are generally undefined or write partial registers.
        LOG_INTERCONNECT_WARN("[INTERCONNECT] Unhandled 8-bit write to Timer range: 0x%08x = 0x%02x", physical_addr, value);
        // Ignoring is safest for now.
        return;
    }
    
    // TEST: Add missing hardware register write handlers for 8-bit access
    if (physical_addr == 0x1f801010) {
        LOG_INTERCONNECT_DEBUG("[INTERCONNECT] BIOS writing 8-bit 0x%02x to 0x1f801010 (unknown register)", value);
        return; // Don't store, just log
    }
    if (physical_addr == 0x1f801020) {
        LOG_INTERCONNECT_DEBUG("[INTERCONNECT] BIOS writing 8-bit 0x%02x to 0x1f801020 (unknown register)", value);
        return; // Don't store, just log
    }
    if (physical_addr == 0x1f801030) {
        LOG_INTERCONNECT_DEBUG("[INTERCONNECT] BIOS writing 8-bit 0x%02x to 0x1f801030 (unknown register)", value);
        return; // Don't store, just log
    }
    
    if (physical_addr >= 0x1f801800 && physical_addr <= 0x1f801803) {
        cdrom_write8(&inter->cdrom, physical_addr, value);
        return;
    }

     // Expansion 2 (0x1F802000-0x1F803FFF) — BIOS POST codes + optional DUART TTY
    // SCPH-1001 only writes POST codes (non-printable) to offset 0x041.
    // Any printable ASCII write to any EXP2 offset is captured as TTY output.
    if (physical_addr >= EXPANSION_2_START && physical_addr <= EXPANSION_2_END) {
        char ch = (char)(value & 0xFF);
        if ((uint8_t)ch >= 0x20 && (uint8_t)ch < 0x7F) {
            if (inter->tty_line_len < (int)(sizeof(inter->tty_line_buf) - 1))
                inter->tty_line_buf[inter->tty_line_len++] = ch;
        } else if (ch == '\n' || ch == '\r') {
            if (inter->tty_line_len > 0) {
                inter->tty_line_buf[inter->tty_line_len] = '\0';
                LOG_BIOS_INFO("[INTERCONNECT] [EXP2] %s", inter->tty_line_buf);
                inter->tty_line_len = 0;
            }
        }
        return;
    }

    // SPU Region
        if (physical_addr >= SPU_START && physical_addr <= SPU_END) {
            spu_write8(inter, physical_addr, value);
            return;
        }

     // Main RAM Region
    if (physical_addr <= RAM_END) {
        // DEBUG: Log writes to exception handler region
        if (physical_addr <= 0x100) {
}
        ram_store8(inter->ram, physical_addr, value); // Delegate
        return;
    }

    // SIO (Serial I/O) - JOY registers 0x1F801040-0x1F80104F
    // MUST be checked before MEM_CONTROL_END (0x1F80107F).
    if (physical_addr >= 0x1f801040 && physical_addr <= 0x1f80104f) {
        uint32_t offset = physical_addr - 0x1f801040;
        sio_write8(&inter->sio, offset, value);
        if (inter->sio.pending_irq) {
            inter->sio.pending_irq = false;
            interconnect_set_irq_line(inter, IRQ_CTRLMEMCARD, true);
            interconnect_set_irq_line(inter, IRQ_CTRLMEMCARD, false);
        }
        return;
    }

    // Memory Control Region
    if (physical_addr >= MEM_CONTROL_START && physical_addr <= MEM_CONTROL_END) {
return; // Unlikely target for 8-bit writes
    }

    // BIOS Region (Read-Only)
    if (physical_addr >= BIOS_START && physical_addr <= BIOS_END) {
        LOG_INTERCONNECT_ERROR("[INTERCONNECT] Error: Write8 attempt to BIOS ROM at address: 0x%08x = 0x%02x", physical_addr, value);
        return;
    }

    // Expansion 1 Region
    if (physical_addr >= EXPANSION_1_START && physical_addr <= EXPANSION_1_END) {
return;
    }

    // Handle MEM_CONTROL before generic region
    if (physical_addr >= 0x1f801000 && physical_addr <= 0x1f801fff) {
        uint32_t offset = physical_addr - 0x1f801000;
        hwregs[offset] = value;
        return;
    }

    // --- then generic MEM_CONTROL region handler ...
    if (physical_addr >= MEM_CONTROL_START && physical_addr <= MEM_CONTROL_END) {
        switch (physical_addr) {
            case EXPANSION_1_BASE_ADDR: // 0x1f801000
                if ((uint32_t)value != 0x1f000000) LOG_INTERCONNECT_WARN("[INTERCONNECT] Bad Expansion 1 base address write: 0x%08x", value);
                else 
                break;
            case EXPANSION_2_BASE_ADDR: // 0x1f801004
                 if (value != 0x1f802000) LOG_INTERCONNECT_WARN("[INTERCONNECT] Bad Expansion 2 base address write: 0x%08x", value);
                 else 
                break;
            case RAM_SIZE_ADDR: // 0x1f801060
break;
            // IRQ regs handled above
            default:
                break;
        }
        return;
    }

    LOG_INTERCONNECT_ERROR("[INTERCONNECT] Unhandled physical memory write8 at address: 0x%08x = 0x%02x (Mapped from 0x%08x)",
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
        LOG_DMA_WARN("[DMA] Request sync zero size (BS=%u BC=%u)", bs, bc);
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
        LOG_DMA_ERROR("[DMA] Invalid channel index %u", channel_index);
        return;
    }

    LOG_DMA_DEBUG("[DMA] ch%d start", channel_index);
    DmaChannel* ch = &inter->dma.channels[channel_index];
    DmaSync sync_mode = ch->sync;

    LOG_DMA_DEBUG("[DMA] ch%d sync=%d dir=%d base=0x%08x",
                  channel_index, sync_mode, ch->direction, ch->base_addr);

    switch (sync_mode) {
        case LINKED_LIST:
            // Primarily used for GPU Channel 2
            if (channel_index == 2 && ch->direction == FROM_RAM) {
                uint32_t addr = ch->base_addr & 0x00FFFFFC; // Start address from MADR
                LOG_DMA_DEBUG("[DMA] GPU LL start at 0x%08x", addr);
                while(1) {
                    // Check address bounds before reading header
                    if (addr >= RAM_SIZE) {
                        LOG_DMA_ERROR("[DMA] GPU LL: header 0x%08x out of bounds", addr);
                        break;
                    }
                    // Read header: size in high byte, next address in low 24 bits
                    uint32_t header = interconnect_load32(inter, addr); // Use interconnect load
                    uint32_t num_words = header >> 24;
                    uint32_t next_addr = header & 0x00FFFFFC; // Mask to word boundary

                    // Transfer packet words (if any)
                    if (num_words > 0) {
                         for (uint32_t i = 0; i < num_words; ++i) {
                            addr = (addr + 4) & 0x00FFFFFC; // Advance address for command word
                            if (addr >= RAM_SIZE) { // Check bounds before reading command
                                LOG_DMA_ERROR("[DMA] GPU LL: command addr 0x%08x out of bounds", addr);
                                next_addr = 0xFFFFFF; // Force stop after this packet
                                break; // Exit inner loop
                            }
                            uint32_t command_word = interconnect_load32(inter, addr);
                            LOG_DMA_TRACE("[DMA] GPU LL word=0x%08x addr=0x%08x", command_word, addr);

                            // Respect GPU STAT[26] (GP0 ready) to avoid overflowing the GP0 FIFO
                            {
                                int wait_cycles = 0;
                                while ((gpu_read_status(&inter->gpu) & (1u << 26)) == 0) {
                                    // Let pending events (timers/VBlank) run so the GPU can drain
                                    eventq_dispatch_due(inter);
                                    if (++wait_cycles > 10000) {
                                        LOG_DMA_WARN("[DMA] GPU LL: GP0 ready timeout, proceeding");
                                        break;
                                    }
                                }
                            }
                            gpu_gp0(&inter->gpu, command_word); // Send command to GPU GP0 port
                        }
                        if (next_addr == 0xFFFFFF) break; // Break outer loop if error occurred
                    }

                    // Check for end-of-list marker (Top bit of next_addr usually, or 0xFFFFFF) [cite: 1808]
                    if ((header & 0x800000) != 0) { // Check MSB of address field as per Mednafen comment
                        LOG_DMA_TRACE("[DMA] GPU LL end (0x800000)");
                        break;
                    }
                    // Check for explicit 0xFFFFFF marker (safer)
                    if (next_addr == 0xFFFFFF) {
                        LOG_DMA_TRACE("[DMA] GPU LL end (0xFFFFFF)");
                         break;
                    }

                    // Check next address validity before proceeding
                     if (next_addr >= RAM_SIZE) {
                         LOG_DMA_ERROR("[DMA] GPU LL: next header 0x%08x out of bounds", next_addr);
                         break;
                     }
                    // Move to the next header address
                    addr = next_addr;
                }
                LOG_DMA_DEBUG("[DMA] GPU LL done");
            } else {
                 LOG_DMA_ERROR("[DMA] Linked list on unsupported ch=%d dir=%d", channel_index, ch->direction);
            }
            break;

        case MANUAL:
        case REQUEST:
            {
                uint32_t words_to_transfer = dma_get_transfer_size_words(ch);
                if (words_to_transfer == 0) {
                    LOG_DMA_WARN("[DMA] ch%d block/request with zero size", channel_index);
                    break; // Nothing to do
                }

                uint32_t addr = ch->base_addr & 0x00FFFFFC; // Start address
                int32_t step = (ch->step == INCREMENT) ? 4 : -4;
                LOG_DMA_DEBUG("[DMA] ch%d %s %s step=%d addr=0x%08x words=%u",
                       channel_index, (ch->direction == FROM_RAM ? "FROM_RAM" : "TO_RAM"),
                       (sync_mode == MANUAL ? "MANUAL" : "REQUEST"), step, addr, words_to_transfer);

                for (uint32_t i = 0; i < words_to_transfer; ++i) {
                    // Ensure address stays within RAM bounds (mask low bits, check high bits)
                    uint32_t current_addr_masked = addr & 0x001FFFFC; // Mask address to stay within 2MB and word aligned
                    if (current_addr_masked >= RAM_SIZE) {
                         LOG_DMA_ERROR("[DMA] ch%d addr 0x%08x (masked 0x%08x) out of RAM bounds", channel_index, addr, current_addr_masked);
                         break; // Stop transfer if address goes out of bounds
                    }

                    if (ch->direction == FROM_RAM) {
                    // RAM -> Peripheral
                    uint32_t data_word = interconnect_load32(inter, current_addr_masked); // Read from RAM

                    switch (channel_index) {                            case 2: // GPU
                                // Respect GP0 FIFO / GPUSTAT ready bit before sending data words
                                {
                                    int wait_cycles = 0;
                                    while ((gpu_read_status(&inter->gpu) & (1u << 26)) == 0) {
                                        eventq_dispatch_due(inter);
                                        if (++wait_cycles > 10000) {
                                            LOG_DMA_WARN("[DMA] GPU GP0 ready timeout in block transfer, proceeding");
                                            break;
                                        }
                                    }
                                }
                                gpu_gp0(&inter->gpu, data_word); // Send data word to GP0 (for Image Load etc.)
                                break;
                            case 4: { /* SPU: 32-bit word → two halfwords into SPU RAM */
                                uint16_t hw[2] = { (uint16_t)(data_word & 0xFFFF), (uint16_t)(data_word >> 16) };
                                spu_dma_write_halfwords(&inter->spu, inter, hw, 2);
                                break;
                            }
                            default:
                                LOG_DMA_WARN("[DMA] Unhandled FROM_RAM ch%d addr=0x%08x data=0x%08x",
                                       channel_index, current_addr_masked, data_word);
                                break;
                        }
                    } else { // TO_RAM
                        uint32_t data_word = 0;
                        switch (channel_index) {
                            case 6: /* OTC */
                                data_word = (i == (words_to_transfer - 1))
                                            ? 0x00FFFFFF
                                            : ((addr - 4) & 0x00FFFFFC);
                                break;
                            case 2: /* GPU GPUREAD */
                                data_word = gpu_read_data(&inter->gpu);
                                break;
                            case 3: /* CDROM → RAM */
                                data_word = cdrom_dma_read_word(&inter->cdrom);
                                break;
                            case 4: { /* SPU → RAM */
                                uint16_t hw[2];
                                spu_dma_read_halfwords(&inter->spu, inter, hw, 2);
                                data_word = (uint32_t)hw[0] | ((uint32_t)hw[1] << 16);
                                break;
                            }
                            default:
                                LOG_DMA_WARN("[DMA] Unhandled TO_RAM ch%d addr=0x%08x",
                                       channel_index, current_addr_masked);
                                break;
                        }
                        interconnect_store32(inter, current_addr_masked, data_word); // Write to RAM
                    }

                    // Advance address for next word
                    addr = (uint32_t)((int32_t)addr + step); // Apply step
                }
                 LOG_DMA_DEBUG("[DMA] Channel %d transfer complete: %u words", channel_index, words_to_transfer);
            }
            break;

        default: // Should not happen if sync enum is correct
            LOG_DMA_ERROR("[DMA] Unknown sync mode %d on ch%d", sync_mode, channel_index);
            break;
    }

    // Mark the channel as finished (clears enable/trigger bits)
    dma_channel_done(ch);
    LOG_DMA_DEBUG("[DMA] ch%d done", channel_index);

    // Signal DMA completion IRQ (IRQ3) if the channel has IRQ enabled in DICR.
    // Edge-triggered on I_STAT[3]: only raise IRQ3 when I_STAT bit 3 transitions 0→1.
    // Using I_STAT[3] (not channel_irq_flags) as the guard ensures the IRQ re-fires
    // after the BIOS clears I_STAT[3] via direct write (0xFFF7), even if DICR flags
    // were not properly ACKed (e.g. BIOS writes ack_flags for wrong channel).
    if (inter->dma.channel_irq_enable & (1 << channel_index)) {
        inter->dma.channel_irq_flags |= (1 << channel_index);
        inter->dma.master_irq_flag = inter->dma.force_irq ||
            (inter->dma.master_irq_enable &&
             (inter->dma.channel_irq_flags & inter->dma.channel_irq_enable) != 0);
        if (inter->dma.master_irq_flag && !(inter->irq_status & (1u << IRQ_DMA))) {
            interconnect_request_irq(inter, IRQ_DMA, "DMA channel done");
        }
    }
}

// --- BIOS Boot Helper: Force Interrupt Configuration ---
// Per PSX-Spex/nocash, if the BIOS doesn't configure I_MASK for IRQ0,
// we need to force it to allow VBlank IRQ0 processing.
static void interconnect_force_bios_boot_config(Interconnect* inter) {
    static bool test_pattern_drawn = false;
    
    // Only force configuration if I_MASK is not already configured for IRQ0
    if ((inter->irq_mask & 0x0001) == 0) {
        LOG_INTERCONNECT_DEBUG("[INTERCONNECT] BIOS Boot Helper: Forcing I_MASK configuration for IRQ0");
        
        // Enable IRQ0 (VBlank) in I_MASK
        inter->irq_mask |= 0x0001;  // Bit 0: IRQ0 enable
        
        LOG_INTERCONNECT_DEBUG("[INTERCONNECT] Forced I_MASK=0x%04x [PSX-Spex: IRQ0 enabled]", inter->irq_mask);
    }
    
    // Also force enable GPU display if it's disabled to fix black screen
    if (inter->gpu.display_disabled) {
        LOG_INTERCONNECT_DEBUG("[INTERCONNECT] BIOS Boot Helper: Forcing GPU display enable to fix black screen");
        inter->gpu.display_disabled = false;
        
        // Set reasonable display ranges (NTSC 320x240)
        inter->gpu.display_horiz_start = 0x200;  // 512
        inter->gpu.display_horiz_end = 0xC00;    // 3072
        inter->gpu.display_line_start = 0x10;    // 16
        inter->gpu.display_line_end = 0x100;     // 256
        
        LOG_INTERCONNECT_DEBUG("[INTERCONNECT] Forced GPU display enable with NTSC 320x240 ranges");
        
        // Only draw test pattern once to avoid overriding BIOS display
        if (!test_pattern_drawn) {
            LOG_INTERCONNECT_DEBUG("[INTERCONNECT] Drawing test pattern to verify GPU functionality");
            
            // Draw a simple test pattern to verify GPU is working
            // Clear screen with dark blue background
            gpu_gp0(&inter->gpu, 0x02); // Clear cache command
            gpu_gp0(&inter->gpu, 0x00000000); // Black background
            
            // Draw a simple colored rectangle in the center
            gpu_gp0(&inter->gpu, 0x60); // Monochrome rectangle command
            gpu_gp0(&inter->gpu, 0x00FF0000); // Red color
            gpu_gp0(&inter->gpu, 0x00640064); // X=100, Y=100
            gpu_gp0(&inter->gpu, 0x00640064); // Width=100, Height=100
            
            test_pattern_drawn = true;
            LOG_INTERCONNECT_DEBUG("[INTERCONNECT] Test pattern drawn successfully");
        }
    }
}

// Simplified BIOS boot helper - no recursive calls
void interconnect_check_bios_boot(Interconnect* inter) {
    (void)inter;
}

// Helper: Perform the actual GPU DMA transfer for channel 2
void perform_gpu_dma_transfer(struct Interconnect* sys, DmaChannel* ch) {
    LOG_DMA_DEBUG("[DMA] GPU transfer sync=%d dir=%d", ch->sync, ch->direction);
    // FROM_RAM: RAM -> GPU (Image Load)
    if (ch->direction == FROM_RAM) {
        uint32_t addr = ch->base_addr & 0x001FFFFC; // 2MB RAM, word aligned
        uint32_t words = (ch->block_count == 0 ? 1 : ch->block_count) * (ch->block_size == 0 ? 1 : ch->block_size);
        if (words == 0) words = 1;
        
        // Log source address for debugging menu graphics
        LOG_DMA_DEBUG("[DMA] GPU FROM_RAM base=0x%08x words=%u", ch->base_addr, words);
        
        // This function is only called for Linked List DMA (logo), not Block/Request (menu)
        for (uint32_t i = 0; i < words; ++i) {
            uint32_t data_word = ram_load32(sys->ram, addr);
            // Respect GPU STAT[26] (GP0 ready) to avoid overflowing hardware FIFO
            int waited = 0;
            while ((gpu_read_status(&sys->gpu) & (1u << 26)) == 0) {
                // Small busy-wait; log once when we actually wait to verify backpressure
                if (waited == 0) 
                waited++;
                if (waited > 1000) break; // safety cap to avoid infinite hang in broken cases
            }
            if (waited > 0) LOG_DMA_DEBUG("[DMA] GPU: waited %d iterations for ready", waited);
            gpu_gp0(&sys->gpu, data_word); // Send to GP0 (Image Load)
            addr += 4;
        }
        LOG_DMA_DEBUG("[DMA] GPU FROM_RAM done: %u words from 0x%08x", words, ch->base_addr);
    }
    // TO_RAM: GPU -> RAM (Image Read)
    else if (ch->direction == TO_RAM) {
        uint32_t addr = ch->base_addr & 0x001FFFFC;
        uint32_t words = (ch->block_count == 0 ? 1 : ch->block_count) * (ch->block_size == 0 ? 1 : ch->block_size);
        if (words == 0) words = 1;
        for (uint32_t i = 0; i < words; ++i) {
            uint32_t data_word = gpu_read_data(&sys->gpu);
            ram_store32(sys->ram, addr, data_word);
            addr += 4;
        }
        LOG_DMA_DEBUG("[DMA] GPU TO_RAM done: %u words", words);
    }
    // LINKED_LIST: GPU command list
    else if (ch->sync == LINKED_LIST) {
        uint32_t addr = ch->base_addr & 0x001FFFFC;
        uint32_t safety = 0;
        while (safety++ < 0x10000) { // Safety limit to avoid infinite loops
            uint32_t header = ram_load32(sys->ram, addr);
            uint32_t count = (header >> 24) & 0xFF;
            for (uint32_t i = 0; i < count; ++i) {
                addr = (addr + 4) & 0x001FFFFC;
                uint32_t cmd = ram_load32(sys->ram, addr);
                gpu_gp0(&sys->gpu, cmd);
            }
            if (header & 0x800000) break; // End of list
            addr = header & 0x001FFFFC;
        }
        LOG_DMA_DEBUG("[DMA] GPU LL processed");
    }
    else {
        LOG_DMA_WARN("[DMA] GPU: unknown sync=%d dir=%d", ch->sync, ch->direction);
    }

    // --- End of GPU DMA transfer logic ---
    // (Reverted: No DMA IRQ3 signaling here)
}
