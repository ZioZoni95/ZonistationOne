#ifndef CPU_DISASM_H
#define CPU_DISASM_H

#include "cpu_types.h"
#include <stddef.h>

// ============================================================
// CPU Disassembler API
// ============================================================

/**
 * @brief Disassemble a single MIPS instruction to human-readable format
 * @param dest Buffer to store the disassembled instruction string
 * @param pc Program counter value for the instruction
 * @param bits The 32-bit instruction word
 */
void cpu_disassemble_instruction(char* dest, size_t dest_size, uint32_t pc, uint32_t bits);

/**
 * @brief Disassemble a single MIPS instruction with comments
 * @param dest Buffer to store the disassembled instruction string with comments
 * @param pc Program counter value for the instruction
 * @param bits The 32-bit instruction word
 */
void cpu_disassemble_instruction_comment(char* dest, size_t dest_size, uint32_t pc, uint32_t bits);

/**
 * @brief Get the name of a GTE register
 * @param index GTE register index (0-63)
 * @return Register name string, or NULL if invalid
 */
const char* cpu_get_gte_register_name(uint32_t index);

#endif // CPU_DISASM_H