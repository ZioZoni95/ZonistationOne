#include "cpu.h"
#include "log.h"
#include "interconnect.h"
#include "gte.h"
#include "timers.h"

// --- Individual Instruction Implementations ---
// (Keep essential debug prints only: exceptions, cache isolation, GTE/COP errors)

void op_lui(Cpu* cpu, uint32_t instruction) {
    uint32_t imm = instr_imm(instruction);
    uint32_t rt = instr_t(instruction);
    cpu_set_reg(cpu, rt, imm << 16);
}

void op_ori(Cpu* cpu, uint32_t instruction) {
    uint32_t imm = instr_imm(instruction);
    uint32_t rt = instr_t(instruction);
    uint32_t rs = instr_s(instruction);
    cpu_set_reg(cpu, rt, cpu_reg(cpu, rs) | imm);
}

void op_sw(Cpu* cpu, uint32_t instruction) {
    if ((cpu->sr & 0x10000) != 0) // Cache isolated — writes go to I-cache, not RAM
        return;
    uint32_t offset = instr_imm_se(instruction);
    uint32_t rt = instr_t(instruction);
    uint32_t rs = instr_s(instruction);
    uint32_t address = cpu_reg(cpu, rs) + offset;
    uint32_t value = cpu_reg(cpu, rt); // Use input register set
    // Enforce word alignment for SW
    if ((address & 3) != 0) {
        LOG_CPU_ERROR("[CPU] SW Address Error: Unaligned address 0x%08x = 0x%08x @ 0x%08x", address, value, cpu->current_pc);
        cpu->badvaddr = address;
        cpu_exception(cpu, EXCEPTION_STORE_ADDRESS_ERROR);
        return;
    }
    interconnect_store32(cpu->inter, address, value);
}

void op_sll(Cpu* cpu, uint32_t instruction) {
    // NOP is SLL R0, R0, 0. Check for it to avoid calculation.
    if (instruction == 0) return; // Common NOP
    uint32_t shamt = instr_shift(instruction);
    uint32_t rt = instr_t(instruction);
    uint32_t rd = instr_d(instruction);
    cpu_set_reg(cpu, rd, cpu_reg(cpu, rt) << shamt);
}

void op_addiu(Cpu* cpu, uint32_t instruction) {
    uint32_t imm_se = instr_imm_se(instruction);
    uint32_t rt = instr_t(instruction);
    uint32_t rs = instr_s(instruction);
    cpu_set_reg(cpu, rt, cpu_reg(cpu, rs) + imm_se); // Unsigned addition wraps naturally
}

void op_j(Cpu* cpu, uint32_t instruction) {
    uint32_t target_imm = instr_imm_jump(instruction);
    // Upper 4 bits come from the delay slot's PC (cpu->pc already advanced to delay slot).
    cpu->next_pc = (cpu->pc & 0xF0000000) | (target_imm << 2);
    cpu->branch_taken = true;
}

void op_or(Cpu* cpu, uint32_t instruction) {
    uint32_t rd = instr_d(instruction);
    uint32_t rs = instr_s(instruction);
    uint32_t rt = instr_t(instruction);
    cpu_set_reg(cpu, rd, cpu_reg(cpu, rs) | cpu_reg(cpu, rt));
}

/* COP0 rs-field dispatch (bits 25:21 — 32 slots). */
static void op_cop0_co(Cpu* cpu, uint32_t instruction) {
    /* COP0 CO: only subfunction 0x10 = RFE is valid on R3000A */
    if ((instruction & 0x3F) == 0x10) op_rfe(cpu, instruction);
    else op_illegal(cpu, instruction);
}

void op_cop0(Cpu* cpu, uint32_t instruction) {
    static const cpu_handler_t s_cop0_table[32] = {
        [0x00] = op_mfc0,
        [0x04] = op_mtc0,
        [0x10] = op_cop0_co,
    };
    uint32_t rs = (instruction >> 21) & 0x1F;
    cpu_handler_t h = s_cop0_table[rs];
    if (h) h(cpu, instruction);
    else {
        LOG_CPU_WARN("[CPU] Unhandled COP0 rs=%u instr=0x%08x @ 0x%08x", rs, instruction, cpu->current_pc);
        cpu_exception(cpu, EXCEPTION_ILLEGAL_INSTRUCTION);
    }
}

void op_mtc0(Cpu* cpu, uint32_t instruction) {
    uint32_t cpu_r = instr_t(instruction); // Source CPU register
    uint32_t cop_r = instr_d(instruction); // Destination COP0 register
    uint32_t value = cpu_reg(cpu, cpu_r);

    switch (cop_r) {
        case 3: case 5: case 6: case 7: case 9: case 11: // Breakpoint/DCIC regs
             if (value != 0) LOG_CPU_DEBUG("[CPU] MTC0 breakpoint/DCIC reg %u <- 0x%08x", cop_r, value);
             break;
        case 12: // SR (Status Register)
// Apply hardware write mask (DuckStation SR::WRITE_MASK = 0xF27FFFFF).
            cpu->sr = (cpu->sr & ~0xF27FFFFF) | (value & 0xF27FFFFF);
            break;
        case 13: // CAUSE
             // Only bits 8 and 9 (IP0, IP1) seem writable to force software interrupts.
             // Mask other bits.
             cpu->cause = (cpu->cause & ~0x300) | (value & 0x300);
             if ((value & ~0x300) != 0) {
                 LOG_CPU_WARN("[CPU] MTC0 to CAUSE attempting to write non-SW bits: 0x%08x at @ 0x%08x", value, cpu->current_pc);
             }
             break;
        case 8:  // BadVaddr — read-only
        case 14: // EPC — read-only
            LOG_CPU_DEBUG("[CPU] MTC0 to read-only reg %u <- 0x%08x (ignored)", cop_r, value);
            break;
        default:
            LOG_CPU_WARN("[CPU] MTC0 unhandled COP0 reg %u <- 0x%08x at 0x%08x", cop_r, value, cpu->current_pc);
            cpu_exception(cpu, EXCEPTION_ILLEGAL_INSTRUCTION);
            break;
    }
}

void op_rfe(Cpu* cpu, uint32_t instruction) {
    (void)instruction;
    // RFE: Return from Exception - restores the R3000A mode stack (bits 0-5 of SR).
    // Pop: IEp/KUp -> IEc/KUc, IEo/KUo -> IEp/KUp.
    // IEo/KUo (bits 4-5) are preserved unchanged (not popped off).
    // There is no EXL bit on R3000A; SR bit 10 has no meaning here.
    uint32_t old_sr = cpu->sr;
    uint32_t mode = old_sr & 0x3F;
    // Keep bits 4-5 (IEo/KUo) as-is. Shift right by 2 fills bits 0-3.
    cpu->sr = (old_sr & ~0x3F) | ((mode & 0x30) | (mode >> 2));
}

void op_bne(Cpu* cpu, uint32_t instruction) {
    uint32_t imm_se = instr_imm_se(instruction);
    uint32_t rs = instr_s(instruction);
    uint32_t rt = instr_t(instruction);
    if (cpu_reg(cpu, rs) != cpu_reg(cpu, rt)) {
        cpu_branch(cpu, imm_se);
        cpu->branch_taken = true;
    }
}

void op_addi(Cpu* cpu, uint32_t instruction) {
    int32_t imm_se = (int32_t)instr_imm_se(instruction);
    uint32_t rt = instr_t(instruction);
    uint32_t rs = instr_s(instruction);
    int32_t rs_value = (int32_t)cpu_reg(cpu, rs);
    int32_t result;
    // Use GCC/Clang builtin for checked signed addition
    if (__builtin_add_overflow(rs_value, imm_se, &result)) {
        LOG_CPU_ERROR("[CPU] ADDI Signed Overflow: %d + %d @ 0x%08x", rs_value, imm_se, cpu->current_pc);
        cpu_exception(cpu, EXCEPTION_OVERFLOW); // Trigger overflow exception
    } else {
        cpu_set_reg(cpu, rt, (uint32_t)result);
    }
}

void op_lw(Cpu* cpu, uint32_t instruction) {
    if ((cpu->sr & 0x10000) != 0) { // Check cache isolation
// Keep debug print
        return;
    }
    uint32_t offset = instr_imm_se(instruction);
    uint32_t rt = instr_t(instruction);
    uint32_t rs = instr_s(instruction);
    uint32_t address = cpu_reg(cpu, rs) + offset;

    // Enforce word alignment per PSX spec: word accesses must be 4-byte aligned.
    if ((address & 3) != 0) {
        LOG_CPU_ERROR("[CPU] LW Address Error: Unaligned address 0x%08x @ 0x%08x", address, cpu->current_pc);
        cpu->badvaddr = address;
        cpu_exception(cpu, EXCEPTION_LOAD_ADDRESS_ERROR);
        return;
    }

    // Perform load and schedule it for the delay slot
    uint32_t value_loaded = interconnect_load32(cpu->inter, address);
    cpu->load_reg_idx = rt;
    cpu->load_value = value_loaded;
}

void op_sltu(Cpu* cpu, uint32_t instruction) {
    uint32_t rd = instr_d(instruction);
    uint32_t rs = instr_s(instruction);
    uint32_t rt = instr_t(instruction);
    cpu_set_reg(cpu, rd, (cpu_reg(cpu, rs) < cpu_reg(cpu, rt)) ? 1 : 0);
}

void op_addu(Cpu* cpu, uint32_t instruction) {
    uint32_t rd = instr_d(instruction);
    uint32_t rs = instr_s(instruction);
    uint32_t rt = instr_t(instruction);
    cpu_set_reg(cpu, rd, cpu_reg(cpu, rs) + cpu_reg(cpu, rt));
}

void op_sh(Cpu* cpu, uint32_t instruction) {
    if ((cpu->sr & 0x10000) != 0) {
// Keep debug print
        return;
    }
    uint32_t offset = instr_imm_se(instruction);
    uint32_t rt = instr_t(instruction);
    uint32_t rs = instr_s(instruction);
    uint32_t address = cpu_reg(cpu, rs) + offset;
    // Enforce halfword alignment for SH
    if ((address & 1) != 0) {
        LOG_CPU_ERROR("[CPU] SH Address Error: Unaligned address 0x%08x @ 0x%08x", address, cpu->current_pc);
        cpu->badvaddr = address;
        cpu_exception(cpu, EXCEPTION_STORE_ADDRESS_ERROR);
        return;
    }
    uint16_t value = (uint16_t)cpu_reg(cpu, rt); // Lower 16 bits of rt
    interconnect_store16(cpu->inter, address, value);
}

void op_jal(Cpu* cpu, uint32_t instruction) {
    cpu_set_reg(cpu, REG_RA, cpu->pc + 4); // Link Register $31 gets PC+8 (address after delay slot)
    uint32_t target_imm = instr_imm_jump(instruction);
    // Upper 4 bits come from the delay slot's PC (cpu->pc already advanced to delay slot).
    cpu->next_pc = (cpu->pc & 0xF0000000) | (target_imm << 2);
    cpu->branch_taken = true;
}

void op_andi(Cpu* cpu, uint32_t instruction) {
    uint32_t imm = instr_imm(instruction); // Zero-extended immediate
    uint32_t rt = instr_t(instruction);
    uint32_t rs = instr_s(instruction);
    cpu_set_reg(cpu, rt, cpu_reg(cpu, rs) & imm);
}

void op_sb(Cpu* cpu, uint32_t instruction) {
    if ((cpu->sr & 0x10000) != 0) {
// Keep debug print
        return;
    }
    uint32_t offset = instr_imm_se(instruction);
    uint32_t rt = instr_t(instruction);
    uint32_t rs = instr_s(instruction);
    uint32_t address = cpu_reg(cpu, rs) + offset;
    uint8_t value = (uint8_t)cpu_reg(cpu, rt); // Lower 8 bits of rt
    interconnect_store8(cpu->inter, address, value);
}

void op_jr(Cpu* cpu, uint32_t instruction) {
    uint32_t rs = instr_s(instruction);
    uint32_t target_address = cpu_reg(cpu, rs);
    
    // Detect BIOS function vector calls — DuckStation-style LLE side-channel capture.
    // The BIOS will still execute normally (cpu->next_pc = target_address below).
    if (target_address == 0x000000A0 || target_address == 0x000000B0 || target_address == 0x000000C0) {
        uint32_t func_num = cpu_reg(cpu, 9);
        if (func_num >= 0x100 || func_num == 0) {
            uint32_t alt = cpu_reg(cpu, 10);
            if (alt < 0x100 && alt != 0) func_num = alt;
        }
        (void)func_num; /* used by handlers; avoid unused warning if logging is off */

        if (target_address == 0x000000A0)
            handle_a0_syscall(cpu);
        else if (target_address == 0x000000B0)
            handle_b0_syscall(cpu);
        else if (target_address == 0x000000C0)
            handle_c0_syscall(cpu);
        /* LLE: BIOS native code executes normally */
    }
    /* Log truly suspicious jumps: only unaligned targets (BIOS routinely jumps to low RAM) */
    else if ((target_address & 0x3) != 0) {
}
    
    // Dedicated log for suspicious infinite loop at 0x00001010
    if (target_address == 0x00001010 && cpu->current_pc == 0x00001010 && rs == 26) {
        // Suppressed full CPU state dump for 0x1010 loop to avoid excessive logs
}
    cpu->next_pc = target_address;
    cpu->branch_taken = true;
    // Alignment check will happen on fetch in the next cycle
}

void op_lb(Cpu* cpu, uint32_t instruction) {
    if ((cpu->sr & 0x10000) != 0) {
// Keep debug print
        return;
    }
    uint32_t offset = instr_imm_se(instruction);
    uint32_t rt = instr_t(instruction);
    uint32_t rs = instr_s(instruction);
    uint32_t address = cpu_reg(cpu, rs) + offset;
    uint8_t value_loaded = interconnect_load8(cpu->inter, address);
    // Sign-extend the 8-bit value to 32 bits
    uint32_t value_sign_extended = (uint32_t)(int32_t)(int8_t)value_loaded;
    // Schedule load for delay slot
    cpu->load_reg_idx = rt;
    cpu->load_value = value_sign_extended;
}

void op_beq(Cpu* cpu, uint32_t instruction) {
    uint32_t imm_se = instr_imm_se(instruction);
    uint32_t rs = instr_s(instruction);
    uint32_t rt = instr_t(instruction);
    if (cpu_reg(cpu, rs) == cpu_reg(cpu, rt)) {
        cpu_branch(cpu, imm_se);
        cpu->branch_taken = true;
    }
}

void op_mfc0(Cpu* cpu, uint32_t instruction) {
    uint32_t cpu_r_dest = instr_t(instruction); // Target CPU register
    uint32_t cop_r_src = instr_d(instruction);  // Source COP0 register
    uint32_t value_read = 0; // Default value if read fails or is unhandled

    switch (cop_r_src) {
        case  3: value_read = 0; break; // BPC (breakpoint on execute) — not implemented, return 0
        case  5: value_read = 0; break; // BDA (breakpoint on data access) — not implemented, return 0
        case  6: value_read = 0; break; // JUMPDEST (TAR, target address register) — not implemented, return 0
        case  7: value_read = 0; break; // DCIC (debug/cache control) — not implemented, return 0
        case  8: value_read = cpu->badvaddr; break; // BadVaddr
        case  9: value_read = 0; break; // BDAM (breakpoint data access mask) — not implemented, return 0
        case 11: value_read = 0; break; // BPCM (breakpoint execute mask) — not implemented, return 0
        case 12: value_read = cpu->sr; break; // SR
        case 13: value_read = cpu->cause; break; // CAUSE
        case 14: value_read = cpu->epc; break; // EPC
        case 15: value_read = cpu->prid; break; // PRID
        default:
            LOG_CPU_DEBUG("[CPU] MFC0 unhandled reg %u at 0x%08x", cop_r_src, cpu->current_pc);
            cpu_exception(cpu, EXCEPTION_ILLEGAL_INSTRUCTION);
            return;
    }
    // Schedule load for delay slot
    cpu->load_reg_idx = cpu_r_dest;
    cpu->load_value = value_read;
}

void op_and(Cpu* cpu, uint32_t instruction) {
    uint32_t rd = instr_d(instruction);
    uint32_t rs = instr_s(instruction);
    uint32_t rt = instr_t(instruction);
    cpu_set_reg(cpu, rd, cpu_reg(cpu, rs) & cpu_reg(cpu, rt));
}

void op_add(Cpu* cpu, uint32_t instruction) {
    uint32_t rd = instr_d(instruction);
    uint32_t rs = instr_s(instruction);
    uint32_t rt = instr_t(instruction);
    int32_t rs_value = (int32_t)cpu_reg(cpu, rs);
    int32_t rt_value = (int32_t)cpu_reg(cpu, rt);
    int32_t result;
    // Use GCC/Clang builtin for checked signed addition
    if (__builtin_add_overflow(rs_value, rt_value, &result)) {
        LOG_CPU_ERROR("[CPU] ADD Signed Overflow: %d + %d @ 0x%08x", rs_value, rt_value, cpu->current_pc);
        cpu_exception(cpu, EXCEPTION_OVERFLOW); // Trigger overflow exception
    } else {
        cpu_set_reg(cpu, rd, (uint32_t)result);
    }
}

void op_bgtz(Cpu* cpu, uint32_t instruction) {
    uint32_t imm_se = instr_imm_se(instruction);
    uint32_t rs = instr_s(instruction);
    // Comparison is signed
    if ((int32_t)cpu_reg(cpu, rs) > 0) {
        cpu_branch(cpu, imm_se);
        cpu->branch_taken = true;
    }
}

void op_blez(Cpu* cpu, uint32_t instruction) {
    uint32_t imm_se = instr_imm_se(instruction);
    uint32_t rs = instr_s(instruction);
    // Comparison is signed
    if ((int32_t)cpu_reg(cpu, rs) <= 0) {
        cpu_branch(cpu, imm_se);
        cpu->branch_taken = true;
    }
}

void op_lbu(Cpu* cpu, uint32_t instruction) {
     if ((cpu->sr & 0x10000) != 0) {
// Keep debug print
        return;
    }
    uint32_t offset = instr_imm_se(instruction);
    uint32_t rt = instr_t(instruction);
    uint32_t rs = instr_s(instruction);
    uint32_t address = cpu_reg(cpu, rs) + offset;
    uint8_t value_loaded = interconnect_load8(cpu->inter, address);
    // Zero-extend the 8-bit value to 32 bits
    uint32_t value_zero_extended = (uint32_t)value_loaded;
    // Schedule load for delay slot
    cpu->load_reg_idx = rt;
    cpu->load_value = value_zero_extended;
}

void op_jalr(Cpu* cpu, uint32_t instruction) {
    uint32_t rs = instr_s(instruction); // Register containing target address
    uint32_t rd = instr_d(instruction); // Register to store return address (defaults to $ra=31 if rd=0?)
    uint32_t target_address = cpu_reg(cpu, rs);
    uint32_t return_addr = cpu->pc + 4; // Address of instruction after delay slot

    // DISABLED for LLE mode: Let BIOS ROM handle syscalls natively
    // HLE interceptors are moved to LowLevelEmulation branch
    /*
    if (target_address == 0xA0 || target_address == 0xB0 || target_address == 0xC0) {
        uint32_t func_num = cpu_reg(cpu, 9);
        if (func_num >= 0x100 || func_num == 0) {
            uint32_t alt = cpu_reg(cpu, 10);
            if (alt < 0x100 && alt != 0) func_num = alt;
        }
        const char* vector_name = (target_address == 0xA0) ? "A" :
                                  (target_address == 0xB0) ? "B" : "C";
if (target_address == 0xA0)
            handle_a0_syscall(cpu);
        else if (target_address == 0xB0)
            handle_b0_syscall(cpu);
    }
    */
    // Log truly suspicious jumps: only unaligned targets (BIOS routinely jumps to low RAM)
    if ((target_address & 0x3) != 0) {
}

    // Store return address in rd
    cpu_set_reg(cpu, rd, return_addr);
    // Set jump target
    cpu->next_pc = target_address;
    cpu->branch_taken = true;
    // Alignment check will happen on fetch in the next cycle
}

// Handles BGEZ, BLTZ, BGEZAL, BLTZAL based on bits 20 and 16
void op_bxx(Cpu* cpu, uint32_t instruction) {
    uint32_t imm_se = instr_imm_se(instruction);
    uint32_t rs = instr_s(instruction);
    int is_bgez = (instruction >> 16) & 1; // Bit 16: 1=BGEZ, 0=BLTZ
    int is_link = (instruction >> 20) & 1; // Bit 20: 1=Link (BGEZAL/BLTZAL)
    int32_t rs_value = (int32_t)cpu_reg(cpu, rs);

    // Determine condition met
    bool condition_met;
    if (is_bgez) { // BGEZ or BGEZAL
        condition_met = (rs_value >= 0);
    } else { // BLTZ or BLTZAL
        condition_met = (rs_value < 0);
    }

    // Per MIPS spec: BGEZAL/BLTZAL write $ra UNCONDITIONALLY (even if branch not taken).
    if (is_link) {
        cpu_set_reg(cpu, REG_RA, cpu->pc + 4);
    }
    if (condition_met) {
        cpu_branch(cpu, imm_se);
        cpu->branch_taken = true;
    }
}

void op_slti(Cpu* cpu, uint32_t instruction) {
    int32_t imm_se = (int32_t)instr_imm_se(instruction); // Immediate is signed
    uint32_t rt = instr_t(instruction);
    uint32_t rs = instr_s(instruction);
    // Comparison is signed
    cpu_set_reg(cpu, rt, ((int32_t)cpu_reg(cpu, rs) < imm_se) ? 1 : 0);
}

void op_subu(Cpu* cpu, uint32_t instruction) {
    uint32_t rd = instr_d(instruction);
    uint32_t rs = instr_s(instruction);
    uint32_t rt = instr_t(instruction);
    cpu_set_reg(cpu, rd, cpu_reg(cpu, rs) - cpu_reg(cpu, rt)); // Unsigned wraps
}

void op_sra(Cpu* cpu, uint32_t instruction) {
    uint32_t shamt = instr_shift(instruction);
    uint32_t rt = instr_t(instruction);
    uint32_t rd = instr_d(instruction);
    int32_t value_signed = (int32_t)cpu_reg(cpu, rt);
    // Arithmetic shift preserves sign bit
    cpu_set_reg(cpu, rd, (uint32_t)(value_signed >> shamt));
}

// PSX MIPS mul/div latencies (DuckStation cpu_core.cpp values)
#define MULT_TICKS  7   // MULT / MULTU
#define DIV_TICKS  37   // DIV / DIVU

// Signed division
void op_div(Cpu* cpu, uint32_t instruction) {
    uint32_t rs = instr_s(instruction);
    uint32_t rt = instr_t(instruction);
    int32_t n = (int32_t)cpu_reg(cpu, rs); // Numerator
    int32_t d = (int32_t)cpu_reg(cpu, rt); // Denominator

    // Handle special cases according to MIPS spec / Guide Table 7
    if (d == 0) { // Division by zero
        cpu->hi = (uint32_t)n;
        cpu->lo = (n >= 0) ? 0xffffffff : 1;
    } else if ((uint32_t)n == 0x80000000 && d == -1) { // Overflow case: MinInt / -1
        cpu->hi = 0;
        cpu->lo = 0x80000000; // Result is MinInt
    } else { // Normal division
        cpu->lo = (uint32_t)(n / d); // Quotient
        cpu->hi = (uint32_t)(n % d); // Remainder
    }
    cpu->muldiv_completion_tick = cpu->inter->cpu_cycle_counter + DIV_TICKS;
}

// Unsigned division
void op_divu(Cpu* cpu, uint32_t instruction) {
    uint32_t rs = instr_s(instruction);
    uint32_t rt = instr_t(instruction);
    uint32_t n = cpu_reg(cpu, rs);
    uint32_t d = cpu_reg(cpu, rt);

    if (d == 0) { // Division by zero
        cpu->hi = n;          // Remainder is numerator
        cpu->lo = 0xffffffff; // Quotient is -1
    } else { // Normal division
        cpu->lo = n / d; // Quotient
        cpu->hi = n % d; // Remainder
    }
    cpu->muldiv_completion_tick = cpu->inter->cpu_cycle_counter + DIV_TICKS;
}

// Move From LO
void op_mflo(Cpu* cpu, uint32_t instruction) {
    uint32_t rd = instr_d(instruction);
    // Stall until MulDiv completes (DuckStation-style)
    if (cpu->inter->cpu_cycle_counter < cpu->muldiv_completion_tick) {
        uint32_t stall = cpu->muldiv_completion_tick - cpu->inter->cpu_cycle_counter;
        cpu->inter->cpu_cycle_counter += stall;
        cpu->downcount -= (int32_t)stall;
    }
    cpu_set_reg(cpu, rd, cpu->lo);
}

// Shift Right Logical
void op_srl(Cpu* cpu, uint32_t instruction) {
    uint32_t shamt = instr_shift(instruction);
    uint32_t rt = instr_t(instruction);
    uint32_t rd = instr_d(instruction);
    // Logical shift fills with zeros
    cpu_set_reg(cpu, rd, cpu_reg(cpu, rt) >> shamt);
}

// Set if Less Than Immediate Unsigned
void op_sltiu(Cpu* cpu, uint32_t instruction) {
    uint32_t imm_se = instr_imm_se(instruction); // Immediate is sign-extended
    uint32_t rt = instr_t(instruction);
    uint32_t rs = instr_s(instruction);
    // Comparison is unsigned
    cpu_set_reg(cpu, rt, (cpu_reg(cpu, rs) < imm_se) ? 1 : 0);
}

// Set on Less Than (Signed)
void op_slt(Cpu* cpu, uint32_t instruction) {
    uint32_t rd = instr_d(instruction);
    uint32_t rs = instr_s(instruction);
    uint32_t rt = instr_t(instruction);
    // Comparison is signed
    cpu_set_reg(cpu, rd, ((int32_t)cpu_reg(cpu, rs) < (int32_t)cpu_reg(cpu, rt)) ? 1 : 0);
}

// Move From HI
void op_mfhi(Cpu* cpu, uint32_t instruction) {
    uint32_t rd = instr_d(instruction);
    // Stall until MulDiv completes (DuckStation-style)
    if (cpu->inter->cpu_cycle_counter < cpu->muldiv_completion_tick) {
        uint32_t stall = cpu->muldiv_completion_tick - cpu->inter->cpu_cycle_counter;
        cpu->inter->cpu_cycle_counter += stall;
        cpu->downcount -= (int32_t)stall;
    }
    cpu_set_reg(cpu, rd, cpu->hi);
}

// System Call - DuckStation style: just raise exception, handle in main loop
void op_syscall(Cpu* cpu, uint32_t instruction) {
    (void)instruction;
    // DuckStation just triggers the exception - the syscall is handled by BIOS code
    // We only intercept A0/B0 syscall vectors in the main execution loop
    cpu_exception(cpu, EXCEPTION_SYSCALL);
}

// Bitwise Not Or
void op_nor(Cpu* cpu, uint32_t instruction) {
    uint32_t rd = instr_d(instruction);
    uint32_t rs = instr_s(instruction);
    uint32_t rt = instr_t(instruction);
    cpu_set_reg(cpu, rd, ~(cpu_reg(cpu, rs) | cpu_reg(cpu, rt))); //
}

// Move To LO
void op_mtlo(Cpu* cpu, uint32_t instruction) {
    uint32_t rs = instr_s(instruction);
    cpu->lo = cpu_reg(cpu, rs); //
    // TODO: Writing HI/LO can interfere with ongoing DIV/MULT. Ignored for now.
}

// Move To HI
void op_mthi(Cpu* cpu, uint32_t instruction) {
    uint32_t rs = instr_s(instruction);
    cpu->hi = cpu_reg(cpu, rs); //
    // TODO: Timing/interlock implications ignored.
}

// Load Halfword Unsigned
void op_lhu(Cpu* cpu, uint32_t instruction) {
     if ((cpu->sr & 0x10000) != 0) {
// Keep debug print
        return;
    }
    uint32_t offset = instr_imm_se(instruction);
    uint32_t rt = instr_t(instruction);
    uint32_t rs = instr_s(instruction);
    uint32_t address = cpu_reg(cpu, rs) + offset;
    // Enforce halfword alignment: addresses must be 2-byte aligned
    if ((address & 1) != 0) {
        LOG_CPU_ERROR("[CPU] LHU Address Error: Unaligned address 0x%08x @ 0x%08x", address, cpu->current_pc);
        cpu->badvaddr = address;
        cpu_exception(cpu, EXCEPTION_LOAD_ADDRESS_ERROR);
        return;
    }
    uint16_t value_loaded = interconnect_load16(cpu->inter, address);
    // Zero-extend the 16-bit value
    uint32_t value_zero_extended = (uint32_t)value_loaded;
    // Schedule load for delay slot
    cpu->load_reg_idx = rt;
    cpu->load_value = value_zero_extended;
}

// Load Halfword (Signed)
void op_lh(Cpu* cpu, uint32_t instruction) {
    if ((cpu->sr & 0x10000) != 0) {
// Keep debug print
        return;
    }
    uint32_t offset = instr_imm_se(instruction);
    uint32_t rt = instr_t(instruction);
    uint32_t rs = instr_s(instruction);
    uint32_t address = cpu_reg(cpu, rs) + offset;
    // Enforce halfword alignment: addresses must be 2-byte aligned
    if ((address & 1) != 0) {
        LOG_CPU_ERROR("[CPU] LH Address Error: Unaligned address 0x%08x @ 0x%08x", address, cpu->current_pc);
        cpu->badvaddr = address;
        cpu_exception(cpu, EXCEPTION_LOAD_ADDRESS_ERROR);
        return;
    }
    uint16_t value_loaded = interconnect_load16(cpu->inter, address);
    // Sign-extend the 16-bit value
    uint32_t value_sign_extended = (uint32_t)(int32_t)(int16_t)value_loaded;
    // Schedule load for delay slot
    cpu->load_reg_idx = rt;
    cpu->load_value = value_sign_extended;
}

// Shift Left Logical Variable
void op_sllv(Cpu* cpu, uint32_t instruction) {
    uint32_t rd = instr_d(instruction);
    uint32_t rs = instr_s(instruction); // Register containing shift amount
    uint32_t rt = instr_t(instruction); // Register to shift
    // Shift amount uses only lower 5 bits
    uint32_t shift_amount = cpu_reg(cpu, rs) & 0x1F;
    cpu_set_reg(cpu, rd, cpu_reg(cpu, rt) << shift_amount);
}

// Shift Right Arithmetic Variable
void op_srav(Cpu* cpu, uint32_t instruction) {
    uint32_t rd = instr_d(instruction);
    uint32_t rs = instr_s(instruction); // Register containing shift amount
    uint32_t rt = instr_t(instruction); // Register to shift
    uint32_t shift_amount = cpu_reg(cpu, rs) & 0x1F; // Lower 5 bits
    int32_t value_signed = (int32_t)cpu_reg(cpu, rt);
    // Arithmetic shift
    cpu_set_reg(cpu, rd, (uint32_t)(value_signed >> shift_amount));
}

// Shift Right Logical Variable
void op_srlv(Cpu* cpu, uint32_t instruction) {
    uint32_t rd = instr_d(instruction);
    uint32_t rs = instr_s(instruction); // Register containing shift amount
    uint32_t rt = instr_t(instruction); // Register to shift
    uint32_t shift_amount = cpu_reg(cpu, rs) & 0x1F; // Lower 5 bits
    // Logical shift
    cpu_set_reg(cpu, rd, cpu_reg(cpu, rt) >> shift_amount);
}

// Multiply Unsigned
void op_multu(Cpu* cpu, uint32_t instruction) {
    uint32_t rs = instr_s(instruction);
    uint32_t rt = instr_t(instruction);
    uint64_t val_rs = (uint64_t)cpu_reg(cpu, rs);
    uint64_t val_rt = (uint64_t)cpu_reg(cpu, rt);
    uint64_t result_64 = val_rs * val_rt;
    cpu->hi = (uint32_t)(result_64 >> 32);
    cpu->lo = (uint32_t)(result_64 & 0xFFFFFFFF);
    cpu->muldiv_completion_tick = cpu->inter->cpu_cycle_counter + MULT_TICKS;
}

// Bitwise Exclusive Or
void op_xor(Cpu* cpu, uint32_t instruction) {
     uint32_t rd = instr_d(instruction);
    uint32_t rs = instr_s(instruction);
    uint32_t rt = instr_t(instruction);
    cpu_set_reg(cpu, rd, cpu_reg(cpu, rs) ^ cpu_reg(cpu, rt));
}

// Breakpoint
void op_break(Cpu* cpu, uint32_t instruction) {
    (void)instruction;
    LOG_CPU_DEBUG("[CPU] BREAK opcode at 0x%08x", cpu->current_pc);
    cpu_exception(cpu, EXCEPTION_BREAK);
}

// Multiply (Signed)
void op_mult(Cpu* cpu, uint32_t instruction) {
    uint32_t rs = instr_s(instruction);
    uint32_t rt = instr_t(instruction);
    int64_t val_rs_s = (int64_t)(int32_t)cpu_reg(cpu, rs);
    int64_t val_rt_s = (int64_t)(int32_t)cpu_reg(cpu, rt);
    int64_t result_s64 = val_rs_s * val_rt_s;
    cpu->hi = (uint32_t)((uint64_t)result_s64 >> 32);
    cpu->lo = (uint32_t)((uint64_t)result_s64 & 0xFFFFFFFF);
    cpu->muldiv_completion_tick = cpu->inter->cpu_cycle_counter + MULT_TICKS;
}

// Subtract (Signed, with Overflow check)
void op_sub(Cpu* cpu, uint32_t instruction) {
    uint32_t rd = instr_d(instruction);
    uint32_t rs = instr_s(instruction);
    uint32_t rt = instr_t(instruction);
    int32_t rs_value = (int32_t)cpu_reg(cpu, rs);
    int32_t rt_value = (int32_t)cpu_reg(cpu, rt);
    int32_t result;
    // Use GCC/Clang builtin for checked signed subtraction
    if (__builtin_sub_overflow(rs_value, rt_value, &result)) {
        LOG_CPU_ERROR("[CPU] SUB Signed Overflow: %d - %d @ 0x%08x", rs_value, rt_value, cpu->current_pc);
        cpu_exception(cpu, EXCEPTION_OVERFLOW); //
    } else {
        cpu_set_reg(cpu, rd, (uint32_t)result);
    }
}

// Bitwise Exclusive Or Immediate
void op_xori(Cpu* cpu, uint32_t instruction) {
    uint32_t imm = instr_imm(instruction); // Zero-extended immediate
    uint32_t rt = instr_t(instruction);
    uint32_t rs = instr_s(instruction);
    cpu_set_reg(cpu, rt, cpu_reg(cpu, rs) ^ imm);
}

// Coprocessor 1 (FPU) Opcode - Triggers exception
void op_cop1(Cpu* cpu, uint32_t instruction) {
    (void)instruction;
    cpu_exception(cpu, EXCEPTION_COPROCESSOR_ERROR);
}

void op_cop2(Cpu* cpu, uint32_t instruction) {
    if (!(cpu->sr & (1u << 30))) {
        LOG_CPU_DEBUG("[CPU] COP2 access with SR.CU2=0 at 0x%08x", cpu->current_pc);
        cpu_exception(cpu, EXCEPTION_COPROCESSOR_ERROR);
        return;
    }

    uint32_t rs      = (instruction >> 21) & 0x1F;
    uint32_t rt      = instr_t(instruction);
    uint32_t rd      = instr_d(instruction);

    /* rs bit4 set → GTE data operation (0x10-0x1F) */
    if (rs & 0x10) {
        uint32_t cycles = gte_execute_instruction(&cpu->gte, instruction);
        (void)cycles;
        return;
    }

    /* rs 0x00-0x0F → COP2 register move */
    switch (rs) {
        case 0x00: { /* MFC2: rt ← GTE data[rd] (load delay) */
            uint32_t v = (cpu->gte_load_delay_reg == rd)
                       ? cpu->gte_load_delay_value
                       : (uint32_t)gte_read_data_register(&cpu->gte, rd);
            cpu->load_reg_idx = rt;
            cpu->load_value   = v;
            break;
        }
        case 0x02: { /* CFC2: rt ← GTE ctrl[rd] (load delay) */
            cpu->load_reg_idx = rt;
            cpu->load_value   = (uint32_t)gte_read_control_register(&cpu->gte, rd);
            break;
        }
        case 0x04: /* MTC2: GTE data[rd] ← rt */
            gte_write_data_register(&cpu->gte, rd, (int32_t)cpu_reg(cpu, rt));
            break;
        case 0x06: /* CTC2: GTE ctrl[rd] ← rt */
            gte_write_control_register(&cpu->gte, rd, (int32_t)cpu_reg(cpu, rt));
            break;
        default:
            op_illegal(cpu, instruction);
            break;
    }
}

// Coprocessor 3 Opcode - Triggers exception
void op_cop3(Cpu* cpu, uint32_t instruction) {
    (void)instruction;
    cpu_exception(cpu, EXCEPTION_COPROCESSOR_ERROR);
}

// Load Word Left (Handles unaligned loads)
void op_lwl(Cpu* cpu, uint32_t instruction) {
    if ((cpu->sr & 0x10000) != 0) {
return;
    }
    uint32_t offset = instr_imm_se(instruction);
    uint32_t rt = instr_t(instruction);
    uint32_t rs = instr_s(instruction);
    uint32_t addr = cpu_reg(cpu, rs) + offset;

    // Merge with pending load value if target register matches
    uint32_t current_rt_value = (cpu->load_reg_idx == rt) ? cpu->load_value : cpu->out_regs[rt];

    uint32_t aligned_addr = addr & ~3;
    uint32_t aligned_word = interconnect_load32(cpu->inter, aligned_addr);
    uint32_t merged_value;

    // Shift and mask based on address alignment (Little Endian)
    switch (addr & 3) {
        case 0: merged_value = (current_rt_value & 0x00FFFFFF) | (aligned_word << 24); break;
        case 1: merged_value = (current_rt_value & 0x0000FFFF) | (aligned_word << 16); break;
        case 2: merged_value = (current_rt_value & 0x000000FF) | (aligned_word << 8);  break;
        case 3: merged_value = (current_rt_value & 0x00000000) | (aligned_word << 0);  break;
        default: merged_value = 0; /* Should not happen */ break;
    }
    // Schedule merged value for load delay slot
    cpu->load_reg_idx = rt;
    cpu->load_value = merged_value;
}

// Load Word Right (Handles unaligned loads)
void op_lwr(Cpu* cpu, uint32_t instruction) {
     if ((cpu->sr & 0x10000) != 0) {
return;
    }
    uint32_t offset = instr_imm_se(instruction);
    uint32_t rt = instr_t(instruction);
    uint32_t rs = instr_s(instruction);
    uint32_t addr = cpu_reg(cpu, rs) + offset;

    // Merge with pending load value if target register matches
    uint32_t current_rt_value = (cpu->load_reg_idx == rt) ? cpu->load_value : cpu->out_regs[rt];

    uint32_t aligned_addr = addr & ~3;
    uint32_t aligned_word = interconnect_load32(cpu->inter, aligned_addr);
    uint32_t merged_value;

    // Shift and mask based on address alignment (Little Endian)
    switch (addr & 3) {
        case 0: merged_value = (current_rt_value & 0x00000000) | (aligned_word >> 0);  break;
        case 1: merged_value = (current_rt_value & 0xFF000000) | (aligned_word >> 8);  break;
        case 2: merged_value = (current_rt_value & 0xFFFF0000) | (aligned_word >> 16); break;
        case 3: merged_value = (current_rt_value & 0xFFFFFF00) | (aligned_word >> 24); break;
        default: merged_value = current_rt_value; /* Should not happen */ break;
    }
    // Schedule merged value for load delay slot
    cpu->load_reg_idx = rt;
    cpu->load_value = merged_value;
}

// Store Word Left (Handles unaligned stores)
void op_swl(Cpu* cpu, uint32_t instruction) {
     if ((cpu->sr & 0x10000) != 0) {
return;
    }
    uint32_t offset = instr_imm_se(instruction);
    uint32_t rt = instr_t(instruction); // Register containing data to store
    uint32_t rs = instr_s(instruction); // Register containing base address
    uint32_t addr = cpu_reg(cpu, rs) + offset;
    uint32_t value_to_store = cpu_reg(cpu, rt); // Use input register set value

    uint32_t aligned_addr = addr & ~3;
    // Read-Modify-Write the aligned word in memory
    uint32_t current_mem_word = interconnect_load32(cpu->inter, aligned_addr);
    uint32_t modified_mem_word;

    // Shift and mask based on address alignment (Little Endian)
    switch (addr & 3) {
        case 0: modified_mem_word = (current_mem_word & 0xFFFFFF00) | (value_to_store >> 24); break;
        case 1: modified_mem_word = (current_mem_word & 0xFFFF0000) | (value_to_store >> 16); break;
        case 2: modified_mem_word = (current_mem_word & 0xFF000000) | (value_to_store >> 8);  break;
        case 3: modified_mem_word = (current_mem_word & 0x00000000) | (value_to_store >> 0);  break;
        default: modified_mem_word = current_mem_word; /* Should not happen */ break;
    }
    interconnect_store32(cpu->inter, aligned_addr, modified_mem_word);
}

// Store Word Right (Handles unaligned stores)
void op_swr(Cpu* cpu, uint32_t instruction) {
    if ((cpu->sr & 0x10000) != 0) {
return;
    }
    uint32_t offset = instr_imm_se(instruction);
    uint32_t rt = instr_t(instruction); // Register containing data to store
    uint32_t rs = instr_s(instruction); // Register containing base address
    uint32_t addr = cpu_reg(cpu, rs) + offset;
    uint32_t value_to_store = cpu_reg(cpu, rt); // Use input register set value

    uint32_t aligned_addr = addr & ~3;
    // Read-Modify-Write
    uint32_t current_mem_word = interconnect_load32(cpu->inter, aligned_addr);
    uint32_t modified_mem_word;

    // Shift and mask based on address alignment (Little Endian)
    switch (addr & 3) {
        case 0: modified_mem_word = (current_mem_word & 0x00000000) | (value_to_store << 0);  break;
        case 1: modified_mem_word = (current_mem_word & 0x000000FF) | (value_to_store << 8);  break;
        case 2: modified_mem_word = (current_mem_word & 0x0000FFFF) | (value_to_store << 16); break;
        case 3: modified_mem_word = (current_mem_word & 0x00FFFFFF) | (value_to_store << 24); break;
        default: modified_mem_word = current_mem_word; /* Should not happen */ break;
    }
    interconnect_store32(cpu->inter, aligned_addr, modified_mem_word);
}

// Load Word Coprocessor 0 - Not supported
void op_lwc0(Cpu* cpu, uint32_t instruction) {
    (void)instruction;
    cpu_exception(cpu, EXCEPTION_COPROCESSOR_ERROR);
}

// Load Word Coprocessor 1 (FPU) - Not supported
void op_lwc1(Cpu* cpu, uint32_t instruction) {
    (void)instruction;
    cpu_exception(cpu, EXCEPTION_COPROCESSOR_ERROR);
}

// Load Word Coprocessor 2 (GTE): [RS + imm_se] -> GTE data register RT
void op_lwc2(Cpu* cpu, uint32_t instruction) {
    uint32_t rt     = instr_t(instruction);           // destination GTE data register
    uint32_t rs     = instr_s(instruction);           // base address register
    uint32_t offset = instr_imm_se(instruction);
    uint32_t addr   = cpu_reg(cpu, rs) + offset;
    uint32_t value  = interconnect_load32(cpu->inter, addr);
    gte_write_data_register(&cpu->gte, rt, value);
}

// Load Word Coprocessor 3 - Not supported
void op_lwc3(Cpu* cpu, uint32_t instruction) {
    (void)instruction;
    cpu_exception(cpu, EXCEPTION_COPROCESSOR_ERROR);
}

// Store Word Coprocessor 0 - Not supported
void op_swc0(Cpu* cpu, uint32_t instruction) {
    (void)instruction;
    cpu_exception(cpu, EXCEPTION_COPROCESSOR_ERROR);
}

// Store Word Coprocessor 1 (FPU) - Not supported
void op_swc1(Cpu* cpu, uint32_t instruction) {
    (void)instruction;
    cpu_exception(cpu, EXCEPTION_COPROCESSOR_ERROR);
}

// Store Word Coprocessor 2 (GTE): GTE data register RT -> [RS + imm_se]
void op_swc2(Cpu* cpu, uint32_t instruction) {
    uint32_t rt     = instr_t(instruction);           // source GTE data register
    uint32_t rs     = instr_s(instruction);           // base address register
    uint32_t offset = instr_imm_se(instruction);
    uint32_t addr   = cpu_reg(cpu, rs) + offset;
    uint32_t value  = gte_read_data_register(&cpu->gte, rt);
    interconnect_store32(cpu->inter, addr, value);
}

// Store Word Coprocessor 3 - Not supported
void op_swc3(Cpu* cpu, uint32_t instruction) {
    (void)instruction;
    cpu_exception(cpu, EXCEPTION_COPROCESSOR_ERROR);
}

// Illegal/Unhandled Instruction Handler
void op_illegal(Cpu* cpu, uint32_t instruction) {
    LOG_CPU_ERROR("[CPU] Illegal instruction 0x%08x at 0x%08x", instruction, cpu->current_pc);
    cpu_exception(cpu, EXCEPTION_ILLEGAL_INSTRUCTION);
}