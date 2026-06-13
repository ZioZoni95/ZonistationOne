#include "cpu.h"

/* Forward-declare op_special before s_op_table references it */
static void op_special(Cpu*, uint32_t);

/* SPECIAL subfunction dispatch table (instruction bits 5:0 — 64 slots).
 * Mirrors pcsx-redux s_psxSPC. NULL slots → op_illegal. */
static const cpu_handler_t s_special_table[64] = {
    [0x00] = op_sll,
    [0x02] = op_srl,
    [0x03] = op_sra,
    [0x04] = op_sllv,
    [0x06] = op_srlv,
    [0x07] = op_srav,
    [0x08] = op_jr,
    [0x09] = op_jalr,
    [0x0C] = op_syscall,
    [0x0D] = op_break,
    [0x10] = op_mfhi,
    [0x11] = op_mthi,
    [0x12] = op_mflo,
    [0x13] = op_mtlo,
    [0x18] = op_mult,
    [0x19] = op_multu,
    [0x1A] = op_div,
    [0x1B] = op_divu,
    [0x20] = op_add,
    [0x21] = op_addu,
    [0x22] = op_sub,
    [0x23] = op_subu,
    [0x24] = op_and,
    [0x25] = op_or,
    [0x26] = op_xor,
    [0x27] = op_nor,
    [0x2A] = op_slt,
    [0x2B] = op_sltu,
};

/* Primary opcode dispatch table (instruction bits 31:26 — 64 slots).
 * Mirrors pcsx-redux s_psxBSC. NULL slots → op_illegal. */
static const cpu_handler_t s_op_table[64] = {
    [0x00] = op_special,  /* SPECIAL — dispatches via s_special_table */
    [0x01] = op_bxx,      /* REGIMM (BGEZ/BLTZ/BGEZAL/BLTZAL) */
    [0x02] = op_j,
    [0x03] = op_jal,
    [0x04] = op_beq,
    [0x05] = op_bne,
    [0x06] = op_blez,
    [0x07] = op_bgtz,
    [0x08] = op_addi,
    [0x09] = op_addiu,
    [0x0A] = op_slti,
    [0x0B] = op_sltiu,
    [0x0C] = op_andi,
    [0x0D] = op_ori,
    [0x0E] = op_xori,
    [0x0F] = op_lui,
    [0x10] = op_cop0,
    [0x11] = op_cop1,     /* COP1 → exception (FPU absent) */
    [0x12] = op_cop2,     /* COP2 = GTE */
    [0x13] = op_cop3,     /* COP3 → exception */
    [0x20] = op_lb,
    [0x21] = op_lh,
    [0x22] = op_lwl,
    [0x23] = op_lw,
    [0x24] = op_lbu,
    [0x25] = op_lhu,
    [0x26] = op_lwr,
    [0x28] = op_sb,
    [0x29] = op_sh,
    [0x2A] = op_swl,
    [0x2B] = op_sw,
    [0x2E] = op_swr,
    [0x30] = op_lwc0,
    [0x31] = op_lwc1,
    [0x32] = op_lwc2,     /* LWC2 = GTE load */
    [0x33] = op_lwc3,
    [0x38] = op_swc0,
    [0x39] = op_swc1,
    [0x3A] = op_swc2,     /* SWC2 = GTE store */
    [0x3B] = op_swc3,
};

static void op_special(Cpu* cpu, uint32_t instruction) {
    cpu_handler_t h = s_special_table[instruction & 0x3F];
    if (h) h(cpu, instruction);
    else   op_illegal(cpu, instruction);
}

void decode_and_execute(Cpu* cpu, uint32_t instruction) {
    cpu_handler_t h = s_op_table[instruction >> 26];
    if (h) h(cpu, instruction);
    else   op_illegal(cpu, instruction);
}
