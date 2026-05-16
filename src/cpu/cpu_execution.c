#include "cpu.h"
#include <string.h>
#include "interconnect.h"
#include "event_scheduler.h"
#include "gte.h"
#include "timers.h"
#include "debugger.h"

// ============================================================================
// CPU Execution Loop - DuckStation Style
// ============================================================================

// Check for pending hardware interrupt - called once per instruction
static inline bool CheckPendingInterrupt(Cpu* cpu) {
    // Per PSX-SPX: interrupt pending if (I_STAT & I_MASK) != 0 AND SR.IE == 1
    uint16_t i_stat = cpu->inter->irq_status;
    uint16_t i_mask = cpu->inter->irq_mask;
    bool irq_pending = (i_stat & i_mask) != 0;
    
    // Update COP0 Cause bit 10 (IP2) based on current state - NOT a latch!
    if (irq_pending) {
        cpu->cause |= (1u << 10);
    } else {
        cpu->cause &= ~(1u << 10);
    }
    
    // Check: SR.IEc && ((cause & sr) & 0xFF00) != 0
    bool sr_iec = (cpu->sr & 1) != 0;
    uint32_t sr_cause_masked = (cpu->sr & cpu->cause) & 0xFF00;
    bool has_interrupt = sr_iec && (sr_cause_masked != 0);
    
    if (has_interrupt) {
        /* PSX-SPX: do not take IRQ if the next instruction is a GTE opcode (COP2 data op).
           Defer until the GTE instruction completes to avoid EPC pointing into a GTE op. */
        uint32_t next_instr = cpu_icache_fetch(cpu, cpu->current_pc);
        if ((next_instr & 0xFE000000) == 0x4A000000) return false;
        cpu_exception(cpu, EXCEPTION_INTERRUPT);
        return true;
    }
    return false;
}

// Main execution cycle - called for each instruction
void cpu_run_next_instruction(Cpu* cpu) {
    // exception_pending is per-instruction state; clear it before running this step.
    cpu->exception_pending = false;

    // --- GTE Busy Stalling ---
    // Decrement GTE cycles remaining; clear busy when complete
    if (cpu->gte.busy && cpu->gte.cycles_remaining > 0) {
        cpu->gte.cycles_remaining--;
        if (cpu->gte.cycles_remaining == 0) {
            cpu->gte.busy = false;
        }
    }

    // --- GTE Load Delay Advancement (Phase B5) ---
    // Shift pending delayed value to current, preparing it to be returned on next MFC2
    if (cpu->gte_next_load_delay_reg != 255) {
        cpu->gte_load_delay_reg = cpu->gte_next_load_delay_reg;
        cpu->gte_load_delay_value = cpu->gte_next_load_delay_value;
        cpu->gte_next_load_delay_reg = 255;  // Clear pending
        cpu->gte_next_load_delay_value = 0;
    }

    // --- 1. Handle Load Delay from previous instruction ---
    // Must commit before the interrupt check so the register file is consistent
    // at exception entry (EPC points to the interrupted instruction, regs already updated).
    if (cpu->load_reg_idx != REG_ZERO) {
        cpu_set_reg(cpu, cpu->load_reg_idx, cpu->load_value);
        cpu->load_reg_idx = REG_ZERO;
    }

    // Establish current-instruction context before any potential interrupt exception.
    // This matches R3000A behavior where IRQ is taken between instructions, and BD/EPC
    // are derived from the instruction about to execute.
    cpu->current_pc = cpu->pc;
    cpu->in_delay_slot = cpu->branch_taken;

    // --- Breakpoint check (before executing the instruction) ---
    if (!cpu->inter->debugger.step_skip_bp) {
        debugger_check_breakpoint(&cpu->inter->debugger, cpu);
        if (cpu->inter->debugger.paused) return;
    } else {
        cpu->inter->debugger.step_skip_bp = false;
    }

    // --- 2. Check for pending interrupt ---
    if (CheckPendingInterrupt(cpu)) {
        return; // Exception raised, PC already updated
    }

    // --- 3. Fetch Instruction ---
    
    // Check PC alignment
    if (cpu->current_pc % 4 != 0) {
        cpu_exception(cpu, EXCEPTION_LOAD_ADDRESS_ERROR);
        return;
    }
    
    uint32_t instruction = cpu_icache_fetch(cpu, cpu->current_pc);

    // --- 4. Update Branch State ---
    cpu->branch_taken = false;
    
    // Advance PC
    cpu->pc = cpu->next_pc;
    cpu->next_pc = cpu->pc + 4;
    
    // --- 5. Commit Register State ---
    memcpy(cpu->regs, cpu->out_regs, sizeof(cpu->regs));
    cpu->regs[REG_ZERO] = 0;
    
    // --- 6. Decode and Execute ---
    decode_and_execute(cpu, instruction);
    if (cpu->exception_pending) {
        return;
    }
    
    // --- 7. Finalize ---
    cpu->out_regs[REG_ZERO] = 0;

    // --- 8. Advance Cycle Counters ---
    cpu->inter->cpu_cycle_counter++;
    cpu->downcount--;

    // Step timers every 64 cycles so counters advance for BIOS busy-waits.
    if ((cpu->inter->cpu_cycle_counter & 0x3F) == 0) {
        timers_step(&cpu->inter->timers_state, 64);
    }

    // --- 9. Dispatch Events (DuckStation-style downcount) ---
    if (cpu->downcount <= 0) {
        eventq_dispatch_due(cpu->inter);
        // Recalculate downcount = cycles until next scheduled event
        uint32_t next = cpu->inter->evq_next_cycle;
        uint32_t now  = cpu->inter->cpu_cycle_counter;
        cpu->downcount = (next != UINT32_MAX && (int32_t)(next - now) > 0)
                       ? (int32_t)(next - now) : 1;
    }

    // --- 10. Check CDROM custom events ---
    interconnect_check_cdrom_events(cpu->inter);
}
