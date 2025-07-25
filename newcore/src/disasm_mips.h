// disasm_mips.h
// Disassembler logic (header)
// TODO: Move or port disassembler declarations here if present.

#ifndef DISASM_MIPS_H
#define DISASM_MIPS_H

#include <stdint.h>
#include <stddef.h>

// --- MIPS Disassembler API ---
// Disassemble a single instruction to a string
void disasm_mips_instruction(uint32_t instruction, char* out_buf, size_t buf_size);
// Disassemble a block of instructions (optional)
void disasm_mips_block(const uint32_t* code, size_t count, char* out_buf, size_t buf_size);

#endif // DISASM_MIPS_H 