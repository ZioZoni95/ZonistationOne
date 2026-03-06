#ifndef CPU_CORE_H
#define CPU_CORE_H

#include "cpu_types.h"
#include "cpu_cache.h"
#include "interconnect.h"
#include "gte.h"

// ============================================================
// CPU State Structure
// ============================================================

typedef struct Cpu {
    // --- Core Registers ---
    uint32_t pc;
    uint32_t next_pc;
    uint32_t current_pc;
    uint32_t current_instruction;  // Current instruction being executed

    // --- General Purpose Registers ---
    Registers regs;      // Committed register file (all reads come from here)

    // --- Load Delay Slot (Dual slot system for back-to-back loads) ---
    Register load_reg_idx;
    uint32_t load_value;
    Register next_load_reg_idx;  // Next load delay slot
    uint32_t next_load_value;

    // --- Branch Delay Slot State ---
    bool branch_taken;
    bool in_delay_slot;
    bool exception_pending;

    // --- Timing System ---
    uint32_t downcount;              // Cycles until next event check
    uint32_t pending_ticks;          // Cycles accumulated this frame
    uint32_t muldiv_completion_tick; // When multiply/divide completes
    uint32_t gte_completion_tick;    // When GTE operation completes

    // --- Coprocessor 0 Registers ---
    Cop0Registers cop0;

    // --- Connection to Memory System ---
    Interconnect* inter;

    // --- Instruction Cache ---
    ICacheLine icache[ICACHE_NUM_LINES];

    // --- GTE Coprocessor ---
    Gte gte;

    // --- Interrupt Request State (DuckStation-style) ---
    bool interrupt_requested;    // Set by interrupt controller

    // --- Scratchpad (1KB Fast RAM at 0x1F800000) ---
    uint8_t scratchpad[1024]; // 1KB fast RAM, DuckStation-style

    // --- Boot Stage Tracking (all state in struct for thread safety) ---
    BootStage boot_stage;
    bool boot_jumped_to_ram;        // True once BIOS has jumped out of ROM
    bool boot_logo_started;         // True once logo animation has started
    bool boot_patch_loop_broken;    // True once BIOS patch verification was bypassed
    uint64_t boot_patch_region_time;// Cumulative instructions spent in patch region
    uint64_t boot_patch_first_entry;// Instruction count when patch region first entered
    bool boot_patch_data_written;   // True once simulated patch data was written
    bool boot_in_patch_region;      // True while PC is in patch verification region
    BootStage boot_last_stage;      // Previous boot stage (for transition logging)
    bool boot_stage_logged[BOOT_STAGE_GAME_RUNNING + 1]; // Which boot stages have been logged

    // --- Execution Diagnostics (DuckStation-style: all state in struct) ---
    uint64_t instruction_count;     // Total instructions executed
    uint32_t diag_last_pc;          // Last PC value (for stuck-loop detection)
    uint64_t diag_stuck_counter;    // How many consecutive instructions at same PC
    bool diag_warned_low_kuseg;     // One-shot warning for low KUSEG execution
    int  diag_last_bios_region;     // Last BIOS region detected (for region logging)
    int  diag_prev_bios_region;     // Previous BIOS region (for oscillation detection)
    int  diag_oscillation_count;    // Oscillation counter for region changes
    bool diag_inspected_0x80000080; // One-shot inspection at exception vector
    uint64_t diag_idle_counter;     // Idle instruction counter for WAITING_INPUT stage
    uint32_t diag_irq_trace_count;  // Rate-limit counter for IRQ trace logs
    uint64_t diag_irq_check_log_counter; // Rate-limit counter for periodic IRQ status
    bool diag_last_hw_irq_pending;  // Previous hw_irq_pending state (for change detection)
    uint32_t diag_hw_irq_change_count; // Count of hw_irq_pending state changes
    int  diag_bios_fetch_count;     // Count of instructions logged in BIOS patch region
    int  diag_boot_log_stage;       // Boot log stage tracker

} Cpu;

// ============================================================
// Core CPU Operations
// ============================================================

/**
 * @brief Initialize CPU state
 * @param cpu CPU instance to initialize
 * @param inter Interconnect pointer for memory access
 */
void cpu_init(Cpu* cpu, Interconnect* inter);

/**
 * @brief Execute next instruction (main emulation loop)
 * @param cpu CPU instance
 */
void cpu_run_next_instruction(Cpu* cpu);

// ============================================================
// Hot Path Inline Operations (Performance Critical)
// ============================================================

/**
 * @brief Read register value (optimized for hot path)
 * Always returns 0 for R0, inlined for performance.
 */
static inline uint32_t cpu_reg_get(const Cpu* cpu, Register index) {
    return cpu->regs.r[index];  // Register struct handles R0 as always 0
}

/**
 * @brief Write register value (optimized for hot path)
 * Writes directly to the committed register file.
 * Automatically handles R0 writes (ignored).
 */
static inline void cpu_reg_set_fast(Cpu* cpu, Register index, uint32_t value) {
    if (index != REG_ZERO) {
        cpu->regs.r[index] = value;
    }
    cpu->regs.r[REG_ZERO] = 0;  // Ensure R0 stays 0
}

/**
 * @brief Extract opcode from instruction (O(1) bit manipulation)
 */
static inline uint32_t cpu_opcode(uint32_t instruction) {
    return (instruction >> 26) & 0x3F;
}

/**
 * @brief Set CPU interrupt request state (DuckStation-style)
 * 
 * Called by interrupt controller when interrupt state changes.
 * CPU will check this during instruction execution.
 * 
 * @param cpu Pointer to CPU state
 * @param requested true if interrupt should be requested, false to clear
 */
void cpu_set_interrupt_request(Cpu* cpu, bool requested);

/**
 * @brief Dispatch interrupt check and raise exception if needed
 * @param cpu Pointer to CPU state
 */
void cpu_dispatch_interrupt(Cpu* cpu);

/**
 * @brief Checks if interrupts are enabled in the CPU status register
 * @param cpu Pointer to CPU state
 * @return true if interrupts are enabled (SR.IEC = 1)
 */
bool cpu_interrupts_enabled(const Cpu* cpu);

// ============================================================
// Register Access (Legacy API - calls inline versions)
// ============================================================

/**
 * @brief Initializes CPU to power-on state
 * @param cpu Pointer to CPU structure
 * @param inter Pointer to interconnect
 */
void cpu_init(Cpu* cpu, Interconnect* inter);

/**
 * @brief Executes one CPU instruction cycle
 * @param cpu Pointer to CPU state
 */
void cpu_run_next_instruction(Cpu* cpu);

/**
 * @brief Reads value from GPR
 * @param cpu Pointer to CPU state
 * @param index Register index
 * @return Register value
 */
uint32_t cpu_reg(Cpu* cpu, Register index);

/**
 * @brief Writes value to GPR
 * @param cpu Pointer to CPU state
 * @param index Register index
 * @param value Value to write
 */
void cpu_set_reg(Cpu* cpu, Register index, uint32_t value);

/**
 * @brief Schedule a register write for the load delay slot
 * Used by all load instructions (LB, LH, LW, LBU, LHU, LWL, LWR).
 * Handles double-load hazard: if a load to the same register is already pending,
 * the earlier value is discarded (DuckStation behavior).
 * @param cpu Pointer to CPU state
 * @param index Target register index
 * @param value Value to write after one instruction delay
 */
void cpu_set_reg_delayed(Cpu* cpu, Register index, uint32_t value);

/**
 * @brief Updates next_pc for branch instruction
 * @param cpu Pointer to CPU state
 * @param offset_se Sign-extended branch offset
 */
void cpu_branch(Cpu* cpu, uint32_t offset_se);

// ============================================================
// Execution Tracing & Logging
// ============================================================

/**
 * @brief Global flag to enable/disable execution tracing
 */
extern bool cpu_trace_execution;

/**
 * @brief Start execution tracing
 */
void cpu_start_trace(void);

/**
 * @brief Stop execution tracing
 */
void cpu_stop_trace(void);

/**
 * @brief Check if execution tracing is enabled
 * @return true if tracing is enabled
 */
bool cpu_is_trace_enabled(void);

/**
 * @brief Write a message to the execution log
 * @param format printf-style format string
 * @param ... format arguments
 */
void cpu_write_to_execution_log(const char* format, ...);

/**
 * @brief Print detailed instruction trace
 * @param cpu Pointer to CPU state
 * @param instruction The instruction being executed
 * @param pc Program counter
 */
void cpu_trace_print_instruction(const Cpu* cpu, uint32_t instruction, uint32_t pc);

/**
 * @brief Log an instruction with register state
 * @param cpu Pointer to CPU state
 * @param instruction The instruction being executed
 * @param pc Program counter
 * @param regs true to include register dump
 */
void cpu_log_instruction(const Cpu* cpu, uint32_t instruction, uint32_t pc, bool regs);

/**
 * @brief Force completion of any pending load delays (for exceptions/branches)
 * Based on DuckStation's FlushLoadDelay() - ensures load delays complete immediately
 * @param cpu CPU instance
 */
void cpu_flush_load_delay(Cpu* cpu);

/**
 * @brief Reset pipeline state after exceptions or branches
 * Based on DuckStation's FlushPipeline() - clears load delays and branch state
 * @param cpu CPU instance
 */
void cpu_flush_pipeline(Cpu* cpu);

#endif // CPU_CORE_H
