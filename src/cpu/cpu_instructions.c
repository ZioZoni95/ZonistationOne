/**
 * @file cpu_instructions.c
 * @brief MIPS R3000A instruction implementations
 * 
 * Contains all 60+ instruction handlers for the PlayStation CPU.
 * Each handler implements one MIPS instruction according to PSX-SPX specification.
 * 
 * Thread Safety:
 * - Each instruction operates on a single CPU instance
 * - Memory access through interconnect (thread-safe)
 * - GTE operations are synchronous per-CPU
 * 
 * Based on DuckStation architecture and PSX-SPX documentation.
 */

#include "cpu/cpu_instructions.h"
#include "cpu/cpu_core.h"
#include "cpu/cpu_cache.h"
#include "cpu/cpu_exceptions.h"
#include "cpu/cpu_types.h"
#include "bios/bios_core.h"  // For BIOS function call logging
#include "interconnect.h"
#include "gte.h"
#include "log.h"
#include "spu.h"
#include <stdlib.h>
#include <limits.h>
#include <stdbool.h>

// ============================================================================
// Instruction Dispatch Tables (O(1) Lookup)
// ============================================================================

// Function pointer type for instruction handlers
typedef void (*InstructionHandler)(Cpu* cpu, uint32_t instruction);

// Forward declarations for all instruction handlers
static void op_special_dispatch(Cpu* cpu, uint32_t instruction);

// Primary opcode dispatch table (64 entries, indexed by bits 26-31)
static InstructionHandler primary_table[64];

// Secondary opcode dispatch table for SPECIAL (64 entries, indexed by bits 0-5)
static InstructionHandler special_table[64];

// ============================================================================
// Instruction Decoder (Optimized O(1) Table Dispatch)
// ============================================================================

/**
 * @brief Initialize instruction dispatch tables
 * Called once at startup. After this, decoding is O(1) array lookup.
 */
void cpu_instructions_init(void) {
    static bool initialized = false;
    if (initialized) return;  // Only initialize once
    initialized = true;
    // Initialize all entries to illegal instruction handler
    for (int i = 0; i < 64; i++) {
        primary_table[i] = op_illegal;
        special_table[i] = op_illegal;
    }
    
    // === Primary Opcode Table (bits 26-31) ===
    
    // SPECIAL opcode (0x00) uses secondary dispatch
    primary_table[0x00] = op_special_dispatch;
    
    // REGIMM branches (0x01)
    primary_table[0x01] = op_bxx;
    
    // Jump instructions
    primary_table[0x02] = op_j;
    primary_table[0x03] = op_jal;
    
    // Branch instructions
    primary_table[0x04] = op_beq;
    primary_table[0x05] = op_bne;
    primary_table[0x06] = op_blez;
    primary_table[0x07] = op_bgtz;
    
    // Immediate arithmetic/logical
    primary_table[0x08] = op_addi;
    primary_table[0x09] = op_addiu;
    primary_table[0x0A] = op_slti;
    primary_table[0x0B] = op_sltiu;
    primary_table[0x0C] = op_andi;
    primary_table[0x0D] = op_ori;
    primary_table[0x0E] = op_xori;
    primary_table[0x0F] = op_lui;
    
    // Coprocessor operations
    primary_table[0x10] = op_cop0;
    primary_table[0x11] = op_cop1;
    primary_table[0x12] = op_cop2;
    primary_table[0x13] = op_cop3;
    
    // Load instructions
    primary_table[0x20] = op_lb;
    primary_table[0x21] = op_lh;
    primary_table[0x22] = op_lwl;
    primary_table[0x23] = op_lw;
    primary_table[0x24] = op_lbu;
    primary_table[0x25] = op_lhu;
    primary_table[0x26] = op_lwr;
    
    // Store instructions
    primary_table[0x28] = op_sb;
    primary_table[0x29] = op_sh;
    primary_table[0x2A] = op_swl;
    primary_table[0x2B] = op_sw;
    primary_table[0x2E] = op_swr;
    
    // Coprocessor load/store
    primary_table[0x30] = op_lwc0;
    primary_table[0x31] = op_lwc1;
    primary_table[0x32] = op_lwc2;
    primary_table[0x33] = op_lwc3;
    primary_table[0x38] = op_swc0;
    primary_table[0x39] = op_swc1;
    primary_table[0x3A] = op_swc2;
    primary_table[0x3B] = op_swc3;
    
    // === Secondary Opcode Table for SPECIAL (bits 0-5) ===
    
    // Shift instructions
    special_table[0x00] = op_sll;
    special_table[0x02] = op_srl;
    special_table[0x03] = op_sra;
    special_table[0x04] = op_sllv;
    special_table[0x06] = op_srlv;
    special_table[0x07] = op_srav;
    
    // Jump register
    special_table[0x08] = op_jr;
    special_table[0x09] = op_jalr;
    
    // System calls
    special_table[0x0C] = op_syscall;
    special_table[0x0D] = op_break;
    
    // Move from/to HI/LO
    special_table[0x10] = op_mfhi;
    special_table[0x11] = op_mthi;
    special_table[0x12] = op_mflo;
    special_table[0x13] = op_mtlo;
    
    // Multiply/Divide
    special_table[0x18] = op_mult;
    special_table[0x19] = op_multu;
    special_table[0x1A] = op_div;
    special_table[0x1B] = op_divu;
    
    // ALU operations
    special_table[0x20] = op_add;
    special_table[0x21] = op_addu;
    special_table[0x22] = op_sub;
    special_table[0x23] = op_subu;
    special_table[0x24] = op_and;
    special_table[0x25] = op_or;
    special_table[0x26] = op_xor;
    special_table[0x27] = op_nor;
    
    // Set comparisons
    special_table[0x2A] = op_slt;
    special_table[0x2B] = op_sltu;
}

/**
 * @brief Secondary dispatch for SPECIAL opcode instructions
 * Uses bits 0-5 (function field) for O(1) lookup
 */
static void op_special_dispatch(Cpu* cpu, uint32_t instruction) {
    uint32_t subfunc = instruction & 0x3F;  // bits 0-5
    special_table[subfunc](cpu, instruction);
}

/**
 * @brief Decode and execute instruction (O(1) complexity)
 * Uses pre-built dispatch tables for constant-time instruction lookup.
 * This is called millions of times per second - performance critical!
 */
void decode_and_execute(Cpu* cpu, uint32_t instruction) {
    // Extract primary opcode (bits 26-31)
    uint32_t opcode = (instruction >> 26) & 0x3F;
    
    // O(1) dispatch: direct array lookup + indirect function call
    primary_table[opcode](cpu, instruction);
}


// ============================================================================
// Instruction Handlers
// ============================================================================

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
    if ((cpu->cop0.sr.isc) != 0) { // Check cache isolation bit
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
        cpu->cop0.badvaddr = address;
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
    // Combine upper 4 bits of current PC+4 with target
    cpu->next_pc = (cpu->current_pc & 0xF0000000) | (target_imm << 2);
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
        case 3: // BPC
            cpu->cop0.bpc = value;
            LOG_CPU_DEBUG("COP0 BPC <- %08x", value);
            break;
        case 5: // BDA
            cpu->cop0.bda = value;
            LOG_CPU_DEBUG("COP0 BDA <- %08x", value);
            break;
        case 6: // TAR/JUMPDEST
            cpu->cop0.tar = value;
            LOG_CPU_DEBUG("COP0 TAR <- %08x", value);
            break;
        case 7: // Cache Control (DuckStation-style)
            cpu->cop0.sr.isc = value;
            // Bit 1 = Invalidate mode: if set, invalidate all cache
            if (value & (1 << 1)) {
                cpu_icache_clear(cpu);
            }
            break;
        case 9: // BDAM
            cpu->cop0.bdam = value;
            LOG_CPU_DEBUG("COP0 BDAM <- %08x", value);
            break;
        case 11: // BPCM
            cpu->cop0.bpcm = value;
            LOG_CPU_DEBUG("COP0 BPCM <- %08x", value);
            break;
        case 12: // SR (Status Register)
            LOG_CPU_DEBUG("MTC0 write to SR: 0x%08x (PC=0x%08x)", value, cpu->current_pc);
            cpu->cop0.sr.bits = value;
            // Check for pending interrupts immediately after SR update
            cpu_dispatch_interrupt(cpu);
            break;
        case 13: // CAUSE
             // Only bits 8 and 9 (IP0, IP1) seem writable to force software interrupts.
             // Mask other bits.
             cpu->cop0.cause.bits = (cpu->cop0.cause.bits & ~0x300) | (value & 0x300);
             if ((value & ~0x300) != 0) {
                 LOG_CPU_WARN("MTC0 to CAUSE attempting to write non-SW bits: 0x%08x at PC=0x%08x", value, cpu->current_pc);
             }
             // Check for pending interrupts immediately after Cause update
             cpu_dispatch_interrupt(cpu);
             break;
        default:
            LOG_CPU_WARN("MTC0 to unhandled/read-only COP0 Register %u = 0x%08x at PC=0x%08x", cop_r, value, cpu->current_pc);
            break;
    }
}

void op_rfe(Cpu* cpu, uint32_t instruction) {
    (void)instruction;
    // RFE: Return from Exception
    // Pop mode stack: current <= previous, previous <= old, old <= old (duplicate top)
    // Based on DuckStation/MIPS behavior
    uint32_t old_sr = cpu->cop0.sr.bits;
    uint32_t new_sr = old_sr;
    
    // Mask out bits 0-3 (KUc, IEc, KUp, IEp)
    // New 0-3 come from old 2-5 (KUp, IEp, KUo, IEo)
    new_sr = (old_sr & ~0xF) | ((old_sr >> 2) & 0xF);
    
    cpu->cop0.sr.bits = new_sr;
    LOG_CPU_DEBUG("RFE: SR changed from 0x%08x to 0x%08x", old_sr, new_sr);
    
    // Check for pending interrupts immediately after SR update
    cpu_dispatch_interrupt(cpu);
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
    if ((cpu->cop0.sr.isc) != 0) { // Check cache isolation
        LOG_DEBUG("~ LW Ignored (Cache Isolated, SR=0x%08x)\n", cpu->cop0.sr.bits); // Keep debug print
        return;
    }
    uint32_t offset = instr_imm_se(instruction);
    uint32_t rt = instr_t(instruction);
    uint32_t rs = instr_s(instruction);
    uint32_t address = cpu_reg(cpu, rs) + offset;

    // Enforce word alignment per PSX spec: word accesses must be 4-byte aligned.
    if ((address & 3) != 0) {
        LOG_ERROR("LW Address Error: Unaligned address 0x%08x (PC=0x%08x) [base_reg=r%u=0x%08x, offset=0x%08x]\n", 
                  address, cpu->current_pc, rs, cpu_reg(cpu, rs), offset);
        cpu->cop0.badvaddr = address;
        cpu_exception(cpu, EXCEPTION_LOAD_ADDRESS_ERROR);
        return;
    }

    // Perform load and schedule it for the delay slot
    uint32_t value_loaded = interconnect_load32(cpu->inter, address);
    
    // Schedule load delay
    cpu_set_reg_delayed(cpu, rt, value_loaded);
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
    if ((cpu->cop0.sr.isc) != 0) {
        LOG_DEBUG("~ SH Ignored (Cache Isolated, SR=0x%08x)\n", cpu->cop0.sr.bits); // Keep debug print
        return;
    }
    uint32_t offset = instr_imm_se(instruction);
    uint32_t rt = instr_t(instruction);
    uint32_t rs = instr_s(instruction);
    uint32_t address = cpu_reg(cpu, rs) + offset;
    // Enforce halfword alignment for SH
    if ((address & 1) != 0) {
        LOG_ERROR("SH Address Error: Unaligned address 0x%08x (PC=0x%08x)\n", address, cpu->current_pc);
        cpu->cop0.badvaddr = address;
        cpu_exception(cpu, EXCEPTION_STORE_ADDRESS_ERROR);
        return;
    }
    uint16_t value = (uint16_t)cpu_reg(cpu, rt); // Lower 16 bits of rt
    interconnect_store16(cpu->inter, address, value);
}

void op_jal(Cpu* cpu, uint32_t instruction) {
    cpu_set_reg(cpu, REG_RA, cpu->pc + 4); // Link Register $31 gets PC+8 (address after delay slot)
    uint32_t target_imm = instr_imm_jump(instruction);
    cpu->next_pc = (cpu->current_pc & 0xF0000000) | (target_imm << 2); // Same target calculation as J
    cpu->branch_taken = true;
}

void op_andi(Cpu* cpu, uint32_t instruction) {
    uint32_t imm = instr_imm(instruction); // Zero-extended immediate
    uint32_t rt = instr_t(instruction);
    uint32_t rs = instr_s(instruction);
    cpu_set_reg(cpu, rt, cpu_reg(cpu, rs) & imm);
}

void op_sb(Cpu* cpu, uint32_t instruction) {
    if ((cpu->cop0.sr.isc) != 0) {
        LOG_DEBUG("~ SB Ignored (Cache Isolated, SR=0x%08x)\n", cpu->cop0.sr.bits); // Keep debug print
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
    
    // Check alignment like DuckStation
    if ((target_address & 0x3) != 0) {
        cpu->cop0.badvaddr = target_address;
        cpu_exception(cpu, EXCEPTION_LOAD_ADDRESS_ERROR);
        return;
    }
    
    // Detect BIOS function vector calls
    if (target_address == 0x000000A0 || target_address == 0x000000B0 || target_address == 0x000000C0) {
        // Prefer a small function index in R9; if R9 looks like an address, fall back to R10.
        uint32_t func_num = cpu_reg(cpu, 9);
        if (func_num >= 0x100 || func_num == 0) {
            uint32_t alt = cpu_reg(cpu, 10);
            if (alt < 0x100 && alt != 0) func_num = alt;
            else func_num = cpu_reg(cpu, 9); // keep original if no small alt found
        }
        
        uint8_t table = (uint8_t)(target_address & 0xFF);
        uint8_t function = (uint8_t)(func_num & 0xFF);
        uint32_t ra = cpu_reg(cpu, 31);  // Return address
        
        // Use BIOS module for logging instead of CPU module
        bios_log_function_call(cpu->current_pc, table, function, ra);
    }
    
    // Dedicated log for suspicious infinite loop at 0x00001010
    if (target_address == 0x00001010 && cpu->current_pc == 0x00001010 && rs == 26) {
        // Suppressed full CPU state dump for 0x1010 loop to avoid excessive logs
        LOG_CPU_DEBUG("[0x1010_LOOP] JR $26 to 0x00001010 detected (full dump suppressed)");
    }
    cpu->next_pc = target_address;
    cpu->branch_taken = true;
}

void op_lb(Cpu* cpu, uint32_t instruction) {
    if ((cpu->cop0.sr.isc) != 0) {
        LOG_DEBUG("~ LB Ignored (Cache Isolated, SR=0x%08x)\n", cpu->cop0.sr.bits); // Keep debug print
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
    cpu_set_reg_delayed(cpu, rt, value_sign_extended);
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
        case 3:  value_read = cpu->cop0.bpc; break;
        case 5:  value_read = cpu->cop0.bda; break;
        case 6:  value_read = cpu->cop0.tar; break;
        case 7:  value_read = cpu->cop0.sr.isc; break; // Cache Control mirroring
        case 8:  value_read = cpu->cop0.badvaddr; break;
        case 9:  value_read = cpu->cop0.bdam; break;
        case 11: value_read = cpu->cop0.bpcm; break;
        case 12: value_read = cpu->cop0.sr.bits; break;
        case 13: value_read = cpu->cop0.cause.bits; break;
        case 14: value_read = cpu->cop0.epc; break;
        case 15: value_read = cpu->cop0.prid; break;
        default:
            LOG_WARN("Warning: MFC0 read from unhandled COP0 Register %u (PC=0x%08x)\n", cop_r_src, cpu->current_pc);
            break;
    }
    // Schedule load for delay slot
    cpu_set_reg_delayed(cpu, cpu_r_dest, value_read);
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
     if ((cpu->cop0.sr.isc) != 0) {
        LOG_DEBUG("~ LBU Ignored (Cache Isolated, SR=0x%08x)\n", cpu->cop0.sr.bits); // Keep debug print
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
    cpu_set_reg_delayed(cpu, rt, value_zero_extended);
}

void op_jalr(Cpu* cpu, uint32_t instruction) {
    uint32_t rs = instr_s(instruction); // Register containing target address
    uint32_t rd = instr_d(instruction); // Register to store return address (defaults to $ra=31 if rd=0?)
    uint32_t target_address = cpu_reg(cpu, rs);
    uint32_t return_addr = cpu->pc + 4; // Address of instruction after delay slot

    // Check for BIOS function call (to 0xA0, 0xB0, or 0xC0)
    if (target_address == 0xA0 || target_address == 0xB0 || target_address == 0xC0) {
        // Prefer a small function index in R9; if R9 looks like an address, fall back to R10.
        uint32_t func_num = cpu_reg(cpu, 9);
        if (func_num >= 0x100 || func_num == 0) {
            uint32_t alt = cpu_reg(cpu, 10);
            if (alt < 0x100 && alt != 0) func_num = alt;
            else func_num = cpu_reg(cpu, 9);
        }
        
        uint8_t table = (uint8_t)(target_address & 0xFF);
        uint8_t function = (uint8_t)(func_num & 0xFF);
        
        // Use BIOS module for logging instead of CPU module
        bios_log_function_call(cpu->current_pc, table, function, return_addr);
    }
    // Log other suspicious jumps to low memory or unaligned addresses
    else if (target_address < 0x00010000 || (target_address & 0x3) != 0) {
        LOG_CPU_DEBUG("@SUSPICIOUS_JALR from PC=0x%08x: $%d=0x%08x -> jumping to 0x%08x, return to $%d=0x%08x", 
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

    if (condition_met) {
        // Link if necessary (store PC+8 in $ra)
        if (is_link) {
            cpu_set_reg(cpu, REG_RA, cpu->pc + 4); //
        }
        // Perform the branch
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
        cpu->regs.hi = (uint32_t)n;
        cpu->regs.lo = (n >= 0) ? 0xffffffff : 1;
    } else if ((uint32_t)n == 0x80000000 && d == -1) { // Overflow case: MinInt / -1
        cpu->regs.hi = 0;
        cpu->regs.lo = 0x80000000; // Result is MinInt
    } else { // Normal division
        cpu->regs.lo = (uint32_t)(n / d); // Quotient
        cpu->regs.hi = (uint32_t)(n % d); // Remainder
    }
    
    // DuckStation-style: Division takes 36 cycles (per PSX-SPX)
    cpu->muldiv_completion_tick = cpu->pending_ticks + 36;
}

// Unsigned division
void op_divu(Cpu* cpu, uint32_t instruction) {
    uint32_t rs = instr_s(instruction);
    uint32_t rt = instr_t(instruction);
    uint32_t n = cpu_reg(cpu, rs);
    uint32_t d = cpu_reg(cpu, rt);

    if (d == 0) { // Division by zero
        cpu->regs.hi = n;          // Remainder is numerator
        cpu->regs.lo = 0xffffffff; // Quotient is -1
    } else { // Normal division
        cpu->regs.lo = n / d; // Quotient
        cpu->regs.hi = n % d; // Remainder
    }
    
    // DuckStation-style: Unsigned division takes 36 cycles
    cpu->muldiv_completion_tick = cpu->pending_ticks + 36;
}

// Move From LO
void op_mflo(Cpu* cpu, uint32_t instruction) {
    uint32_t rd = instr_d(instruction);
    
    // DuckStation-style: Stall if MULDIV not yet complete
    if (cpu->pending_ticks < cpu->muldiv_completion_tick) {
        cpu->pending_ticks = cpu->muldiv_completion_tick;
    }
    
    cpu_set_reg(cpu, rd, cpu->regs.lo);
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
    
    // DuckStation-style: Stall if MULDIV not yet complete
    if (cpu->pending_ticks < cpu->muldiv_completion_tick) {
        cpu->pending_ticks = cpu->muldiv_completion_tick;
    }
    
    cpu_set_reg(cpu, rd, cpu->regs.hi);
}

// System Call
void op_syscall(Cpu* cpu, uint32_t instruction) {
    (void)instruction;
    // Get the syscall number from register $a0
    uint32_t syscall_num = cpu_reg(cpu, 4); 

    // Attempt to handle it directly
    bool was_handled = handle_bios_syscall(cpu, syscall_num);

    // If the handler returned false, it means we don't have this
    // syscall implemented yet. In that case, trigger a full exception
    // so we can see it in the logs and debug it.
    if (!was_handled) {
        LOG_ERROR("Unhandled BIOS Syscall: 0x%02x, triggering full exception.\n", syscall_num);
        cpu_exception(cpu, EXCEPTION_SYSCALL);
        return; // <--- Ensure we do not continue executing after exception
    }
    // If it was handled, we do nothing and simply proceed to the next instruction.
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
    
    // Stall if MULDIV not yet complete
    if (cpu->pending_ticks < cpu->muldiv_completion_tick) {
        cpu->pending_ticks = cpu->muldiv_completion_tick;
    }
    
    cpu->regs.lo = cpu_reg(cpu, rs);
}

// Move To HI
void op_mthi(Cpu* cpu, uint32_t instruction) {
    uint32_t rs = instr_s(instruction);
    
    // Stall if MULDIV not yet complete
    if (cpu->pending_ticks < cpu->muldiv_completion_tick) {
        cpu->pending_ticks = cpu->muldiv_completion_tick;
    }
    
    cpu->regs.hi = cpu_reg(cpu, rs);
}

// Load Halfword Unsigned
void op_lhu(Cpu* cpu, uint32_t instruction) {
     if ((cpu->cop0.sr.isc) != 0) {
        LOG_DEBUG("~ LHU Ignored (Cache Isolated, SR=0x%08x)\n", cpu->cop0.sr.bits); // Keep debug print
        return;
    }
    uint32_t offset = instr_imm_se(instruction);
    uint32_t rt = instr_t(instruction);
    uint32_t rs = instr_s(instruction);
    uint32_t address = cpu_reg(cpu, rs) + offset;
    // Enforce halfword alignment: addresses must be 2-byte aligned
    if ((address & 1) != 0) {
        LOG_ERROR("LHU Address Error: Unaligned address 0x%08x (PC=0x%08x)\n", address, cpu->current_pc);
        cpu->cop0.badvaddr = address;
        cpu_exception(cpu, EXCEPTION_LOAD_ADDRESS_ERROR);
        return;
    }
    uint16_t value_loaded = interconnect_load16(cpu->inter, address);
    // Zero-extend the 16-bit value
    uint32_t value_zero_extended = (uint32_t)value_loaded;
    
    // Schedule load for delay slot
    cpu_set_reg_delayed(cpu, rt, value_zero_extended);
}

// Load Halfword (Signed)
void op_lh(Cpu* cpu, uint32_t instruction) {
    if ((cpu->cop0.sr.isc) != 0) {
        LOG_DEBUG("~ LH Ignored (Cache Isolated, SR=0x%08x)\n", cpu->cop0.sr.bits); // Keep debug print
        return;
    }
    uint32_t offset = instr_imm_se(instruction);
    uint32_t rt = instr_t(instruction);
    uint32_t rs = instr_s(instruction);
    uint32_t address = cpu_reg(cpu, rs) + offset;
    // Enforce halfword alignment: addresses must be 2-byte aligned
    if ((address & 1) != 0) {
        LOG_ERROR("LH Address Error: Unaligned address 0x%08x (PC=0x%08x)\n", address, cpu->current_pc);
        cpu->cop0.badvaddr = address;
        cpu_exception(cpu, EXCEPTION_LOAD_ADDRESS_ERROR);
        return;
    }
    uint16_t value_loaded = interconnect_load16(cpu->inter, address);
    // Sign-extend the 16-bit value
    uint32_t value_sign_extended = (uint32_t)(int32_t)(int16_t)value_loaded;
    // Schedule load for delay slot
    cpu_set_reg_delayed(cpu, rt, value_sign_extended);
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
    cpu->regs.hi = (uint32_t)(result_64 >> 32);
    cpu->regs.lo = (uint32_t)(result_64 & 0xFFFFFFFF);
    
    // DuckStation-style: Multiply takes 7 cycles (per PSX-SPX)
    cpu->muldiv_completion_tick = cpu->pending_ticks + 7;
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
    LOG_CPU_INFO("@nocash BREAK OPCODE: EPC=0x%08x PC=0x%08x");
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
    cpu->regs.hi = (uint32_t)((uint64_t)result_s64 >> 32);
    cpu->regs.lo = (uint32_t)((uint64_t)result_s64 & 0xFFFFFFFF);
    
    // DuckStation-style: Signed multiply takes 7 cycles
    cpu->muldiv_completion_tick = cpu->pending_ticks + 7;
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

// Coprocessor 2 (GTE) Opcode
void op_cop2(Cpu* cpu, uint32_t instruction) {
    if (instruction & (1 << 25)) {
        // GTE Command (bit 25 = 1)
        // Stall if GTE not yet complete (DuckStation-style)
        if (cpu->pending_ticks < cpu->gte_completion_tick) {
            cpu->pending_ticks = cpu->gte_completion_tick;
        }
        
        uint32_t cycles = gte_execute_instruction(&cpu->gte, instruction);
        cpu->gte_completion_tick = cpu->pending_ticks + cycles;
        LOG_TRACE("GTE: Executed command 0x%08x (cycles=%u, PC=0x%08x)\n", instruction, cycles, cpu->current_pc);
    } else {
        // Move instructions (bit 25 = 0)
        uint32_t cop_opcode = (instruction >> 21) & 0x1F;
        uint32_t rt = instr_t(instruction);
        uint32_t rd = instr_d(instruction);

        switch (cop_opcode) {
            case 0x00: // MFC2 (Move From Coprocessor 2)
            {
                // Stall until GTE complete
                if (cpu->pending_ticks < cpu->gte_completion_tick) {
                    cpu->pending_ticks = cpu->gte_completion_tick;
                }
                uint32_t value = gte_read_data_register(&cpu->gte, rd);
                cpu_set_reg_delayed(cpu, rt, value);
                break;
            }
            case 0x02: // CFC2 (Copy From Coprocessor 2 Control)
            {
                // Stall until GTE complete
                if (cpu->pending_ticks < cpu->gte_completion_tick) {
                    cpu->pending_ticks = cpu->gte_completion_tick;
                }
                uint32_t value = gte_read_control_register(&cpu->gte, rd);
                cpu_set_reg_delayed(cpu, rt, value);
                break;
            }
            case 0x04: // MTC2 (Move To Coprocessor 2)
            {
                uint32_t value = cpu_reg(cpu, rt);
                gte_write_data_register(&cpu->gte, rd, value);
                break;
            }
            case 0x06: // CTC2 (Copy To Coprocessor 2 Control)
            {
                uint32_t value = cpu_reg(cpu, rt);
                gte_write_control_register(&cpu->gte, rd, value);
                break;
            }
            default:
                LOG_WARN("Unhandled COP2 sub-opcode: 0x%02x at PC=0x%08x\n", cop_opcode, cpu->current_pc);
                cpu_exception(cpu, EXCEPTION_ILLEGAL_INSTRUCTION);
                break;
        }
    }
}

// Coprocessor 3 Opcode - Triggers exception
void op_cop3(Cpu* cpu, uint32_t instruction) {
    LOG_WARN("Warning: Unsupported COP3 instruction: 0x%08x (PC=0x%08x)\n", instruction, cpu->current_pc);
    cpu_exception(cpu, EXCEPTION_COPROCESSOR_ERROR); //
}

// Load Word Left (Handles unaligned loads)
void op_lwl(Cpu* cpu, uint32_t instruction) {
    if ((cpu->cop0.sr.isc) != 0) {
        LOG_DEBUG("~ LWL Ignored (Cache Isolated, SR=0x%08x)\n", cpu->cop0.sr.bits); return;
    }
    uint32_t offset = instr_imm_se(instruction);
    uint32_t rt = instr_t(instruction);
    uint32_t rs = instr_s(instruction);
    uint32_t addr = cpu_reg(cpu, rs) + offset;

    // Merge with pending load value if target register matches
    uint32_t current_rt_value = (cpu->load_reg_idx == rt) ? cpu->load_value : cpu->regs.r[rt];

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
    cpu_set_reg_delayed(cpu, rt, merged_value);
}

// Load Word Right (Handles unaligned loads)
void op_lwr(Cpu* cpu, uint32_t instruction) {
     if ((cpu->cop0.sr.isc) != 0) {
        LOG_DEBUG("~ LWR Ignored (Cache Isolated, SR=0x%08x)\n", cpu->cop0.sr.bits); return;
    }
    uint32_t offset = instr_imm_se(instruction);
    uint32_t rt = instr_t(instruction);
    uint32_t rs = instr_s(instruction);
    uint32_t addr = cpu_reg(cpu, rs) + offset;

    // Merge with pending load value if target register matches
    uint32_t current_rt_value = (cpu->load_reg_idx == rt) ? cpu->load_value : cpu->regs.r[rt];

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
    cpu_set_reg_delayed(cpu, rt, merged_value);
}

// Store Word Left (Handles unaligned stores)
void op_swl(Cpu* cpu, uint32_t instruction) {
     if ((cpu->cop0.sr.isc) != 0) {
        LOG_DEBUG("~ SWL Ignored (Cache Isolated, SR=0x%08x)\n", cpu->cop0.sr.bits); return;
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
    if ((cpu->cop0.sr.isc) != 0) {
        LOG_DEBUG("~ SWR Ignored (Cache Isolated, SR=0x%08x)\n", cpu->cop0.sr.bits); return;
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

// Load Word Coprocessor 2 (GTE)
void op_lwc2(Cpu* cpu, uint32_t instruction) {
    uint32_t rt = instr_t(instruction); // GTE register index
    uint32_t rs = instr_s(instruction);
    uint32_t offset = instr_imm_se(instruction);
    uint32_t address = cpu_reg(cpu, rs) + offset;
    
    // LWC2 is word access, must be 4-byte aligned
    if ((address & 3) != 0) {
        LOG_ERROR("LWC2 Address Error: Unaligned address 0x%08x (PC=0x%08x)\n", address, cpu->current_pc);
        cpu_exception_with_badvaddr(cpu, EXCEPTION_LOAD_ADDRESS_ERROR, address);
        return;
    }
    
    uint32_t value = interconnect_load32(cpu->inter, address);
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

// Store Word Coprocessor 2 (GTE)
void op_swc2(Cpu* cpu, uint32_t instruction) {
    // Stall until GTE complete (DuckStation behavior)
    if (cpu->pending_ticks < cpu->gte_completion_tick) {
        cpu->pending_ticks = cpu->gte_completion_tick;
    }

    uint32_t rt = instr_t(instruction); // GTE register index
    uint32_t rs = instr_s(instruction);
    uint32_t offset = instr_imm_se(instruction);
    uint32_t address = cpu_reg(cpu, rs) + offset;
    
    // SWC2 is word access, must be 4-byte aligned
    if ((address & 3) != 0) {
        LOG_ERROR("SWC2 Address Error: Unaligned address 0x%08x (PC=0x%08x)\n", address, cpu->current_pc);
        cpu->cop0.badvaddr = address;
        cpu_exception(cpu, EXCEPTION_STORE_ADDRESS_ERROR);
        return;
    }
    
    uint32_t value = gte_read_data_register(&cpu->gte, rt);
    interconnect_store32(cpu->inter, address, value);
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

