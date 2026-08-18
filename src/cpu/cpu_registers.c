/* SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 ZioZoni95
 *
 * Part of ZoniStation One, a PlayStation 1 emulator.
 * See LICENSE for the full licence text and THIRD-PARTY.md for the
 * components of this project that have other authors.
 */
#include "cpu.h"
#include "log.h"

// --- Register Access ---
/**
 * @brief Reads the value of a GPR from the input set (cpu->regs).
 */
uint32_t cpu_reg(Cpu* cpu, RegisterIndex index) {
    // No need to check index 0 specifically, as cpu->regs[0] is always 0.
    if (index >= 32) {
        LOG_CPU_ERROR("[CPU] GPR read index out of bounds: %u", index);
        return 0; // Or trigger an internal error
    }
    return cpu->regs[index];
}

/**
 * @brief Writes a value to a GPR. R0 stays hardwired to zero.
 */
void cpu_set_reg(Cpu* cpu, RegisterIndex index, uint32_t value) {
    if (index >= 32) {
        LOG_CPU_ERROR("[CPU] GPR write index out of bounds: %u", index);
        return;
    }
    /* R0 is hardwired: dropping the write is the whole of it. The old code
     * wrote into a second register file and then re-zeroed its slot 0 on every
     * call, which the branch above already makes unnecessary. */
    if (index != REG_ZERO) {
        cpu->regs[index] = value;
        /* A load in flight for this same register loses the race. Its data "isn't
         * updated until the next opcode has completed"
         * (psx-spx-docs/docs/cpuspecifications.md:172-174) — this write belongs to
         * that next opcode, so it is the later one and stands. Without the cancel
         * the load would land afterwards and quietly undo it. */
        if (index == cpu->delay_load_reg) {
            cpu->delay_load_reg = REG_ZERO;
        }
    }
}


// --- Branch/Jump Helper ---
/**
 * @brief Updates next_pc for branch instructions based on offset.
 */
void cpu_branch(Cpu* cpu, uint32_t offset_se) {
    // MIPS branch offsets are relative to the instruction *after* the delay slot (PC+4),
    // but since our 'current_pc' points to the branch itself, the effective base is current_pc+4.
    // The offset is shifted left by 2 because it's word-aligned.
    uint32_t branch_offset = offset_se << 2;
    cpu->next_pc = cpu->current_pc + 4 + branch_offset; // Target is relative to PC+4
    // The instruction handler (e.g., op_beq) MUST set cpu->branch_taken = true after calling this.
}