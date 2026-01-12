#ifndef CPU_INSTRUCTIONS_H
#define CPU_INSTRUCTIONS_H

#include <stdint.h>

// Forward declaration
typedef struct Cpu Cpu;

// ============================================================
// Instruction Decoder
// ============================================================

/**
 * @brief Initialize instruction dispatch tables
 * Must be called once at startup before any instruction decoding.
 * Sets up O(1) lookup tables for optimal performance.
 */
void cpu_instructions_init(void);

/**
 * @brief Decodes and executes a single instruction
 * @param cpu Pointer to CPU state
 * @param instruction 32-bit instruction word
 */
void decode_and_execute(Cpu* cpu, uint32_t instruction);

// ============================================================
// Instruction Handlers
// ============================================================

// Arithmetic/Logic
void op_lui(Cpu* cpu, uint32_t instruction);
void op_ori(Cpu* cpu, uint32_t instruction);
void op_addiu(Cpu* cpu, uint32_t instruction);
void op_addi(Cpu* cpu, uint32_t instruction);
void op_addu(Cpu* cpu, uint32_t instruction);
void op_add(Cpu* cpu, uint32_t instruction);
void op_subu(Cpu* cpu, uint32_t instruction);
void op_sub(Cpu* cpu, uint32_t instruction);
void op_and(Cpu* cpu, uint32_t instruction);
void op_andi(Cpu* cpu, uint32_t instruction);
void op_or(Cpu* cpu, uint32_t instruction);
void op_xor(Cpu* cpu, uint32_t instruction);
void op_xori(Cpu* cpu, uint32_t instruction);
void op_nor(Cpu* cpu, uint32_t instruction);

// Shifts
void op_sll(Cpu* cpu, uint32_t instruction);
void op_srl(Cpu* cpu, uint32_t instruction);
void op_sra(Cpu* cpu, uint32_t instruction);
void op_sllv(Cpu* cpu, uint32_t instruction);
void op_srlv(Cpu* cpu, uint32_t instruction);
void op_srav(Cpu* cpu, uint32_t instruction);

// Comparisons
void op_slt(Cpu* cpu, uint32_t instruction);
void op_sltu(Cpu* cpu, uint32_t instruction);
void op_slti(Cpu* cpu, uint32_t instruction);
void op_sltiu(Cpu* cpu, uint32_t instruction);

// Branches/Jumps
void op_j(Cpu* cpu, uint32_t instruction);
void op_jal(Cpu* cpu, uint32_t instruction);
void op_jr(Cpu* cpu, uint32_t instruction);
void op_jalr(Cpu* cpu, uint32_t instruction);
void op_beq(Cpu* cpu, uint32_t instruction);
void op_bne(Cpu* cpu, uint32_t instruction);
void op_blez(Cpu* cpu, uint32_t instruction);
void op_bgtz(Cpu* cpu, uint32_t instruction);
void op_bxx(Cpu* cpu, uint32_t instruction); // BLTZ/BGEZ/BLTZAL/BGEZAL

// Multiply/Divide
void op_mult(Cpu* cpu, uint32_t instruction);
void op_multu(Cpu* cpu, uint32_t instruction);
void op_div(Cpu* cpu, uint32_t instruction);
void op_divu(Cpu* cpu, uint32_t instruction);
void op_mfhi(Cpu* cpu, uint32_t instruction);
void op_mflo(Cpu* cpu, uint32_t instruction);
void op_mthi(Cpu* cpu, uint32_t instruction);
void op_mtlo(Cpu* cpu, uint32_t instruction);

// Load/Store
void op_lb(Cpu* cpu, uint32_t instruction);
void op_lh(Cpu* cpu, uint32_t instruction);
void op_lw(Cpu* cpu, uint32_t instruction);
void op_lbu(Cpu* cpu, uint32_t instruction);
void op_lhu(Cpu* cpu, uint32_t instruction);
void op_lwl(Cpu* cpu, uint32_t instruction);
void op_lwr(Cpu* cpu, uint32_t instruction);
void op_sb(Cpu* cpu, uint32_t instruction);
void op_sh(Cpu* cpu, uint32_t instruction);
void op_sw(Cpu* cpu, uint32_t instruction);
void op_swl(Cpu* cpu, uint32_t instruction);
void op_swr(Cpu* cpu, uint32_t instruction);

// Coprocessor Instructions
void op_cop0(Cpu* cpu, uint32_t instruction);
void op_cop1(Cpu* cpu, uint32_t instruction);
void op_cop2(Cpu* cpu, uint32_t instruction);
void op_cop3(Cpu* cpu, uint32_t instruction);
void op_mfc0(Cpu* cpu, uint32_t instruction);
void op_mtc0(Cpu* cpu, uint32_t instruction);
void op_rfe(Cpu* cpu, uint32_t instruction);
void op_lwc0(Cpu* cpu, uint32_t instruction);
void op_lwc1(Cpu* cpu, uint32_t instruction);
void op_lwc2(Cpu* cpu, uint32_t instruction);
void op_lwc3(Cpu* cpu, uint32_t instruction);
void op_swc0(Cpu* cpu, uint32_t instruction);
void op_swc1(Cpu* cpu, uint32_t instruction);
void op_swc2(Cpu* cpu, uint32_t instruction);
void op_swc3(Cpu* cpu, uint32_t instruction);

// System
void op_syscall(Cpu* cpu, uint32_t instruction);
void op_break(Cpu* cpu, uint32_t instruction);
void op_illegal(Cpu* cpu, uint32_t instruction);

#endif // CPU_INSTRUCTIONS_H
