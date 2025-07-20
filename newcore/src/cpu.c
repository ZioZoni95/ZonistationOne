#include "../include/cpu.h"
#include "../include/log.h"
#include <string.h>
#include <stdint.h>

// --- Register access helpers ---
static inline uint32_t cpu_reg(NcCpu* cpu, NcRegIndex idx) {
    return idx == NC_REG_ZERO ? 0 : cpu->regs[idx & 31];
}
static inline void cpu_set_reg(NcCpu* cpu, NcRegIndex idx, uint32_t value) {
    if (idx != NC_REG_ZERO) cpu->out_regs[idx & 31] = value;
}

// --- Exception handler (real logic) ---
void nc_cpu_exception(NcCpu* cpu, NcExceptionCause cause) {
    cpu->exception_pending = true;
    // Save EPC: if in delay slot, EPC = address of branch, else current_pc
    cpu->epc = cpu->current_pc;
    // Update Cause: set ExcCode (bits 6:2), preserve IP bits and BD
    uint32_t old_cause = cpu->cause;
    uint32_t ip_bits = old_cause & 0xFF00;
    uint32_t bd_bit = old_cause & (1u << 31);
    cpu->cause = ip_bits | ((uint32_t)cause << 2);
    if (cpu->in_delay_slot) cpu->cause |= (1u << 31);
    else cpu->cause &= ~(1u << 31);
    // Update SR: push mode bits onto stack
    uint32_t mode_stack = cpu->sr & 0x3F;
    cpu->sr &= ~0x3F;
    cpu->sr |= (mode_stack << 2) & 0x3F;
    // Jump to exception handler vector (BEV bit in SR)
    uint32_t handler_addr = (cpu->sr & (1 << 22)) ? 0xbfc00180 : 0x80000080;
    cpu->pc = handler_addr;
    cpu->next_pc = cpu->pc + 4;
    NC_LOGE("[CPU] Exception: cause=0x%02x EPC=0x%08x PC=0x%08x SR=0x%08x", cause, cpu->epc, cpu->current_pc, cpu->sr);
}

// --- Instruction handler stubs ---
#define HANDLER(name) static void name(NcCpu* cpu, uint32_t instr) { NC_LOGW("[CPU] Stub: %s", #name); }
// Only use HANDLER for unimplemented instructions:
HANDLER(nc_op_sll) HANDLER(nc_op_srl) HANDLER(nc_op_sra) HANDLER(nc_op_sllv) HANDLER(nc_op_srlv) HANDLER(nc_op_srav)
HANDLER(nc_op_mfhi) HANDLER(nc_op_mthi)
HANDLER(nc_op_mflo) HANDLER(nc_op_mtlo) HANDLER(nc_op_mult) HANDLER(nc_op_multu) HANDLER(nc_op_div) HANDLER(nc_op_divu)

// --- Arithmetic and logic instruction handlers ---
static void nc_op_add(NcCpu* cpu, uint32_t instr) {
    uint32_t rd = (instr >> 11) & 0x1F;
    uint32_t rs = (instr >> 21) & 0x1F;
    uint32_t rt = (instr >> 16) & 0x1F;
    int32_t a = (int32_t)cpu_reg(cpu, rs);
    int32_t b = (int32_t)cpu_reg(cpu, rt);
    int32_t res = a + b;
    // Check for signed overflow
    if (((a ^ res) & (b ^ res)) < 0) {
        nc_cpu_exception(cpu, NC_EXC_OVERFLOW);
        return;
    }
    cpu_set_reg(cpu, rd, (uint32_t)res);
}
static void nc_op_addu(NcCpu* cpu, uint32_t instr) {
    uint32_t rd = (instr >> 11) & 0x1F;
    uint32_t rs = (instr >> 21) & 0x1F;
    uint32_t rt = (instr >> 16) & 0x1F;
    cpu_set_reg(cpu, rd, cpu_reg(cpu, rs) + cpu_reg(cpu, rt));
}
static void nc_op_sub(NcCpu* cpu, uint32_t instr) {
    uint32_t rd = (instr >> 11) & 0x1F;
    uint32_t rs = (instr >> 21) & 0x1F;
    uint32_t rt = (instr >> 16) & 0x1F;
    int32_t a = (int32_t)cpu_reg(cpu, rs);
    int32_t b = (int32_t)cpu_reg(cpu, rt);
    int32_t res = a - b;
    // Check for signed overflow
    if (((a ^ b) & (a ^ res)) < 0) {
        nc_cpu_exception(cpu, NC_EXC_OVERFLOW);
        return;
    }
    cpu_set_reg(cpu, rd, (uint32_t)res);
}
static void nc_op_subu(NcCpu* cpu, uint32_t instr) {
    uint32_t rd = (instr >> 11) & 0x1F;
    uint32_t rs = (instr >> 21) & 0x1F;
    uint32_t rt = (instr >> 16) & 0x1F;
    cpu_set_reg(cpu, rd, cpu_reg(cpu, rs) - cpu_reg(cpu, rt));
}
static void nc_op_and(NcCpu* cpu, uint32_t instr) {
    uint32_t rd = (instr >> 11) & 0x1F;
    uint32_t rs = (instr >> 21) & 0x1F;
    uint32_t rt = (instr >> 16) & 0x1F;
    cpu_set_reg(cpu, rd, cpu_reg(cpu, rs) & cpu_reg(cpu, rt));
}
static void nc_op_or(NcCpu* cpu, uint32_t instr) {
    uint32_t rd = (instr >> 11) & 0x1F;
    uint32_t rs = (instr >> 21) & 0x1F;
    uint32_t rt = (instr >> 16) & 0x1F;
    cpu_set_reg(cpu, rd, cpu_reg(cpu, rs) | cpu_reg(cpu, rt));
}
static void nc_op_xor(NcCpu* cpu, uint32_t instr) {
    uint32_t rd = (instr >> 11) & 0x1F;
    uint32_t rs = (instr >> 21) & 0x1F;
    uint32_t rt = (instr >> 16) & 0x1F;
    cpu_set_reg(cpu, rd, cpu_reg(cpu, rs) ^ cpu_reg(cpu, rt));
}
static void nc_op_nor(NcCpu* cpu, uint32_t instr) {
    uint32_t rd = (instr >> 11) & 0x1F;
    uint32_t rs = (instr >> 21) & 0x1F;
    uint32_t rt = (instr >> 16) & 0x1F;
    cpu_set_reg(cpu, rd, ~(cpu_reg(cpu, rs) | cpu_reg(cpu, rt)));
}
static void nc_op_slt(NcCpu* cpu, uint32_t instr) {
    uint32_t rd = (instr >> 11) & 0x1F;
    uint32_t rs = (instr >> 21) & 0x1F;
    uint32_t rt = (instr >> 16) & 0x1F;
    cpu_set_reg(cpu, rd, ((int32_t)cpu_reg(cpu, rs) < (int32_t)cpu_reg(cpu, rt)) ? 1 : 0);
}
static void nc_op_sltu(NcCpu* cpu, uint32_t instr) {
    uint32_t rd = (instr >> 11) & 0x1F;
    uint32_t rs = (instr >> 21) & 0x1F;
    uint32_t rt = (instr >> 16) & 0x1F;
    cpu_set_reg(cpu, rd, (cpu_reg(cpu, rs) < cpu_reg(cpu, rt)) ? 1 : 0);
}

// --- Load and store instruction handlers ---
static void nc_op_lw(NcCpu* cpu, uint32_t instr) {
    uint32_t rt = (instr >> 16) & 0x1F;
    uint32_t rs = (instr >> 21) & 0x1F;
    int16_t imm = (int16_t)(instr & 0xFFFF);
    uint32_t addr = cpu_reg(cpu, rs) + imm;
    cpu_set_reg(cpu, rt, nc_interconnect_read32(cpu->inter, addr));
}
static void nc_op_sw(NcCpu* cpu, uint32_t instr) {
    uint32_t rt = (instr >> 16) & 0x1F;
    uint32_t rs = (instr >> 21) & 0x1F;
    int16_t imm = (int16_t)(instr & 0xFFFF);
    uint32_t addr = cpu_reg(cpu, rs) + imm;
    nc_interconnect_write32(cpu->inter, addr, cpu_reg(cpu, rt));
}
static void nc_op_lb(NcCpu* cpu, uint32_t instr) {
    uint32_t rt = (instr >> 16) & 0x1F;
    uint32_t rs = (instr >> 21) & 0x1F;
    int16_t imm = (int16_t)(instr & 0xFFFF);
    uint32_t addr = cpu_reg(cpu, rs) + imm;
    uint32_t word = nc_interconnect_read32(cpu->inter, addr & ~3);
    uint8_t val = (word >> (8 * (addr & 3))) & 0xFF;
    cpu_set_reg(cpu, rt, (int8_t)val);
}
static void nc_op_lbu(NcCpu* cpu, uint32_t instr) {
    uint32_t rt = (instr >> 16) & 0x1F;
    uint32_t rs = (instr >> 21) & 0x1F;
    int16_t imm = (int16_t)(instr & 0xFFFF);
    uint32_t addr = cpu_reg(cpu, rs) + imm;
    uint32_t word = nc_interconnect_read32(cpu->inter, addr & ~3);
    uint8_t val = (word >> (8 * (addr & 3))) & 0xFF;
    cpu_set_reg(cpu, rt, val);
}
static void nc_op_lh(NcCpu* cpu, uint32_t instr) {
    uint32_t rt = (instr >> 16) & 0x1F;
    uint32_t rs = (instr >> 21) & 0x1F;
    int16_t imm = (int16_t)(instr & 0xFFFF);
    uint32_t addr = cpu_reg(cpu, rs) + imm;
    uint32_t word = nc_interconnect_read32(cpu->inter, addr & ~3);
    uint16_t val = (word >> (8 * (addr & 2))) & 0xFFFF;
    cpu_set_reg(cpu, rt, (int16_t)val);
}
static void nc_op_lhu(NcCpu* cpu, uint32_t instr) {
    uint32_t rt = (instr >> 16) & 0x1F;
    uint32_t rs = (instr >> 21) & 0x1F;
    int16_t imm = (int16_t)(instr & 0xFFFF);
    uint32_t addr = cpu_reg(cpu, rs) + imm;
    uint32_t word = nc_interconnect_read32(cpu->inter, addr & ~3);
    uint16_t val = (word >> (8 * (addr & 2))) & 0xFFFF;
    cpu_set_reg(cpu, rt, val);
}
static void nc_op_sb(NcCpu* cpu, uint32_t instr) {
    uint32_t rt = (instr >> 16) & 0x1F;
    uint32_t rs = (instr >> 21) & 0x1F;
    int16_t imm = (int16_t)(instr & 0xFFFF);
    uint32_t addr = cpu_reg(cpu, rs) + imm;
    uint32_t word = nc_interconnect_read32(cpu->inter, addr & ~3);
    uint32_t shift = 8 * (addr & 3);
    word = (word & ~(0xFF << shift)) | ((cpu_reg(cpu, rt) & 0xFF) << shift);
    nc_interconnect_write32(cpu->inter, addr & ~3, word);
}
static void nc_op_sh(NcCpu* cpu, uint32_t instr) {
    uint32_t rt = (instr >> 16) & 0x1F;
    uint32_t rs = (instr >> 21) & 0x1F;
    int16_t imm = (int16_t)(instr & 0xFFFF);
    uint32_t addr = cpu_reg(cpu, rs) + imm;
    uint32_t word = nc_interconnect_read32(cpu->inter, addr & ~3);
    uint32_t shift = 8 * (addr & 2);
    word = (word & ~(0xFFFF << shift)) | ((cpu_reg(cpu, rt) & 0xFFFF) << shift);
    nc_interconnect_write32(cpu->inter, addr & ~3, word);
}

// --- Branch and jump instruction handlers ---
static void nc_op_beq(NcCpu* cpu, uint32_t instr) {
    uint32_t rs = (instr >> 21) & 0x1F;
    uint32_t rt = (instr >> 16) & 0x1F;
    int16_t imm = (int16_t)(instr & 0xFFFF);
    if (cpu_reg(cpu, rs) == cpu_reg(cpu, rt)) {
        cpu->next_pc = cpu->current_pc + 4 + ((int32_t)imm << 2);
        cpu->branch_taken = true;
    }
}
static void nc_op_bne(NcCpu* cpu, uint32_t instr) {
    uint32_t rs = (instr >> 21) & 0x1F;
    uint32_t rt = (instr >> 16) & 0x1F;
    int16_t imm = (int16_t)(instr & 0xFFFF);
    if (cpu_reg(cpu, rs) != cpu_reg(cpu, rt)) {
        cpu->next_pc = cpu->current_pc + 4 + ((int32_t)imm << 2);
        cpu->branch_taken = true;
    }
}
static void nc_op_blez(NcCpu* cpu, uint32_t instr) {
    uint32_t rs = (instr >> 21) & 0x1F;
    int16_t imm = (int16_t)(instr & 0xFFFF);
    if ((int32_t)cpu_reg(cpu, rs) <= 0) {
        cpu->next_pc = cpu->current_pc + 4 + ((int32_t)imm << 2);
        cpu->branch_taken = true;
    }
}
static void nc_op_bgtz(NcCpu* cpu, uint32_t instr) {
    uint32_t rs = (instr >> 21) & 0x1F;
    int16_t imm = (int16_t)(instr & 0xFFFF);
    if ((int32_t)cpu_reg(cpu, rs) > 0) {
        cpu->next_pc = cpu->current_pc + 4 + ((int32_t)imm << 2);
        cpu->branch_taken = true;
    }
}
static void nc_op_j(NcCpu* cpu, uint32_t instr) {
    uint32_t target = instr & 0x03FFFFFF;
    cpu->next_pc = (cpu->current_pc & 0xF0000000) | (target << 2);
    cpu->branch_taken = true;
}
static void nc_op_jal(NcCpu* cpu, uint32_t instr) {
    uint32_t target = instr & 0x03FFFFFF;
    cpu_set_reg(cpu, 31, cpu->current_pc + 8); // $ra = PC after delay slot
    cpu->next_pc = (cpu->current_pc & 0xF0000000) | (target << 2);
    cpu->branch_taken = true;
}
static void nc_op_jr(NcCpu* cpu, uint32_t instr) {
    uint32_t rs = (instr >> 21) & 0x1F;
    cpu->next_pc = cpu_reg(cpu, rs);
    cpu->branch_taken = true;
}
static void nc_op_jalr(NcCpu* cpu, uint32_t instr) {
    uint32_t rd = (instr >> 11) & 0x1F;
    uint32_t rs = (instr >> 21) & 0x1F;
    cpu_set_reg(cpu, rd, cpu->current_pc + 8); // $rd = PC after delay slot
    cpu->next_pc = cpu_reg(cpu, rs);
    cpu->branch_taken = true;
}

// --- Immediate and system control instruction handlers ---
static void nc_op_lui(NcCpu* cpu, uint32_t instr) {
    uint32_t rt = (instr >> 16) & 0x1F;
    uint16_t imm = instr & 0xFFFF;
    cpu_set_reg(cpu, rt, imm << 16);
}
static void nc_op_ori(NcCpu* cpu, uint32_t instr) {
    uint32_t rt = (instr >> 16) & 0x1F;
    uint32_t rs = (instr >> 21) & 0x1F;
    uint16_t imm = instr & 0xFFFF;
    cpu_set_reg(cpu, rt, cpu_reg(cpu, rs) | imm);
}
static void nc_op_andi(NcCpu* cpu, uint32_t instr) {
    uint32_t rt = (instr >> 16) & 0x1F;
    uint32_t rs = (instr >> 21) & 0x1F;
    uint16_t imm = instr & 0xFFFF;
    cpu_set_reg(cpu, rt, cpu_reg(cpu, rs) & imm);
}
static void nc_op_xori(NcCpu* cpu, uint32_t instr) {
    uint32_t rt = (instr >> 16) & 0x1F;
    uint32_t rs = (instr >> 21) & 0x1F;
    uint16_t imm = instr & 0xFFFF;
    cpu_set_reg(cpu, rt, cpu_reg(cpu, rs) ^ imm);
}
static void nc_op_addi(NcCpu* cpu, uint32_t instr) {
    uint32_t rt = (instr >> 16) & 0x1F;
    uint32_t rs = (instr >> 21) & 0x1F;
    int16_t imm = (int16_t)(instr & 0xFFFF);
    int32_t a = (int32_t)cpu_reg(cpu, rs);
    int32_t res = a + imm;
    if (((a ^ res) & (imm ^ res)) < 0) {
        nc_cpu_exception(cpu, NC_EXC_OVERFLOW);
        return;
    }
    cpu_set_reg(cpu, rt, (uint32_t)res);
}
static void nc_op_addiu(NcCpu* cpu, uint32_t instr) {
    uint32_t rt = (instr >> 16) & 0x1F;
    uint32_t rs = (instr >> 21) & 0x1F;
    int16_t imm = (int16_t)(instr & 0xFFFF);
    cpu_set_reg(cpu, rt, cpu_reg(cpu, rs) + imm);
}
static void nc_op_slti(NcCpu* cpu, uint32_t instr) {
    uint32_t rt = (instr >> 16) & 0x1F;
    uint32_t rs = (instr >> 21) & 0x1F;
    int16_t imm = (int16_t)(instr & 0xFFFF);
    cpu_set_reg(cpu, rt, ((int32_t)cpu_reg(cpu, rs) < imm) ? 1 : 0);
}
static void nc_op_sltiu(NcCpu* cpu, uint32_t instr) {
    uint32_t rt = (instr >> 16) & 0x1F;
    uint32_t rs = (instr >> 21) & 0x1F;
    uint16_t imm = instr & 0xFFFF;
    cpu_set_reg(cpu, rt, (cpu_reg(cpu, rs) < imm) ? 1 : 0);
}
// --- COP0 (System Control) ---
static void nc_op_cop0(NcCpu* cpu, uint32_t instr) {
    uint32_t cop_op = (instr >> 21) & 0x1F;
    if (cop_op == 0x00) { // MFC0
        uint32_t rt = (instr >> 16) & 0x1F;
        uint32_t rd = (instr >> 11) & 0x1F;
        // Only SR (12) and CAUSE (13) are implemented for now
        uint32_t value = 0;
        if (rd == 12) value = cpu->sr;
        else if (rd == 13) value = cpu->cause;
        cpu_set_reg(cpu, rt, value);
    } else if (cop_op == 0x04) { // MTC0
        uint32_t rt = (instr >> 16) & 0x1F;
        uint32_t rd = (instr >> 11) & 0x1F;
        uint32_t value = cpu_reg(cpu, rt);
        if (rd == 12) cpu->sr = value;
        else if (rd == 13) cpu->cause = value;
    } else if (cop_op == 0x10 && (instr & 0x3F) == 0x10) { // RFE
        // Restore previous mode bits in SR
        uint32_t mode_stack = cpu->sr & 0x3F;
        cpu->sr &= ~0x3F;
        cpu->sr |= (mode_stack >> 2) & 0x3F;
    } else {
        nc_cpu_exception(cpu, NC_EXC_COPROCESSOR);
    }
}
// --- COP2 (GTE integration) ---
static void nc_op_cop2(NcCpu* cpu, uint32_t instr) {
    // TODO: Wire up GTE pointer properly (e.g., via EmulatorContext or CPU struct)
    NC_LOGW("[CPU] GTE instruction: GTE pointer not wired");
}
// --- SYSCALL and BREAK ---
static void nc_op_syscall(NcCpu* cpu, uint32_t instr) {
    nc_cpu_exception(cpu, NC_EXC_SYSCALL);
}
static void nc_op_break(NcCpu* cpu, uint32_t instr) {
    nc_cpu_exception(cpu, NC_EXC_BREAK);
}

// --- R-type subfunction dispatch table ---
typedef void (*NcInstrHandler)(NcCpu*, uint32_t);
static NcInstrHandler rtype_table[64] = {
    nc_op_sll, 0, nc_op_srl, nc_op_sra, nc_op_sllv, 0, nc_op_srlv, nc_op_srav,
    nc_op_jr, nc_op_jalr, 0, 0, nc_op_syscall, nc_op_break, 0, 0,
    nc_op_mfhi, nc_op_mthi, nc_op_mflo, nc_op_mtlo, nc_op_mult, nc_op_multu, nc_op_div, nc_op_divu,
    nc_op_add, nc_op_addu, nc_op_sub, nc_op_subu, nc_op_and, nc_op_or, nc_op_xor, nc_op_nor,
    0, 0, nc_op_slt, nc_op_sltu, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0
};

// --- Main opcode dispatch table (stubs for now) ---
#undef HANDLER
#define HANDLER(name) name
static NcInstrHandler opcode_table[64] = {
    0, nc_op_j, nc_op_jal, nc_op_beq, nc_op_bne, nc_op_blez, nc_op_bgtz,
    nc_op_addi, nc_op_addiu, nc_op_slti, nc_op_sltiu, nc_op_andi, nc_op_ori, nc_op_xori, nc_op_lui,
    nc_op_cop0, nc_op_cop1, nc_op_cop2, nc_op_cop3, 0, 0, 0, 0,
    nc_op_lb, nc_op_lh, nc_op_lwl, nc_op_lw, nc_op_lbu, nc_op_lhu, nc_op_lwr, 0,
    nc_op_sb, nc_op_sh, nc_op_swl, nc_op_sw, 0, 0, nc_op_swr, 0,
    nc_op_lwc0, nc_op_lwc1, nc_op_lwc2, nc_op_lwc3, nc_op_swc0, nc_op_swc1, nc_op_swc2, nc_op_swc3,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0
};

// Add stubs for all missing handlers referenced in the opcode table
HANDLER(nc_op_cop1)
HANDLER(nc_op_cop3)
HANDLER(nc_op_lwl)
HANDLER(nc_op_lwr)
HANDLER(nc_op_swl)
HANDLER(nc_op_swr)
HANDLER(nc_op_lwc0)
HANDLER(nc_op_lwc1)
HANDLER(nc_op_lwc2)
HANDLER(nc_op_lwc3)
HANDLER(nc_op_swc0)
HANDLER(nc_op_swc1)
HANDLER(nc_op_swc2)
HANDLER(nc_op_swc3)

// --- Main decode/execute with delay slot and load delay logic ---
void nc_decode_and_execute(NcCpu* cpu, uint32_t instruction) {
    // Handle load delay slot: apply previous load to register
    if (cpu->load_reg_idx != NC_REG_ZERO) {
        cpu_set_reg(cpu, cpu->load_reg_idx, cpu->load_value);
        cpu->load_reg_idx = NC_REG_ZERO;
    }
    cpu->in_delay_slot = cpu->branch_taken;
    cpu->branch_taken = false;
    uint32_t opcode = (instruction >> 26) & 0x3F;
    if (opcode == 0x00) {
        uint32_t subfunc = instruction & 0x3F;
        NcInstrHandler handler = rtype_table[subfunc];
        if (handler) handler(cpu, instruction);
        else nc_cpu_exception(cpu, NC_EXC_ILLEGAL);
        return;
    }
    NcInstrHandler handler = opcode_table[opcode];
    if (handler) handler(cpu, instruction);
    else nc_cpu_exception(cpu, NC_EXC_ILLEGAL);
}

// Initialize CPU state to power-on defaults
void nc_cpu_init(NcCpu* cpu, struct NcInterconnect* inter) {
    NC_LOGI("CPU initialization started");
    cpu->pc = 0xbfc00000;
    cpu->next_pc = cpu->pc + 4;
    cpu->current_pc = cpu->pc;
    cpu->inter = inter;
    for (int i = 0; i < 32; ++i) {
        cpu->regs[i] = 0xdeadbeef;
        cpu->out_regs[i] = 0xdeadbeef;
    }
    cpu->regs[NC_REG_ZERO] = 0;
    cpu->out_regs[NC_REG_ZERO] = 0;
    cpu->load_reg_idx = NC_REG_ZERO;
    cpu->load_value = 0;
    cpu->hi = 0xdeadbeef;
    cpu->lo = 0xdeadbeef;
    cpu->branch_taken = false;
    cpu->in_delay_slot = false;
    cpu->exception_pending = false;
    cpu->sr = 0;
    cpu->cause = 0;
    cpu->epc = 0;
    for (int i = 0; i < NC_ICACHE_LINES; ++i) {
        cpu->icache[i].tag = 0xFFFFFFFF;
        for (int j = 0; j < NC_ICACHE_WORDS; ++j) {
            cpu->icache[i].valid[j] = false;
            cpu->icache[i].data[j] = 0xDEADBEEF;
        }
    }
    NC_LOGI("CPU initialized, PC=0x%08x", cpu->pc);
} 