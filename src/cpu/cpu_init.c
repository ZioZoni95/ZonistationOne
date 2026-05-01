#include "cpu.h"
#include <string.h>
#include "log.h"
#include "gte.h"

// --- CPU Initialization ---
/**
 * @brief Initializes the CPU state to power-on defaults.
 */
void cpu_init(Cpu* cpu, Interconnect* inter) {
    LOG_CPU_INFO("CPU initialization started");
    LOG_SYSTEM_INFO("Initializing CPU...");

    cpu->pc = 0xbfc00000;         // Reset vector: Start of BIOS
    cpu->next_pc = cpu->pc + 4;   // Initial next PC
    cpu->current_pc = cpu->pc;    // Initial current PC (doesn't matter much before first cycle)
    cpu->inter = inter;           // Store pointer to interconnect

    // Initialize General Purpose Registers (GPRs) per PlayStation spec
    // R0 (zero) is always 0, others start at 0
    for (int i = 0; i < 32; ++i) {
        cpu->regs[i] = 0;
        cpu->out_regs[i] = 0;
    }
    cpu->regs[0] = 0;      // R0 (zero) always 0
    cpu->out_regs[0] = 0;  // R0 (zero) always 0

    // Initialize Load Delay Slot state
    cpu->load_reg_idx = REG_ZERO; // Target R0 initially (no-op)
    cpu->load_value = 0;

    // Initialize HI/LO registers (multiply/divide results)
    cpu->hi = 0; // HI register
    cpu->lo = 0; // LO register

    // Initialize Branch Delay Slot state
    cpu->branch_taken = false;    // Not initially in a branch
    cpu->in_delay_slot = false;   // Not initially in a delay slot

    // Initialize Coprocessor 0 Registers
    cpu->sr = (1 << 22);    // Status Register: BEV=1 (bootstrap exception vector)
    cpu->cause = 0;         // Cause Register (cleared)
    cpu->epc = 0;           // Exception PC (cleared)
    cpu->badvaddr = 0;      // Bad virtual address (COP0 r8)
    cpu->prid = 0x00000002; // Processor Revision Identifier: PSX value

    // Initialize boot stage tracking
    cpu->boot_stage = BOOT_STAGE_POWER_ON;

    LOG_CPU_DEBUG("Initializing I-Cache...");
    for (int i = 0; i < ICACHE_NUM_LINES; ++i) {
        cpu->icache[i].tag = 0xFFFFFFFF; // Initialize tag to an invalid pattern
        for (int j = 0; j < ICACHE_LINE_WORDS; ++j) {
            cpu->icache[i].valid[j] = false; // Mark all words in the line as invalid
            cpu->icache[i].data[j] = 0xDEADBEEF; // Optional: Initialize data to garbage
        }
    }

    // Initialize GTE
    LOG_CPU_DEBUG("Initializing GTE...");
    gte_init(&cpu->gte);

    // Initialize GTE Load Delay (Phase B5)
    cpu->gte_load_delay_reg = 255;      // No pending delay (255 = disabled)
    cpu->gte_load_delay_value = 0;
    cpu->gte_next_load_delay_reg = 255; // No next delay
    cpu->gte_next_load_delay_value = 0;

    // Initialize cycle accounting
    cpu->downcount = 1;              // fire dispatch immediately on first cycle
    cpu->muldiv_completion_tick = 0; // no pending MulDiv

    LOG_CPU_INFO("CPU initialized, PC=0x%08x", cpu->pc);
    // (Optional) Consider masking interrupts at startup until BIOS sets up its handler.
}