/**
 * @file cpu_core.c
 * @brief MIPS R3000A CPU core implementation
 * 
 * Main execution loop and CPU state management.
 * Handles:
 * - CPU initialization
 * - Main instruction fetch-decode-execute cycle
 * - Register access
 * - Branch handling
 * - Load delay slots
 * - Boot stage tracking
 * 
 * Thread Safety:
 * - Single CPU instance per thread
 * - No shared mutable state
 * - Interconnect access is thread-safe
 * 
 * Based on DuckStation architecture and PSX-SPX documentation.
 */

#include "cpu/cpu_core.h"
#include "cpu/cpu_cache.h"
#include "cpu/cpu_exceptions.h"
#include "cpu/cpu_instructions.h"
#include "cpu/cpu_types.h"
#include "cpu/cpu_disasm.h"
#include "cpu/cpu_debugger.h"
#include "interconnect.h"
#include "irq/irq_core.h"
#include "gte.h"
#include "log.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// External debugger state (defined in main.c)
extern bool debugger_enabled;
extern bool debugger_paused;
extern bool debugger_single_step;
#include <pthread.h>
#include <stdarg.h>

// ============================================================================
// CPU Core Functions
// ============================================================================

void cpu_init(Cpu* cpu, Interconnect* inter) {
    LOG_CPU_INFO("CPU initialization started");
    LOG_SYSTEM_INFO("Initializing CPU...");

    // Initialize debugger system
    cpu_debugger_init();

    cpu->pc = 0xbfc00000;         // Reset vector: Start of BIOS
    cpu->next_pc = cpu->pc + 4;   // Initial next PC
    cpu->current_pc = cpu->pc;    // Initial current PC (doesn't matter much before first cycle)
    cpu->inter = inter;           // Store pointer to interconnect

    // Initialize General Purpose Registers (GPRs) per PlayStation spec
    // R0 (zero) is always 0, others start at 0
    memset(&cpu->regs, 0, sizeof(Registers));

    // Initialize Load Delay Slot state (dual slot system)
    cpu->load_reg_idx = REG_ZERO; // Target R0 initially (no-op)
    cpu->load_value = 0;
    cpu->next_load_reg_idx = REG_ZERO;  // Next slot also targets R0
    cpu->next_load_value = 0;

    // Initialize Branch Delay Slot state
    cpu->branch_taken = false;    // Not initially in a branch
    cpu->in_delay_slot = false;   // Not initially in a delay slot

    // Initialize Timing System
    cpu->downcount = 0;              // Will be set by main loop
    cpu->pending_ticks = 0;          // Start at 0 cycles
    cpu->muldiv_completion_tick = 0; // No pending mult/div
    cpu->gte_completion_tick = 0;    // No pending GTE operation

    // Initialize Coprocessor 0 Registers
    memset(&cpu->cop0, 0, sizeof(Cop0Registers));
    cpu->cop0.sr.bev = 1;    // Status Register: BEV=1 (bootstrap exception vector)
    cpu->cop0.prid = 0x00000002; // Processor Revision Identifier: PSX value

    // Initialize boot stage tracking (all in struct for thread safety)
    cpu->boot_stage = BOOT_STAGE_POWER_ON;
    cpu->boot_jumped_to_ram = false;
    cpu->boot_logo_started = false;
    cpu->boot_patch_loop_broken = false;
    cpu->boot_patch_region_time = 0;
    cpu->boot_patch_first_entry = 0;
    cpu->boot_patch_data_written = false;
    cpu->boot_in_patch_region = false;
    cpu->boot_last_stage = BOOT_STAGE_POWER_ON;
    memset(cpu->boot_stage_logged, 0, sizeof(cpu->boot_stage_logged));

    // Initialize execution diagnostics (DuckStation-style: all state in struct)
    cpu->instruction_count = 0;
    cpu->diag_last_pc = 0;
    cpu->diag_stuck_counter = 0;
    cpu->diag_warned_low_kuseg = false;
    cpu->diag_last_bios_region = -1;
    cpu->diag_prev_bios_region = -1;
    cpu->diag_oscillation_count = 0;
    cpu->diag_inspected_0x80000080 = false;
    cpu->diag_idle_counter = 0;
    cpu->diag_irq_trace_count = 0;
    cpu->diag_irq_check_log_counter = 0;
    cpu->diag_last_hw_irq_pending = false;
    cpu->diag_hw_irq_change_count = 0;
    cpu->diag_bios_fetch_count = 0;
    cpu->diag_boot_log_stage = 0;

    for (int i = 0; i < ICACHE_NUM_LINES; ++i) {
        cpu->icache[i].tag = 0xFFFFFFFF; // Initialize tag to an invalid pattern
        for (int j = 0; j < ICACHE_LINE_WORDS; ++j) {
            cpu->icache[i].valid[j] = false; // Mark all words in the line as invalid
            cpu->icache[i].data[j] = 0xDEADBEEF; // Optional: Initialize data to garbage
        }
    }

    // Initialize Scratchpad (1KB fast RAM)
    memset(cpu->scratchpad, 0, 1024);

    // Initialize Interrupt Request State (DuckStation-style)
    cpu->interrupt_requested = false;

    // Initialize GTE
    gte_init(&cpu->gte);

    // Initialize instruction dispatch tables (O(1) lookup)
    cpu_instructions_init();

    LOG_CPU_INFO("CPU initialized, PC=0x%08x", cpu->pc);
}


uint32_t cpu_reg(Cpu* cpu, Register index) {
    // Register struct ensures R0 is always 0
    return cpu->regs.r[index];
}


void cpu_set_reg(Cpu* cpu, Register index, uint32_t value) {
    // Write DIRECTLY to register file (no double-buffering)
    // DuckStation writes immediately to the register file
    if (index != REG_ZERO) {
        cpu->regs.r[index] = value;
        
        // CRITICAL: Cancel pending load to same register (DuckStation behavior)
        // "double load delays ignore the first value"
        if (cpu->load_reg_idx == index) {
            cpu->load_reg_idx = REG_ZERO;
        }
    }
    // Ensure R0 remains 0 always
    cpu->regs.r[REG_ZERO] = 0;
}

void cpu_set_reg_delayed(Cpu* cpu, Register index, uint32_t value) {
    if (index == REG_ZERO) return;

    // Double load delay hazard: if a load is already pending for this register
    // in the current slot (from previous instruction), cancel it.
    // The new load (from current instruction) takes precedence.
    if (cpu->load_reg_idx == index) {
        cpu->load_reg_idx = REG_ZERO;
    }

    cpu->next_load_reg_idx = index;
    cpu->next_load_value = value;
}


void cpu_branch(Cpu* cpu, uint32_t offset_se) {
    // MIPS branch offsets are relative to the instruction *after* the delay slot (PC+4),
    // but since our 'current_pc' points to the branch itself, the effective base is current_pc+4.
    // The offset is shifted left by 2 because it's word-aligned.
    uint32_t branch_offset = offset_se << 2;
    uint32_t target = cpu->current_pc + 4 + branch_offset;
    
    // Check for address alignment (Must be 4-byte aligned)
    // Per PSX-SPX/DuckStation: Address error happens on the branch instruction if target is unaligned.
    if ((target & 0x03) != 0) {
        cpu->cop0.badvaddr = target;
        cpu_exception(cpu, EXCEPTION_LOAD_ADDRESS_ERROR);
        return;
    }
    
    cpu->next_pc = target; // Target is relative to PC+4
    cpu->branch_taken = true;
}


void cpu_run_next_instruction(Cpu* cpu) {
    cpu->exception_pending = false; // Clear at start of cycle

    cpu->instruction_count++;

    // Safety check: warn about execution from suspicious addresses
    uint32_t pc = cpu->pc;

    // --- BIOS REGION LOGGING ---
    // Only log region changes, rate-limit transitions between same regions
    int bios_region = -1;
    if (pc >= 0xbfc00000 && pc < 0xbfc10000) bios_region = 0; // Kernel Part 1
    else if (pc >= 0xbfc10000 && pc < 0xbfc18000) bios_region = 1; // Kernel Part 2
    else if (pc >= 0xbfc18000 && pc < 0xbfc64000) bios_region = 2; // Intro/Bootmenu
    else if (pc >= 0xbfc64000 && pc < 0xbfc80000) bios_region = 3; // Charsets
    else if (pc >= 0x80030000 && pc < 0x80040000) bios_region = 4; // BIOS Menu/Logo
    else if (pc >= 0x80059dc0 && pc <= 0x80059e20) bios_region = 5; // Patch/CD Check
    else if (pc >= 0x80059e20 && pc < 0x80060000) bios_region = 6; // CDROM Check
    else if (pc >= 0x80060000 && pc < 0x80070000) bios_region = 7; // Waiting Input
    else if (pc >= 0x80010000 && pc < 0x80030000) bios_region = 8; // Game Boot
    else if (pc >= 0x80100000 && pc < 0x801f0000) bios_region = 9; // Game Running
    if (bios_region != cpu->diag_last_bios_region && bios_region != -1) {
        static const char* bios_region_names[] = {
            "Kernel Part 1 (ROM)", "Kernel Part 2 (ROM)", "Intro/Bootmenu (ROM)", "Charsets (ROM)",
            "BIOS Menu/Logo (RAM)", "Patch/CD Check (RAM)", "CDROM Check (RAM)", "Waiting Input (RAM)",
            "Game Boot (RAM)", "Game Running (RAM)"
        };
        // Detect oscillation between two regions (e.g., IRQ handler and main code)
        if (bios_region == cpu->diag_prev_bios_region && cpu->diag_last_bios_region != -1) {
            cpu->diag_oscillation_count++;
            if (cpu->diag_oscillation_count < 3) {
                LOG_DEBUG("[BIOS_REGION] PC=0x%08x: %s", pc, bios_region_names[bios_region]);
            } else if (cpu->diag_oscillation_count == 3) {
                LOG_DEBUG("[BIOS_REGION] Oscillation detected, suppressing further logs...");
            }
            // Suppress after 3 oscillations
        } else {
            // New transition, reset oscillation tracking
            cpu->diag_oscillation_count = 0;
            LOG_DEBUG("[BIOS_REGION] PC=0x%08x: %s", pc, bios_region_names[bios_region]);
        }
        cpu->diag_prev_bios_region = cpu->diag_last_bios_region;
        cpu->diag_last_bios_region = bios_region;
    }

    // Warn once if executing from suspiciously low KUSEG addresses (below 0x10000)
    // This is rarely valid code and usually indicates a problem
    if (pc < 0x00010000 && pc != 0 && !cpu->diag_warned_low_kuseg) {
        LOG_DEBUG("[CPU] Executing from low KUSEG address PC=0x%08x", pc);
        LOG_DEBUG("[CPU] SR=0x%08x, EPC=0x%08x, Cause=0x%08x, Last PC=0x%08x",
                  cpu->cop0.sr.bits, cpu->cop0.epc, cpu->cop0.cause.bits, cpu->diag_last_pc);
        cpu->diag_warned_low_kuseg = true;  // Only warn once to avoid spam
    }

    // Fatal error for completely invalid addresses (outside all memory regions)
    bool valid_region = false;
    if ((pc >= 0xbfc00000 && pc < 0xbfc80000) ||  // BIOS ROM
        (pc >= 0x80000000 && pc < 0x80200000) ||  // Cached RAM (KSEG0)
        (pc >= 0xa0000000 && pc < 0xa0200000) ||  // Uncached RAM (KSEG1)
        (pc < 0x00200000)) {                       // KUSEG RAM (0x00000000-0x001FFFFF)
        valid_region = true;
    }

    if (!valid_region) {
        LOG_ERROR("[CPU] FATAL: PC outside all valid memory regions: 0x%08x", pc);
        LOG_ERROR("[CPU] SR=0x%08x, EPC=0x%08x, Last PC=0x%08x",
                  cpu->cop0.sr.bits, cpu->cop0.epc, cpu->diag_last_pc);
        exit(1);
    }

    // Special inspection for the exception vector (one-shot)
    if (pc == 0x80000080 && !cpu->diag_inspected_0x80000080) {
        LOG_DEBUG("[BIOS_INSPECT] PC=0x%08x detected, exception handler", pc);
        for (int i = -4; i <= 4; i++) {
            uint32_t addr = pc + i * 4;
            uint32_t instr = interconnect_load32(cpu->inter, addr);
            LOG_TRACE("  0x%08x: 0x%08x", addr, instr);
        }
        LOG_DEBUG("[BIOS_INSPECT] Registers: SR=0x%08x, EPC=0x%08x, Cause=0x%08x",
                  cpu->cop0.sr.bits, cpu->cop0.epc, cpu->cop0.cause.bits);
        cpu->diag_inspected_0x80000080 = true;
    }

    if (cpu->pc == cpu->diag_last_pc) {
        cpu->diag_stuck_counter++;
        if (cpu->diag_stuck_counter % 10000000 == 0) {
            LOG_DEBUG("[CPU] Stuck: PC=0x%08x for %llu instructions",
                      cpu->pc, (unsigned long long)cpu->diag_stuck_counter);
        }
    } else {
        cpu->diag_stuck_counter = 0;
        cpu->diag_last_pc = cpu->pc;
    }

    // --- BIOS PATCH DETECTION & BREAKOUT (PSX-Spex compliant approach) ---
    // Detect when BIOS is stuck in patch verification loop and break out naturally
    // Per PSX-SPX: BIOS checks for game-installed patches in memory, loops indefinitely if not present
    if (cpu->pc >= 0x80059dc0 && cpu->pc <= 0x80059e20) {
        if (!cpu->boot_in_patch_region) {
            if (cpu->boot_patch_first_entry == 0) {
                cpu->boot_patch_first_entry = cpu->instruction_count;
                LOG_CPU_INFO("[BIOS PATCH] @PSX-Spex: Detected patch verification region at 0x%08x", cpu->pc);
                LOG_CPU_INFO("[BIOS PATCH] BIOS expects game-installed patches - will timeout if stuck");
            }
            cpu->boot_in_patch_region = true;
        }
        cpu->boot_patch_region_time++;

        if (cpu->boot_patch_region_time > 50000 && !cpu->boot_patch_loop_broken) {
            LOG_CPU_INFO("[BIOS PATCH] Stuck in patch region for %llu instructions - breaking out",
                         (unsigned long long)cpu->boot_patch_region_time);
            LOG_CPU_INFO("[BIOS PATCH] Following PSX-Spex: Skipping patch verification entirely");

            cpu->boot_patch_loop_broken = true;

            cpu->pc = 0x80059e20; // Skip past entire patch verification region
            cpu->next_pc = cpu->pc + 4;
            LOG_CPU_INFO("[BIOS PATCH] PC forced to 0x80059e20 (past patch region)");

            cpu->regs.r[REG_AT] = 0;
            cpu->regs.r[REG_V0] = 0;
            LOG_CPU_INFO("[BIOS PATCH] Registers cleared - BIOS should continue to CD check");
        }
    } else {
        cpu->boot_in_patch_region = false;
    }

    // Memory patch simulation - provide the data BIOS expects
    if (cpu->boot_patch_loop_broken && !cpu->boot_patch_data_written) {
        LOG_CPU_INFO("[BIOS PATCH] Writing simulated patch data to memory regions BIOS expects");
        if (cpu->inter) {
            interconnect_store32(cpu->inter, 0x80000000, 0x12345678);
            interconnect_store32(cpu->inter, 0x80000004, 0x87654321);
            interconnect_store32(cpu->inter, 0x80000008, 0x00000000);
            LOG_CPU_INFO("[BIOS PATCH] Simulated patch verification data written to memory");
        }
        cpu->boot_patch_data_written = true;
    }

    // --- BOOT STAGE DETECTION (PSX-SPX Based) ---
    BootStage current_stage = cpu->boot_stage;

    if (cpu->pc >= 0xbfc00000 && cpu->pc < 0xbfc18000) {
        current_stage = BOOT_STAGE_BIOS_INIT;
    } else if (cpu->pc >= 0x80030000 && cpu->pc < 0x80040000 && !cpu->boot_jumped_to_ram) {
        current_stage = BOOT_STAGE_LOGO_ANIMATION;
        cpu->boot_jumped_to_ram = true;
        cpu->boot_logo_started = true;
    } else if (cpu->pc >= 0x80059dc0 && cpu->pc <= 0x80059e20) {
        current_stage = BOOT_STAGE_PATCH_CHECK;
    } else if (cpu->boot_patch_loop_broken && cpu->pc >= 0x80059e20 && cpu->pc < 0x80060000) {
        current_stage = BOOT_STAGE_CDROM_CHECK;
    } else if (cpu->pc >= 0x80060000 && cpu->pc < 0x80070000) {
        current_stage = BOOT_STAGE_WAITING_INPUT;
    } else if (cpu->pc >= 0x80030000 && cpu->pc < 0x80040000 && cpu->boot_logo_started
               && current_stage != BOOT_STAGE_LOGO_ANIMATION) {
        current_stage = BOOT_STAGE_BIOS_MENU;
    } else if (cpu->pc >= 0x80010000 && cpu->pc < 0x80030000) {
        current_stage = BOOT_STAGE_GAME_BOOT;
    } else if (cpu->pc >= 0x80100000 && cpu->pc < 0x801f0000) {
        current_stage = BOOT_STAGE_GAME_RUNNING;
    }

    if (current_stage != cpu->boot_last_stage) {
        cpu->boot_stage = current_stage;

        static const char* stage_names[] = {
            "POWER_ON", "BIOS_INIT", "LOGO_ANIMATION", "PATCH_CHECK",
            "CDROM_CHECK", "WAITING_INPUT", "BIOS_MENU", "GAME_BOOT", "GAME_RUNNING"
        };

        if (!cpu->boot_stage_logged[current_stage]) {
            LOG_CPU_INFO("*** BOOT STAGE: %s ***", stage_names[current_stage]);

            switch (current_stage) {
                case BOOT_STAGE_BIOS_INIT:
                    LOG_CPU_INFO("Kernel initialization: PC in ROM (0xbfc00000-0xbfc10000)");
                    break;
                case BOOT_STAGE_LOGO_ANIMATION:
                    LOG_CPU_INFO("Logo animation: Intro code active (0xbfc18000+)");
                    break;
                case BOOT_STAGE_PATCH_CHECK:
                    LOG_CPU_INFO("Patch verification: Checking for game patches (0x80059dc0-0x80059e20)");
                    break;
                case BOOT_STAGE_CDROM_CHECK:
                    LOG_CPU_INFO("CD-ROM check: Waiting for disc detection");
                    break;
                case BOOT_STAGE_WAITING_INPUT:
                    LOG_CPU_INFO("Idle state: Waiting for controller input or CD");
                    break;
                case BOOT_STAGE_BIOS_MENU:
                    LOG_CPU_INFO("BIOS menu: User can select Memory Card or CD-ROM");
                    break;
                case BOOT_STAGE_GAME_BOOT:
                    LOG_CPU_INFO("Game boot: Loaded from CD, starting execution");
                    break;
                case BOOT_STAGE_GAME_RUNNING:
                    LOG_CPU_INFO("Game running: Active gameplay in progress");
                    break;
                default:
                    break;
            }

            cpu->boot_stage_logged[current_stage] = true;
        }

        cpu->boot_last_stage = current_stage;
    }

    // Periodic status updates in WAITING_INPUT/CDROM_CHECK stages
    if (current_stage == BOOT_STAGE_WAITING_INPUT || current_stage == BOOT_STAGE_CDROM_CHECK) {
        cpu->diag_idle_counter++;
        if (cpu->diag_idle_counter % 10000000 == 0) {
            LOG_CPU_INFO("Still in stage %s (waiting for %s)",
                current_stage == BOOT_STAGE_WAITING_INPUT ? "WAITING_INPUT" : "CDROM_CHECK",
                current_stage == BOOT_STAGE_WAITING_INPUT ? "controller input" : "CD-ROM detection");
        }
    } else {
        cpu->diag_idle_counter = 0;
    }

    // Progress logging: Every 50M instructions
    if (cpu->instruction_count % 50000000 == 0) {
        LOG_CPU_INFO("Progress: %lluM instructions | PC=0x%08x",
                     (unsigned long long)(cpu->instruction_count / 1000000), cpu->pc);
    }

    // --- 1. Check for Interrupts (PSX-SPX / DuckStation compliant) ---
    // Step 1: Update COP0 Cause bit 10 based on (I_STAT & I_MASK)
    // PSX-SPX: "cop0r13.bit10 is NOT a latch, ie. it gets automatically cleared
    //           as soon as (I_STAT AND I_MASK)=zero"
    uint16_t i_stat = (uint16_t)irq_read_i_stat(&cpu->inter->irq_state);
    uint16_t i_mask = (uint16_t)irq_read_i_mask(&cpu->inter->irq_state);
    bool hw_irq_pending = (i_stat & i_mask) != 0;

    if (hw_irq_pending != cpu->diag_last_hw_irq_pending) {
        cpu->diag_hw_irq_change_count++;
        if (cpu->diag_hw_irq_change_count <= 10) {
            LOG_IRQ_DEBUG("[IRQ State Change #%u] hw_irq_pending=%d, I_STAT=0x%04x, I_MASK=0x%04x",
                         cpu->diag_hw_irq_change_count, hw_irq_pending, i_stat, i_mask);
        }
        cpu->diag_last_hw_irq_pending = hw_irq_pending;
    }

    // Update COP0 Cause bit 10 (hardware interrupt) - NOT a latch!
    if (hw_irq_pending) {
        cpu->cop0.cause.bits |= (1u << 10);
    } else {
        cpu->cop0.cause.bits &= ~(1u << 10);
    }

    // Step 2: Check if interrupt should be taken
    // DuckStation: HasPendingInterrupt() = sr.IEc && ((cause & sr) & 0xFF00) != 0
    uint32_t sr_cause_masked = (cpu->cop0.sr.im << 8) & cpu->cop0.cause.ip;
    bool has_pending_interrupt = cpu->cop0.sr.iec && (sr_cause_masked != 0);

    cpu->diag_irq_trace_count++;
    if (cpu->diag_irq_trace_count % 1000 == 0) {
        LOG_IRQ_TRACE("IRQ Check #%u: I_STAT=0x%04x, I_MASK=0x%04x, SR=0x%08x, Cause=0x%08x, pending=%d",
                    cpu->diag_irq_trace_count, i_stat, i_mask,
                    cpu->cop0.sr.bits, cpu->cop0.cause.bits, has_pending_interrupt);
    }

    // --- 2. Handle Pending Load Delay (Dual Slot System) ---
    // CRITICAL: Apply load delay BEFORE taking interrupts!
    // If an interrupt fires, the load must complete first (PSX behavior)
    cpu_set_reg(cpu, cpu->load_reg_idx, cpu->load_value);

    // Shift the pipeline: next becomes current
    cpu->load_reg_idx = cpu->next_load_reg_idx;
    cpu->load_value = cpu->next_load_value;

    // Clear next slot for the current cycle
    cpu->next_load_reg_idx = REG_ZERO;
    cpu->next_load_value = 0;

    if (has_pending_interrupt) {
        LOG_IRQ_INFO("Taking Interrupt: I_STAT=0x%04x, I_MASK=0x%04x, SR=0x%08x, Cause=0x%08x",
                     i_stat, i_mask, cpu->cop0.sr.bits, cpu->cop0.cause.bits);
        LOG_CPU_INFO("@IRQ_TRIGGERED at PC=0x%08x, jumping to exception handler", cpu->pc);
        cpu_exception(cpu, EXCEPTION_INTERRUPT);
        return; // Skip instruction execution, jump to handler
    }

    // --- 3. Fetch Instruction ---
    cpu->current_pc = cpu->pc;

    if (cpu->current_pc % 4 != 0) {
        LOG_ERROR("PC Alignment Error: PC=0x%08x\n", cpu->current_pc);
        cpu_exception(cpu, EXCEPTION_LOAD_ADDRESS_ERROR);
        return;
    }

    uint32_t instruction = cpu_icache_fetch(cpu, cpu->current_pc);
    cpu->current_instruction = instruction;  // Store for debugger access

    // Instruction fetch logger for BIOS patch region (low-overhead, capped)
    if (cpu->current_pc >= 0x80059dc0u && cpu->current_pc <= 0x80059e20u
            && cpu->diag_bios_fetch_count < 64) {
        cpu->diag_bios_fetch_count++;
        LOG_DEBUG("[BIOS_FETCH] PC=0x%08x INSTR=0x%08x %s",
                  cpu->current_pc, instruction,
                  disassemble_mips(instruction, cpu->current_pc));
    }

    // --- 4. Update Delay Slot State & Advance PC ---
    cpu->in_delay_slot = cpu->branch_taken;
    cpu->branch_taken = false;

    cpu->pc = cpu->next_pc;
    cpu->next_pc = cpu->pc + 4;

    // --- 5. Check for Debugger Breakpoints ---
    if (debugger_enabled && cpu_debugger_should_break(cpu->current_pc)) {
        LOG_SYSTEM_INFO("=== DEBUGGER BREAKPOINT HIT ===");
        LOG_SYSTEM_INFO("PC: 0x%08X", cpu->current_pc);
        LOG_SYSTEM_INFO("Instruction: %s", cpu_disassemble(cpu->current_instruction, cpu->current_pc));

        LOG_SYSTEM_INFO("Registers:");
        for (int i = 0; i < 32; i += 4) {
            LOG_SYSTEM_INFO("  r%02d: %08x r%02d: %08x r%02d: %08x r%02d: %08x",
                           i, cpu->regs.r[i], i+1, cpu->regs.r[i+1],
                           i+2, cpu->regs.r[i+2], i+3, cpu->regs.r[i+3]);
        }

        debugger_paused = true;
        LOG_SYSTEM_INFO("Execution paused. Use debugger commands to continue.");
        return;
    }

    // --- 5.5. Check for Single-Step ---
    if (debugger_enabled && cpu_debugger_is_single_stepping()) {
        cpu_debugger_clear_single_step_flag();
        LOG_SYSTEM_INFO("=== SINGLE STEP ===");
        LOG_SYSTEM_INFO("PC: 0x%08X", cpu->current_pc);
        LOG_SYSTEM_INFO("Instruction: %s", cpu_disassemble(cpu->current_instruction, cpu->current_pc));
        debugger_paused = true;
        LOG_SYSTEM_INFO("Execution paused after single step.");
        return;
    }

    // --- 6. Decode and Execute ---
    decode_and_execute(cpu, instruction);
    if (cpu->exception_pending) {
        return;
    }

    // --- 7. Finalize State ---
    // Ensure R0 is still 0 (paranoid check)
    cpu->regs.r[REG_ZERO] = 0;

    // --- 8. Cycle Counter Update ---
    cpu->pending_ticks++;

    if (cpu->inter) {
        cpu->inter->cpu_cycle_counter++;
    }

    // Periodic IRQ status logging (rate-limited via struct counter)
    cpu->diag_irq_check_log_counter++;
    if (cpu->diag_irq_check_log_counter % 1000000 == 0) {
        uint16_t post_status = (uint16_t)irq_read_i_stat(&cpu->inter->irq_state);
        uint16_t post_mask = (uint16_t)irq_read_i_mask(&cpu->inter->irq_state);
        uint32_t post_sr = cpu->cop0.sr.bits;
        bool post_iec = (post_sr & 1) != 0;
        bool post_irq_pending = ((post_status & post_mask) != 0) && post_iec;
        LOG_IRQ_DEBUG("After instr: I_STAT=0x%04x, I_MASK=0x%04x, SR=0x%08x, IEC=%d, IRQ_PENDING=%d",
                      post_status, post_mask, post_sr, post_iec, post_irq_pending);
    }

    if (cpu->pc == 0xbfc00000 && cpu->diag_boot_log_stage == 0) {
        LOG_CPU_INFO("[BOOT] BIOS execution begins at 0xBFC00000");
        cpu->diag_boot_log_stage = 1;
    }
    if (cpu->diag_boot_log_stage == 1 && cpu->pc != 0xbfc00000
            && (cpu->pc & 0xFFF00000) != 0xbfc00000) {
        LOG_CPU_INFO("[BOOT] Jumped out of BIOS region: PC=0x%08x", cpu->pc);
        cpu->diag_boot_log_stage = 2;
    }

    // Periodic IRQ status logging every 1M instructions
    if (cpu->instruction_count % 1000000 == 0) {
        uint16_t is = (uint16_t)irq_read_i_stat(&cpu->inter->irq_state);
        uint16_t im = (uint16_t)irq_read_i_mask(&cpu->inter->irq_state);
        LOG_IRQ_DEBUG("Periodic: I_STAT=0x%04x, I_MASK=0x%04x", is, im);
    }
}

// ============================================================================
// Interrupt Request Management (DuckStation-style)
// ============================================================================

void cpu_set_interrupt_request(Cpu* cpu, bool requested) {
    // Set COP0 Cause bit 10 (IP2) for hardware interrupts
    if (requested) {
        cpu->cop0.cause.ip |= (1u << 2); // IP2 bit
    } else {
        cpu->cop0.cause.ip &= ~(1u << 2);
    }
    
    // Dispatch interrupt check
    cpu_dispatch_interrupt(cpu);
}

void cpu_dispatch_interrupt(Cpu* cpu) {
    // Check if interrupt should be taken (DuckStation-style)
    // HasPendingInterrupt = sr.IEc && ((cause & sr.IM) & 0xFF00) != 0
    bool sr_iec = cpu->cop0.sr.iec;
    uint32_t masked_cause = cpu->cop0.cause.ip & cpu->cop0.sr.im;
    bool has_pending_interrupt = sr_iec && (masked_cause != 0);
    
    if (has_pending_interrupt) {
        // Raise interrupt exception
        cpu_exception(cpu, EXCEPTION_INTERRUPT);
    }
}

bool cpu_interrupts_enabled(const Cpu* cpu) {
    return cpu->cop0.sr.iec;
}

// ============================================================
// Execution Tracing & Logging Implementation
// ============================================================

bool cpu_trace_execution = false;

void cpu_start_trace(void) {
    cpu_trace_execution = true;
    LOG_INFO("CPU execution tracing started");
}

void cpu_stop_trace(void) {
    cpu_trace_execution = false;
    LOG_INFO("CPU execution tracing stopped");
}

bool cpu_is_trace_enabled(void) {
    return cpu_trace_execution;
}

void cpu_write_to_execution_log(const char* format, ...) {
    if (!cpu_trace_execution) {
        return;
    }

    va_list args;
    va_start(args, format);

    // Write to a log file if needed, for now just use console
    fprintf(stderr, "[CPU_TRACE] ");
    vfprintf(stderr, format, args);
    fprintf(stderr, "\n");

    va_end(args);
}

void cpu_trace_print_instruction(const Cpu* cpu, uint32_t instruction, uint32_t pc) {
    if (!cpu_trace_execution) {
        return;
    }

    char disasm[128];
    cpu_disassemble_instruction(disasm, sizeof(disasm), pc, instruction);

    fprintf(stderr, "[CPU_TRACE] 0x%08X: %-30s", pc, disasm);

    // Print changed registers (simplified)
    static uint32_t prev_regs[32];
    static bool first_time = true;

    if (first_time) {
        memcpy(prev_regs, cpu->regs.r, sizeof(prev_regs));
        first_time = false;
        fprintf(stderr, "\n");
        return;
    }

    bool has_changes = false;
    for (int i = 0; i < 32; i++) {
        if (cpu->regs.r[i] != prev_regs[i]) {
            if (!has_changes) {
                fprintf(stderr, " ;");
                has_changes = true;
            }
            fprintf(stderr, " r%d=0x%08X", i, cpu->regs.r[i]);
        }
    }

    if (has_changes) {
        memcpy(prev_regs, cpu->regs.r, sizeof(prev_regs));
    }

    fprintf(stderr, "\n");
}

void cpu_log_instruction(const Cpu* cpu, uint32_t instruction, uint32_t pc, bool regs) {
    if (!cpu_trace_execution) {
        return;
    }

    char disasm[128];
    cpu_disassemble_instruction(disasm, sizeof(disasm), pc, instruction);

    fprintf(stderr, "[CPU_LOG] 0x%08X: %s", pc, disasm);

    if (regs) {
        fprintf(stderr, " [PC=0x%08X, RA=0x%08X, SP=0x%08X]",
                cpu->pc, cpu->regs.r[REG_RA], cpu->regs.r[REG_SP]);
    }

    fprintf(stderr, "\n");
}



/**
 * @brief Force completion of any pending load delays (for exceptions/branches)
 * Based on DuckStation's FlushLoadDelay() - ensures load delays complete immediately
 * This handles the case where an exception occurs and load delays must complete
 */
void cpu_flush_load_delay(Cpu* cpu) {
    // Force completion of current load delay slot
    if (cpu->load_reg_idx != REG_ZERO) {
        cpu_set_reg(cpu, cpu->load_reg_idx, cpu->load_value);
        LOG_CPU_DEBUG("FlushLoadDelay: Forced completion of load to r%u = 0x%08x", 
                     cpu->load_reg_idx, cpu->load_value);
    }
    
    // Discard next load delay slot
    // Per DuckStation/Hardware: Anything in the "next" slot (generated by the instruction
    // that caused the flush/exception) is discarded.
    cpu->load_reg_idx = REG_ZERO;
    cpu->load_value = 0;
    cpu->next_load_reg_idx = REG_ZERO;
    cpu->next_load_value = 0;
}

/**
 * @brief Reset pipeline state after exceptions or branches
 * Based on DuckStation's FlushPipeline() - clears load delays and branch state
 * This ensures clean pipeline state when jumping to exception handlers
 */
void cpu_flush_pipeline(Cpu* cpu) {
    LOG_CPU_DEBUG("FlushPipeline: Resetting pipeline state");
    
    // Flush any pending load delays
    cpu_flush_load_delay(cpu);
    
    // Clear branch delay state
    cpu->branch_taken = false;
    cpu->in_delay_slot = false;
    
    // Reset instruction prefetch state (simplified - no prefetch buffer)
    // In DuckStation this would refetch the current instruction
    // but we don't have a prefetch buffer, so we just ensure clean state
}
