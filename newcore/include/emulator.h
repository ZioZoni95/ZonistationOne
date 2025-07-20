#ifndef NEWCORE_EMULATOR_H
#define NEWCORE_EMULATOR_H

#include "cpu.h"
#include "ram.h"
#include "bios.h"
#include "dma.h"
#include "event_scheduler.h"
#include "interconnect.h"
#include "gpu.h" // Add this include for NcGpu

// Scratchpad structure (1KB data cache RAM)
typedef struct NcScratchpad {
    uint8_t data[1024]; // 1KB scratchpad memory
} NcScratchpad;

// Emulator context holding all subsystem state
typedef struct EmulatorContext {
    NcCpu cpu;
    NcRam ram;
    NcBios bios;
    NcDma dma;
    NcEventQueue eventq;
    struct NcInterconnect interconnect;
    NcGpu gpu; // Add GPU subsystem for VRAM access
    NcScratchpad scratchpad; // Add scratchpad for 0x1f800000-0x1f8003ff region
    
    // Interrupt controller state
    uint16_t irq_status; // I_STAT register (pending interrupts)
    uint16_t irq_mask;   // I_MASK register (interrupt enable mask)
    
    // CPU cycle counter for timing simulation
    uint64_t cycle_count;
    
    // ... other subsystems ...
} EmulatorContext;

// Main emulator functions
void nc_cpu_step(EmulatorContext* ctx);
int emulator_run(EmulatorContext* ctx);

#endif // NEWCORE_EMULATOR_H 