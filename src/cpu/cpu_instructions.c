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
    if ((cpu->sr & 0x10000) != 0) { // Check cache isolation bit
        // Rate-limit cache isolation logs
        static uint32_t cache_iso_count = 0;
        if (++cache_iso_count % 1000 == 0) {
            LOG_TRACE("~ SW Ignored (Cache Isolated) #%u", cache_iso_count);
        }
        return;
    }
    uint32_t offset = instr_imm_se(instruction);
    uint32_t rt = instr_t(instruction);
    uint32_t rs = instr_s(instruction);
    uint32_t address = cpu_reg(cpu, rs) + offset;
    uint32_t value = cpu_reg(cpu, rt); // Use input register set
    // Enforce word alignment for SW
    if ((address & 3) != 0) {
        LOG_ERROR("SW Address Error: Unaligned address 0x%08x = 0x%08x (PC=0x%08x)\n", address, value, cpu->current_pc);
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

void op_cop0(Cpu* cpu, uint32_t instruction) {
    uint32_t cop_opcode = instr_cop_opcode(instruction); // Bits 25:21 specify COP0 op
    switch (cop_opcode) {
        case 0b00000: op_mfc0(cpu, instruction); break; // MFC0
        case 0b00100: op_mtc0(cpu, instruction); break; // MTC0
        case 0b10000: // Check subfunction for RFE
            if ((instruction & 0x3f) == 0b010000) {
                op_rfe(cpu, instruction); // RFE
            } else {
                op_illegal(cpu, instruction); // Other TLB/etc. instructions
            }
            break;
        default:
             LOG_WARN("Warning: Unhandled COP0 instruction: 0x%08x (CopOp=%u) at PC=0x%08x\n", instruction, cop_opcode, cpu->current_pc);
             cpu_exception(cpu, EXCEPTION_ILLEGAL_INSTRUCTION); // Or maybe COPROCESSOR_ERROR? Illegal seems better.
            break;
    }
}

void op_mtc0(Cpu* cpu, uint32_t instruction) {
    uint32_t cpu_r = instr_t(instruction); // Source CPU register
    uint32_t cop_r = instr_d(instruction); // Destination COP0 register
    uint32_t value = cpu_reg(cpu, cpu_r);

    switch (cop_r) {
        case 3: case 5: case 6: case 7: case 9: case 11: // Breakpoint/DCIC regs
             if (value != 0) LOG_CPU_WARN("MTC0 to unhandled Breakpoint/DCIC Reg %u = 0x%08x at PC=0x%08x", cop_r, value, cpu->current_pc);
             // No state change for now
             break;
        case 12: // SR (Status Register)
            LOG_CPU_DEBUG("MTC0 write to SR: 0x%08x (PC=0x%08x)", value, cpu->current_pc);
            // Apply hardware write mask (DuckStation SR::WRITE_MASK = 0xF27FFFFF).
            cpu->sr = (cpu->sr & ~0xF27FFFFF) | (value & 0xF27FFFFF);
            break;
        case 13: // CAUSE
             // Only bits 8 and 9 (IP0, IP1) seem writable to force software interrupts.
             // Mask other bits.
             cpu->cause = (cpu->cause & ~0x300) | (value & 0x300);
             if ((value & ~0x300) != 0) {
                 LOG_CPU_WARN("MTC0 to CAUSE attempting to write non-SW bits: 0x%08x at PC=0x%08x", value, cpu->current_pc);
             }
             break;
        case 8:  // BadVaddr (COP0 reg 8) — read-only, writes silently ignored (DuckStation)
        case 14: // EPC (COP0 reg 14) — read-only, writes silently ignored (DuckStation)
            LOG_CPU_WARN("MTC0 to read-only COP0 Register %u = 0x%08x at PC=0x%08x (ignored)", cop_r, value, cpu->current_pc);
            break;
        default:
            LOG_CPU_WARN("MTC0 to unhandled/read-only COP0 Register %u = 0x%08x at PC=0x%08x", cop_r, value, cpu->current_pc);
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
    LOG_CPU_DEBUG("RFE: SR changed from 0x%08x to 0x%08x", old_sr, cpu->sr);
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
        LOG_ERROR("ADDI Signed Overflow: %d + %d (PC=0x%08x)\n", rs_value, imm_se, cpu->current_pc);
        cpu_exception(cpu, EXCEPTION_OVERFLOW); // Trigger overflow exception
    } else {
        cpu_set_reg(cpu, rt, (uint32_t)result);
    }
}

void op_lw(Cpu* cpu, uint32_t instruction) {
    if ((cpu->sr & 0x10000) != 0) { // Check cache isolation
        LOG_DEBUG("~ LW Ignored (Cache Isolated, SR=0x%08x)\n", cpu->sr); // Keep debug print
        return;
    }
    uint32_t offset = instr_imm_se(instruction);
    uint32_t rt = instr_t(instruction);
    uint32_t rs = instr_s(instruction);
    uint32_t address = cpu_reg(cpu, rs) + offset;

    // Enforce word alignment per PSX spec: word accesses must be 4-byte aligned.
    if ((address & 3) != 0) {
        LOG_ERROR("LW Address Error: Unaligned address 0x%08x (PC=0x%08x)\n", address, cpu->current_pc);
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
        LOG_DEBUG("~ SH Ignored (Cache Isolated, SR=0x%08x)\n", cpu->sr); // Keep debug print
        return;
    }
    uint32_t offset = instr_imm_se(instruction);
    uint32_t rt = instr_t(instruction);
    uint32_t rs = instr_s(instruction);
    uint32_t address = cpu_reg(cpu, rs) + offset;
    // Enforce halfword alignment for SH
    if ((address & 1) != 0) {
        LOG_ERROR("SH Address Error: Unaligned address 0x%08x (PC=0x%08x)\n", address, cpu->current_pc);
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
        LOG_DEBUG("~ SB Ignored (Cache Isolated, SR=0x%08x)\n", cpu->sr); // Keep debug print
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
        const char* vector_name = (target_address == 0xA0) ? "A" :
                                  (target_address == 0xB0) ? "B" : "C";
        LOG_CPU_DEBUG("@BIOS_CALL from PC=0x%08x: %s(%02Xh)",
                      cpu->current_pc, vector_name, func_num);

        if (target_address == 0x000000A0)
            handle_a0_syscall(cpu);
        else if (target_address == 0x000000B0)
            handle_b0_syscall(cpu);
        // LLE: BIOS native code will execute normally
    }
    // Log truly suspicious jumps: only unaligned targets (BIOS routinely jumps to low RAM)
    else if ((target_address & 0x3) != 0) {
        LOG_CPU_DEBUG("@SUSPICIOUS_JR from PC=0x%08x: $%d=0x%08x -> unaligned target 0x%08x",
                         cpu->current_pc, rs, target_address, target_address);
    }
    
    // Dedicated log for suspicious infinite loop at 0x00001010
    if (target_address == 0x00001010 && cpu->current_pc == 0x00001010 && rs == 26) {
        // Suppressed full CPU state dump for 0x1010 loop to avoid excessive logs
        LOG_CPU_DEBUG("[0x1010_LOOP] JR $26 to 0x00001010 detected (full dump suppressed)");
    }
    cpu->next_pc = target_address;
    cpu->branch_taken = true;
    // Alignment check will happen on fetch in the next cycle
}

void op_lb(Cpu* cpu, uint32_t instruction) {
    if ((cpu->sr & 0x10000) != 0) {
        LOG_DEBUG("~ LB Ignored (Cache Isolated, SR=0x%08x)\n", cpu->sr); // Keep debug print
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
            LOG_CPU_WARN("MFC0 read from unhandled COP0 Register %u (PC=0x%08x)", cop_r_src, cpu->current_pc);
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
        LOG_ERROR("ADD Signed Overflow: %d + %d (PC=0x%08x)\n", rs_value, rt_value, cpu->current_pc);
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
        LOG_DEBUG("~ LBU Ignored (Cache Isolated, SR=0x%08x)\n", cpu->sr); // Keep debug print
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

    // Check for BIOS function call (to 0xA0, 0xB0, or 0xC0) — LLE side-channel capture
    if (target_address == 0xA0 || target_address == 0xB0 || target_address == 0xC0) {
        uint32_t func_num = cpu_reg(cpu, 9);
        if (func_num >= 0x100 || func_num == 0) {
            uint32_t alt = cpu_reg(cpu, 10);
            if (alt < 0x100 && alt != 0) func_num = alt;
        }
        const char* vector_name = (target_address == 0xA0) ? "A" :
                                  (target_address == 0xB0) ? "B" : "C";
        LOG_CPU_DEBUG("@BIOS_CALL from PC=0x%08x: %s(%02Xh)",
                     cpu->current_pc, vector_name, func_num);

        if (target_address == 0xA0)
            handle_a0_syscall(cpu);
        else if (target_address == 0xB0)
            handle_b0_syscall(cpu);
    }
    // Log truly suspicious jumps: only unaligned targets (BIOS routinely jumps to low RAM)
    else if ((target_address & 0x3) != 0) {
        LOG_CPU_DEBUG("@SUSPICIOUS_JALR from PC=0x%08x: $%d=0x%08x -> unaligned target 0x%08x, return to $%d=0x%08x",
                         cpu->current_pc, rs, target_address, target_address, rd, return_addr);
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
    // Note: Division takes many cycles; result isn't available immediately.
    // We ignore timing for now. HI/LO access should stall if op not finished.
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
    // Ignore timing stall for now.
}

// Move From LO
void op_mflo(Cpu* cpu, uint32_t instruction) {
    uint32_t rd = instr_d(instruction);
    cpu_set_reg(cpu, rd, cpu->lo); //
    // TODO: Should stall if previous DIV/MULT not finished.
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
    cpu_set_reg(cpu, rd, cpu->hi); //
    // TODO: Should stall if previous DIV/MULT not finished.
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
        LOG_DEBUG("~ LHU Ignored (Cache Isolated, SR=0x%08x)\n", cpu->sr); // Keep debug print
        return;
    }
    uint32_t offset = instr_imm_se(instruction);
    uint32_t rt = instr_t(instruction);
    uint32_t rs = instr_s(instruction);
    uint32_t address = cpu_reg(cpu, rs) + offset;
    // Enforce halfword alignment: addresses must be 2-byte aligned
    if ((address & 1) != 0) {
        LOG_ERROR("LHU Address Error: Unaligned address 0x%08x (PC=0x%08x)\n", address, cpu->current_pc);
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
        LOG_DEBUG("~ LH Ignored (Cache Isolated, SR=0x%08x)\n", cpu->sr); // Keep debug print
        return;
    }
    uint32_t offset = instr_imm_se(instruction);
    uint32_t rt = instr_t(instruction);
    uint32_t rs = instr_s(instruction);
    uint32_t address = cpu_reg(cpu, rs) + offset;
    // Enforce halfword alignment: addresses must be 2-byte aligned
    if ((address & 1) != 0) {
        LOG_ERROR("LH Address Error: Unaligned address 0x%08x (PC=0x%08x)\n", address, cpu->current_pc);
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
    // Perform 64-bit multiplication
    uint64_t val_rs = (uint64_t)cpu_reg(cpu, rs);
    uint64_t val_rt = (uint64_t)cpu_reg(cpu, rt);
    uint64_t result_64 = val_rs * val_rt;
    // Store result in HI/LO
    cpu->hi = (uint32_t)(result_64 >> 32);  //
    cpu->lo = (uint32_t)(result_64 & 0xFFFFFFFF); //
    // Ignore timing stall for now
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
    LOG_CPU_INFO("@nocash BREAK OPCODE: EPC=0x%08x PC=0x%08x", cpu->epc, cpu->current_pc);
    cpu_exception(cpu, EXCEPTION_BREAK); //
}

// Multiply (Signed)
void op_mult(Cpu* cpu, uint32_t instruction) {
    uint32_t rs = instr_s(instruction);
    uint32_t rt = instr_t(instruction);
    // Perform 64-bit signed multiplication
    int64_t val_rs_s = (int64_t)(int32_t)cpu_reg(cpu, rs);
    int64_t val_rt_s = (int64_t)(int32_t)cpu_reg(cpu, rt);
    int64_t result_s64 = val_rs_s * val_rt_s;
    // Store result in HI/LO
    cpu->hi = (uint32_t)((uint64_t)result_s64 >> 32); //
    cpu->lo = (uint32_t)((uint64_t)result_s64 & 0xFFFFFFFF); //
    // Ignore timing stall
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
        LOG_ERROR("SUB Signed Overflow: %d - %d (PC=0x%08x)\n", rs_value, rt_value, cpu->current_pc);
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
    LOG_WARN("Warning: Unsupported COP1 (FPU) instruction: 0x%08x (PC=0x%08x)\n", instruction, cpu->current_pc);
    cpu_exception(cpu, EXCEPTION_COPROCESSOR_ERROR); //
}

// Coprocessor 2 (GTE) Opcode - Currently unimplemented
void op_cop2(Cpu* cpu, uint32_t instruction) {
    uint32_t cycles = gte_execute_instruction(&cpu->gte, instruction);
    (void)cycles;
    LOG_TRACE("GTE: Executing instruction 0x%08x (PC=0x%08x)\n", instruction, cpu->current_pc);
    
    // Execute the GTE instruction
    // TODO: Handle GTE busy state and timing if needed
    // For now, we'll just continue execution
}

// Coprocessor 3 Opcode - Triggers exception
void op_cop3(Cpu* cpu, uint32_t instruction) {
    LOG_WARN("Warning: Unsupported COP3 instruction: 0x%08x (PC=0x%08x)\n", instruction, cpu->current_pc);
    cpu_exception(cpu, EXCEPTION_COPROCESSOR_ERROR); //
}

// Load Word Left (Handles unaligned loads)
void op_lwl(Cpu* cpu, uint32_t instruction) {
    if ((cpu->sr & 0x10000) != 0) {
        LOG_DEBUG("~ LWL Ignored (Cache Isolated, SR=0x%08x)\n", cpu->sr); return;
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
        LOG_DEBUG("~ LWR Ignored (Cache Isolated, SR=0x%08x)\n", cpu->sr); return;
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
        LOG_DEBUG("~ SWL Ignored (Cache Isolated, SR=0x%08x)\n", cpu->sr); return;
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
        LOG_DEBUG("~ SWR Ignored (Cache Isolated, SR=0x%08x)\n", cpu->sr); return;
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
    LOG_WARN("Warning: Unsupported LWC0 instruction: 0x%08x (PC=0x%08x)\n", instruction, cpu->current_pc);
    cpu_exception(cpu, EXCEPTION_COPROCESSOR_ERROR); //
}

// Load Word Coprocessor 1 (FPU) - Not supported
void op_lwc1(Cpu* cpu, uint32_t instruction) {
    LOG_WARN("Warning: Unsupported LWC1 instruction: 0x%08x (PC=0x%08x)\n", instruction, cpu->current_pc);
    cpu_exception(cpu, EXCEPTION_COPROCESSOR_ERROR); //
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
    LOG_WARN("Warning: Unsupported LWC3 instruction: 0x%08x (PC=0x%08x)\n", instruction, cpu->current_pc);
    cpu_exception(cpu, EXCEPTION_COPROCESSOR_ERROR); //
}

// Store Word Coprocessor 0 - Not supported
void op_swc0(Cpu* cpu, uint32_t instruction) {
    LOG_WARN("Warning: Unsupported SWC0 instruction: 0x%08x (PC=0x%08x)\n", instruction, cpu->current_pc);
    cpu_exception(cpu, EXCEPTION_COPROCESSOR_ERROR); //
}

// Store Word Coprocessor 1 (FPU) - Not supported
void op_swc1(Cpu* cpu, uint32_t instruction) {
    LOG_WARN("Warning: Unsupported SWC1 instruction: 0x%08x (PC=0x%08x)\n", instruction, cpu->current_pc);
    cpu_exception(cpu, EXCEPTION_COPROCESSOR_ERROR); //
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
    LOG_WARN("Warning: Unsupported SWC3 instruction: 0x%08x (PC=0x%08x)\n", instruction, cpu->current_pc);
    cpu_exception(cpu, EXCEPTION_COPROCESSOR_ERROR); //
}

// Illegal/Unhandled Instruction Handler
void op_illegal(Cpu* cpu, uint32_t instruction) {
    LOG_CPU_INFO("@ILLEGAL_INSTRUCTION: 0x%08x at PC=0x%08x", instruction, cpu->current_pc);
    LOG_ERROR("Error: Illegal/Unhandled instruction 0x%08x encountered at PC=0x%08x\n", instruction, cpu->current_pc);
    
    // Read nearby instructions for context
    if (cpu->inter) {
        LOG_CPU_INFO("@CONTEXT: [PC-8]=0x%08x [PC-4]=0x%08x [PC]=0x%08x [PC+4]=0x%08x", 
                         interconnect_load32(cpu->inter, cpu->current_pc - 8),
                         interconnect_load32(cpu->inter, cpu->current_pc - 4),
                         instruction,
                         interconnect_load32(cpu->inter, cpu->current_pc + 4));
    }
    
    cpu_exception(cpu, EXCEPTION_ILLEGAL_INSTRUCTION);
}