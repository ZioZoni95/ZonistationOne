#ifndef INTERCONNECT_H // Include guard
#define INTERCONNECT_H

// DuckStation-style: No event scheduler (VBlank fires directly from main loop)
#include <stdint.h>       // For uint32_t, uint16_t etc.
#include <stdbool.h>      // For bool type

// Forward declaration to avoid circular dependency
struct Cpu;

// Include headers for components accessed via the interconnect
#include "bios/bios_core.h"  // For BiosState structure
#include "ram.h"
#include "dma.h"
#include "gpu.h"
#include "gpu/gpu_thread.h"
#include "timers/timer_core.h"
#include "sio.h"
#include "spu.h"
#include "irq/irq_core.h"    // Modular interrupt system
#include "cdrom/cdrom_core.h" // Modular CDROM system


/* --- Memory Map Definitions (Physical Addresses) ---
 * These define the physical address ranges used by the interconnect
 * after mapping the CPU's virtual address.
 */
#define TIMERS_START 0x1f801100
#define TIMERS_SIZE  0x30 // Covers Timers 0, 1, 2
#define TIMERS_END   (TIMERS_START + TIMERS_SIZE - 1)

// Main RAM (2 Megabytes)
#define RAM_START 0x00000000
#define RAM_SIZE  (2 * 1024 * 1024)
#define RAM_END   (RAM_START + RAM_SIZE - 1)

// BIOS ROM (512 Kilobytes)
#define BIOS_START 0x1fc00000 // Physical start address
#define BIOS_END   (BIOS_START + BIOS_SIZE - 1)

// Scratchpad (1 Kilobyte Data Cache RAM)
#define SCRATCHPAD_START 0x1f800000
#define SCRATCHPAD_SIZE  1024
#define SCRATCHPAD_END   (SCRATCHPAD_START + SCRATCHPAD_SIZE - 1)

// Memory Control Registers (Expansion Base, RAM Size)
#define MEM_CONTROL_START 0x1f801000
#define MEM_CONTROL_SIZE  0x80 // Covers known registers up to IRQ mask+4
#define MEM_CONTROL_END   (MEM_CONTROL_START + MEM_CONTROL_SIZE - 1)
#define EXPANSION_1_BASE_ADDR 0x1f801000
#define EXPANSION_2_BASE_ADDR 0x1f801004
#define RAM_SIZE_ADDR         0x1f801060

// Interrupt Control Registers (I_STAT, I_MASK)
#define IRQ_REGS_START        0x1f801070
#define IRQ_STATUS_ADDR       0x1f801070 // Read: Pending IRQs / Write: Acknowledge IRQs
#define IRQ_MASK_ADDR         0x1f801074 // Read/Write: Enable/Disable IRQ lines
// IRQ_REGS_END now defined in irq/irq_types.h

// DMA Registers
#define DMA_START 0x1f801080
#define DMA_SIZE  0x80 // Covers common registers and 7 channels
#define DMA_END   (DMA_START + DMA_SIZE - 1)

// Timer Registers
#define TIMERS_START 0x1f801100
#define TIMERS_SIZE  0x30 // Covers Timers 0, 1, 2
#define TIMERS_END   (TIMERS_START + TIMERS_SIZE - 1)

// SPU (Sound Processing Unit) Registers
#define SPU_START 0x1f801C00
#define SPU_SIZE  640 // From Nocash specs
#define SPU_END   (SPU_START + SPU_SIZE - 1)

// GPU Registers (GP0, GP1/GPUSTAT)
#define GPU_START 0x1f801810
#define GPU_SIZE  8 // GP0/GPUREAD (0x1810) and GP1/GPUSTAT (0x1814)
#define GPU_END   (GPU_START + GPU_SIZE - 1)
#define GPU_GP0_ADDR     0x1f801810 // Write address for GP0 commands
#define GPU_GPUREAD_ADDR 0x1f801810 // Read address for GPUREAD fifo
#define GPU_GP1_ADDR     0x1f801814 // Write address for GP1 commands
#define GPU_GPUSTAT_ADDR 0x1f801814 // Read address for GPUSTAT register

// Expansion Region 1 (Parallel Port)
#define EXPANSION_1_START 0x1f000000
#define EXPANSION_1_SIZE  (8 * 1024 * 1024)
#define EXPANSION_1_END   (EXPANSION_1_START + EXPANSION_1_SIZE - 1)

// Expansion Region 2 (Debug/Dev Hardware)
#define EXPANSION_2_START 0x1f802000
#define EXPANSION_2_SIZE  66
#define EXPANSION_2_END   (EXPANSION_2_START + EXPANSION_2_SIZE - 1)

// Cache Control Register (KSEG2)
#define CACHE_CONTROL_ADDR 0xfffe0130

/* --- NOTE: Interrupt Line Definitions moved to irq/irq_types.h ---
 * Use IrqLine enum instead of these macros.
 * Macros kept here for backward compatibility during transition.
 */


/* --- Interconnect Structure Definition ---
 * Holds pointers/instances of all main system components accessed via the bus.
 * Routes memory accesses from the CPU to the correct component.
 */
typedef struct Interconnect {
    BiosState bios_state; // Loaded BIOS state (modular system)
    Ram* ram;   // Pointer to the main RAM data buffer
    struct Cpu* cpu; // Pointer to CPU for triggering exceptions
    Gpu gpu;    // GPU state (embedded directly)
    GpuThreadState gpu_thread_state; // GPU threading system
    Dma dma;    // DMA controller state (embedded directly)

    // --- Scratchpad (Fast RAM / D-Cache) ---
    uint8_t scratchpad[SCRATCHPAD_SIZE]; // 1KB fast RAM at 0x1f800000
    
    // --- Interrupt Controller State (Thread-Safe Modular System) ---
    IrqState irq_state;      // Modular interrupt controller with mutex protection
    
    // --- Component State (Modular Systems) ---
    TimersState timers_state;     // Timer controller state (modular)
    CdromState cdrom_state;  // Modular CDROM controller with mutex protection
    Sio sio;                 // Serial I/O (Controller and Memory Card)
    Spu spu;                 // Sound Processing Unit state (minimal)

    // DuckStation-style: No event scheduler
    // - VBlank fires directly from main loop every frame
    // - Timers use direct cycle stepping (timers_add_sysclk_ticks)
    // - CDROM checked per-frame (interconnect_check_cdrom_events)
    uint32_t cpu_cycle_counter;  // Global CPU cycle counter (for debugging/stats)

} Interconnect;

/* --- Function Declarations (Prototypes) --- */

/**
 * @brief Initializes the Interconnect structure.
 * Stores pointers to BIOS/RAM, initializes embedded peripherals (GPU, DMA),
 * and resets interrupt controller state.
 * @param inter Pointer to the Interconnect struct to initialize.
 * @param bios Pointer to the initialized Bios struct.
 * @param ram Pointer to the initialized Ram struct.
 */
void interconnect_init(Interconnect* inter, BiosState* bios, Ram* ram);

/**
 * @brief Sets the CPU pointer for the Interconnect (called after CPU initialization).
 * Allows interconnect to trigger CPU exceptions directly.
 * @param inter Pointer to the Interconnect instance.
 * @param cpu Pointer to the CPU instance.
 */
void interconnect_set_cpu(Interconnect* inter, struct Cpu* cpu);

/**
 * @brief Maps a CPU virtual address (KUSEG/KSEG0/KSEG1) to a physical address.
 * Based on Guide Section 2.38.
 * @param addr The 32-bit virtual address from the CPU.
 * @return The corresponding 32-bit physical address.
 */
uint32_t mask_region(uint32_t addr);

/**
 * @brief Reads a 32-bit word from the emulated system memory space.
 * Handles address mapping, routes the read, checks alignment.
 * @param inter Pointer to the Interconnect instance.
 * @param address The 32-bit virtual address requested by the CPU.
 * @return The 32-bit value read. Returns 0 on unhandled/error cases for now.
 */
uint32_t interconnect_load32(Interconnect* inter, uint32_t address);

/**
 * @brief Reads a 16-bit halfword from the emulated system memory space.
 * Handles address mapping, routes the read, checks alignment.
 * @param inter Pointer to the Interconnect instance.
 * @param address The 32-bit virtual address requested by the CPU.
 * @return The 16-bit value read. Returns 0 on unhandled/error cases for now.
 */
uint16_t interconnect_load16(Interconnect* inter, uint32_t address);

/**
 * @brief Reads an 8-bit byte from the emulated system memory space.
 * Handles address mapping and routes the read.
 * @param inter Pointer to the Interconnect instance.
 * @param address The 32-bit virtual address requested by the CPU.
 * @return The 8-bit value read. Returns 0 on unhandled reads for now.
 */
uint8_t interconnect_load8(Interconnect* inter, uint32_t address);

/**
 * @brief Writes a 32-bit word to the emulated system memory space.
 * Handles address mapping, routes the write, checks alignment.
 * @param inter Pointer to the Interconnect instance.
 * @param address The 32-bit virtual address targeted by the CPU.
 * @param value The 32-bit value to write.
 */
void interconnect_store32(Interconnect* inter, uint32_t address, uint32_t value);

/**
 * @brief Writes a 16-bit halfword to the emulated system memory space.
 * Handles address mapping, routes the write, checks alignment.
 * @param inter Pointer to the Interconnect instance.
 * @param address The 32-bit virtual address targeted by the CPU.
 * @param value The 16-bit value to write.
 */
void interconnect_store16(Interconnect* inter, uint32_t address, uint16_t value);

/**
 * @brief Writes an 8-bit byte to the emulated system memory space.
 * Handles address mapping and routes the write.
 * @param inter Pointer to the Interconnect instance.
 * @param address The 32-bit virtual address targeted by the CPU.
 * @param value The 8-bit value to write.
 */
void interconnect_store8(Interconnect* inter, uint32_t address, uint8_t value);

/**
 * @brief Sets the state of an IRQ line (edge-triggered).
 * Per PSX-SPX: I_STAT bits are edge-triggered, set on 0->1 transition.
 * @param inter Pointer to the Interconnect instance.
 * @param irq_line The interrupt line number (0-10).
 * @param state true = line active, false = line inactive.
 */
void interconnect_set_irq_line(Interconnect* inter, uint32_t irq_line, bool state);

/**
 * @brief Called by peripherals to signal an interrupt request.
 * Sets the corresponding bit in the I_STAT register (edge-triggered).
 * Used by peripherals (e.g., CDROM) to request IRQ2. Logs the source.
 * @param inter Pointer to the Interconnect instance.
 * @param irq_line The interrupt line number (0-10) to request.
 * @param source The source of the interrupt request.
 */
void interconnect_request_irq(Interconnect* inter, uint32_t irq_line, const char* source);

// Debug: Check for accidental clearing of irq_status
void interconnect_debug_check_irq_status(const Interconnect* inter, const char* context);

/**
 * @brief Called by peripherals to clear/deassert an interrupt line.
 * Sets the line state to inactive (for edge detection purposes).
 * Note: I_STAT clearing is done by BIOS writing to I_STAT register.
 * @param inter Pointer to the Interconnect instance.
 * @param irq_line The interrupt line number (0-10) to clear.
 * @param source The source of the interrupt request.
 */
void interconnect_clear_irq(Interconnect* inter, uint32_t irq_line, const char* source);

/**
 * @brief Schedule an event to be executed after a number of cycles.
 * Used by CDROM for async command execution.
 * @param inter Pointer to the Interconnect instance.
 * @param cycles Number of cycles until the event fires.
 * @param callback Function to call when event fires.
 * @param context Context pointer passed to callback.
 * @param name Name of the event (for debugging).
 */
void interconnect_schedule_event(Interconnect* inter, uint32_t cycles,
                                 void (*callback)(void*, uint32_t), void* context,
                                 const char* name);

/**
 * @brief Trigger CDROM IRQ (IRQ2).
 * Called by CDROM when it needs to raise an interrupt.
 * @param inter Pointer to the Interconnect instance.
 */
void interconnect_trigger_cdrom_irq(Interconnect* inter);

/**
 * @brief Check and fire pending CDROM events.
 * Called by main emulation loop every frame/step.
 * @param inter Pointer to the Interconnect instance.
 */
void interconnect_check_cdrom_events(Interconnect* inter);


#endif // INTERCONNECT_H