/* SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 ZioZoni95
 *
 * Part of ZoniStation One, a PlayStation 1 emulator.
 * See LICENSE for the full licence text and THIRD-PARTY.md for the
 * components of this project that have other authors.
 */
#ifndef INTERCONNECT_H // Include guard
#define INTERCONNECT_H

#include "event_scheduler.h" // For EVENT_COUNT and SystemEventType
#include "debugger.h"
#include <stdint.h>       // For uint32_t, uint16_t etc.
#include <stdbool.h>      // For bool type

// Forward declaration to avoid circular dependency
struct Cpu;

// Include headers for components accessed via the interconnect
#include "bios.h"
#include "ram.h"
#include "dma.h"
#include "gpu.h"
#include "timers.h"
#include "cdrom.h"
#include "sio.h"
#include "spu.h"
#include "mdec.h"


/* --- Memory Map Definitions (Physical Addresses) ---
 * These define the physical address ranges used by the interconnect
 * after mapping the CPU's virtual address.
 */

// Main RAM (2 Megabytes)
#define RAM_START 0x00000000
#define RAM_SIZE  (2 * 1024 * 1024)
#define RAM_END   (RAM_START + RAM_SIZE - 1)

// BIOS ROM (512 Kilobytes)
#define BIOS_START 0x1fc00000 // Physical start address
#define BIOS_SIZE  (512 * 1024)
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
#define IRQ_REGS_END          (IRQ_REGS_START + 8 - 1) // Covers I_STAT and I_MASK

// DMA Registers
#define DMA_START 0x1f801080
#define DMA_SIZE  0x80 // Covers common registers and 7 channels
#define DMA_END   (DMA_START + DMA_SIZE - 1)

// Timer Registers
#define TIMERS_START 0x1f801100
#define TIMERS_SIZE  0x30 // Covers Timers 0, 1, 2
#define TIMERS_END   (TIMERS_START + TIMERS_SIZE - 1)

// SPU (Sound Processing Unit) Registers
/* SPU_START, SPU_SIZE, SPU_END defined in spu.h */

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
#define EXPANSION_2_SIZE  0x2000  // Full EXP2 range (DUART at +0x23, POST at +0x41/0x80)
#define EXPANSION_2_END   (EXPANSION_2_START + EXPANSION_2_SIZE - 1)

// Cache Control Register (KSEG2)
#define CACHE_CONTROL_ADDR 0xfffe0130

/* --- Interrupt Line Definitions ---
 * Defines symbolic names for the PSX hardware interrupt request lines (0-10).
 * These correspond to bits in the I_STAT and I_MASK registers.
 */
#define IRQ_VBLANK    0  // GPU VBlank interrupt
#define IRQ_GPU       1  // GPU interrupt (origin depends on GPU config)
#define IRQ_CDROM     2  // CD-ROM controller interrupt
#define IRQ_DMA       3  // DMA controller interrupt
#define IRQ_TIMER0    4  // Timer 0 interrupt
#define IRQ_TIMER1    5  // Timer 1 interrupt
#define IRQ_TIMER2    6  // Timer 2 interrupt
#define IRQ_CTRLMEMCARD 7 // Controller and Memory Card interrupt
#define IRQ_SIO       8  // Serial I/O interrupt (SIO0/SIO1)
#define IRQ_SPU       9  // Sound Processing Unit interrupt
#define IRQ_PIO      10 // PIO (Controller?) interrupt (Lightpen?)


/* --- Interconnect Structure Definition ---
 * Holds pointers/instances of all main system components accessed via the bus.
 * Routes memory accesses from the CPU to the correct component.
 */
typedef struct Interconnect {
    Bios* bios; // Pointer to the loaded BIOS data
    Ram* ram;   // Pointer to the main RAM data buffer
    struct Cpu* cpu; // Pointer to CPU for triggering exceptions
    Gpu gpu;    // GPU state (embedded directly)
    Dma dma;    // DMA controller state (embedded directly)

    // --- Scratchpad (Fast RAM / D-Cache) ---
    uint8_t scratchpad[SCRATCHPAD_SIZE]; // 1KB fast RAM at 0x1f800000
    
    // --- Interrupt Controller State ---
    uint16_t irq_status;     // I_STAT Register state (reflects pending IRQs)
    uint16_t irq_mask;       // I_MASK Register state (enables/disables IRQs)
    uint16_t irq_line_state; // Current state of each IRQ line (for edge detection)
    // --------------------------------
    Timers timers_state; // <<< ADD THIS MEMBER
    Cdrom cdrom;
    Sio sio;  // Serial I/O (Controller and Memory Card)
    Spu spu;  // Sound Processing Unit state (minimal)
    Mdec mdec; // Macroblock Decoder

    // --- Event System State (for event_scheduler) ---
    // These fields are used by the central event/timing system to schedule and dispatch hardware events.
    #define EVQ_MAX_EVENTS  EVQ_EVENT_COUNT

    uint32_t evq_pending;                        // Bitfield: which events are currently pending
    uint32_t evq_target_cycle[EVQ_MAX_EVENTS];   // Target cycle for each event
    uint32_t evq_next_cycle;                     // Cycle of the next scheduled event
    uint32_t cpu_cycle_counter;                  // Global CPU cycle counter (updated by CPU/main loop)
    bool     frame_complete;                     // Set by the VBlank event; ends system_run_frame()

    /* Monotonic emulated time, for the log stamp (LogClock in log.h).
     * cpu_cycle_counter is 32-bit and wraps every ~127 s, so it cannot be the
     * axis on its own; VBlank folds each field's elapsed cycles into
     * emu_cycle_base, which is what makes a two-minute run comparable against
     * another emulator's timestamps. */
    uint64_t emu_cycle_base;                     // Cycles completed before the current field
    uint32_t field_cycle_mark;                   // cpu_cycle_counter at the current field's start
    uint32_t field_count;                        // CRTC fields (VBlanks) since reset
    // --------------------------------

    // --- BIOS TTY line buffer (EXP2 offset 0x23 capture) ---
    // Two sources feed this: the DUART byte port on EXP2, and the A0/B0 syscall
    // side-channel in cpu_bios.c. The BIOS routes its own printf through the
    // DUART, so once that port is live both sources carry the same stream and
    // every line was being printed twice. tty_duart_seen latches on the first
    // DUART byte and mutes the syscall side from then on; before it latches the
    // syscall side is the only thing that sees the early kernel banner.
    char tty_line_buf[256];
    int  tty_line_len;
    bool tty_duart_seen;

    // --- BIOS TTY input buffer (for kernel getc/putc TTY) ---
    char tty_input_buf[256];
    int  tty_input_read_idx;
    int  tty_input_write_idx;

    // --- Debugger ---
    Debugger debugger;

    // --- Memory Control 1 (0x1F801000-0x1F801020) — real register storage + derived timing ---
    // Timing model from DOCS/memorycontrol.md:136-145 (nocash spec): real games/BIOS
    // configure these delay registers, and un-cached/BIOS-ROM instruction fetches cost several
    // cycles each on real hardware, not the flat 1 cycle/instruction this project previously used.
    uint32_t memctrl_regs[9];    // exp1_base, exp2_base, exp1_delay, exp3_delay, bios_delay,
                                 // spu_delay, cdrom_delay, exp2_delay, common_delay
    uint32_t bios_access_cycles; // extra cycles (beyond the base 1/instruction) per BIOS ROM word fetch

    // Stall cycles owed by the data access(es) of the instruction currently
    // executing. The bus adds to it; cpu_run_next_instruction drains it into the
    // cycle counter and the downcount once the instruction retires, so an
    // instruction's memory cost lands in one place instead of being charged
    // half-way through its own execution. Always zero between instructions,
    // which is why it does not travel in a savestate.
    uint32_t cpu_mem_stall_cycles;

    // Instructions retired, for measuring cycles-per-instruction against the
    // cycle counter. The two were the same number until data accesses started
    // costing more than one cycle; their ratio is now the check that the memory
    // cost model produces a believable CPI rather than one tuned to a single loop.
    uint64_t instructions_retired;

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
void interconnect_init(Interconnect* inter, Bios* bios, Ram* ram);

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
 * @brief Trigger CDROM IRQ (IRQ2).
 * Called by CDROM when it needs to raise an interrupt.
 * @param inter Pointer to the Interconnect instance.
 */
void interconnect_trigger_cdrom_irq(Interconnect* inter);

/**
 * @brief Add a character to the TTY input buffer (for kernel getc)
 * @param inter Pointer to the Interconnect instance.
 * @param ch The character to add
 */
void interconnect_tty_input_add(Interconnect* inter, char ch);

/**
 * @brief Read a character from the TTY input buffer (for kernel getc)
 * @param inter Pointer to the Interconnect instance.
 * @return The next character from the buffer, or -1 if empty
 */
int interconnect_tty_input_get(Interconnect* inter);

/**
 * @brief Feed one captured BIOS TTY character, from whichever source saw it.
 * @param inter Pointer to the Interconnect instance.
 * @param ch Character; the line is flushed to the log on CR or LF.
 * @param from_duart True for the EXP2 byte port, false for the A0/B0 syscall
 *        side-channel. Once the DUART has been seen the syscall side is
 *        ignored, because both then carry the same stream.
 */
void interconnect_tty_char(Interconnect* inter, char ch, bool from_duart);

// Initialize HW dispatch tables (call once from interconnect_init).
void bus_hw_tables_init(void);

// Memory Control 1 (0x1F801000-0x1F801020) real register storage + derived access-time.
void bus_memctrl_init(Interconnect* inter);
void bus_memctrl_recalculate(Interconnect* inter);


#endif // INTERCONNECT_H