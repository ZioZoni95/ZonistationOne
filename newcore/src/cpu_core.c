// cpu_core.c
// Migrated from cpu.c: main MIPS interpreter logic
// TODO: Move interpreter loop, instruction decode, and execution logic here.

#include "cpu_core.h"
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

// --- Opcode Dispatch Tables ---
// These tables map opcodes to handler functions, similar to psxBSC/psxSPC in the reference.
static void (*opcode_table[64])(CpuState*, uint32_t);
static void (*special_table[64])(CpuState*, uint32_t);
static void (*cop2_table[64])(CpuState*, uint32_t); // GTE

// --- CPU Interpreter Loop ---
// Main interpreter loop: fetch, decode, execute, until stopped.
void cpu_core_run(CpuState* cpu) {
    while (!cpu->stopped) {
        cpu_core_step(cpu);
    }
}

// Single step: fetch, decode, execute one instruction.
void cpu_core_step(CpuState* cpu) {
    uint32_t pc = cpu->pc;
    // TODO: Add cycle accounting, memory access, and delay slot logic as needed.
    uint32_t instruction = cpu_core_fetch(cpu, pc);
    cpu->pc += 4; // Advance PC (may be changed by branches)
    // Dispatch to handler based on top 6 bits (opcode)
    uint8_t opcode = (instruction >> 26) & 0x3F;
    if (opcode_table[opcode]) {
        opcode_table[opcode](cpu, instruction);
    } else {
        // TODO: Handle unknown opcode
    }
}

// Fetch instruction from memory (stub, to be implemented)
uint32_t cpu_core_fetch(CpuState* cpu, uint32_t pc) {
    // TODO: Integrate with memory subsystem
    return 0; // Placeholder
}

// --- Opcode Table Initialization ---
// Set up the opcode dispatch tables with handler functions.
void cpu_core_init_tables(void) {
    memset(opcode_table, 0, sizeof(opcode_table));
    memset(special_table, 0, sizeof(special_table));
    memset(cop2_table, 0, sizeof(cop2_table));
    // TODO: Assign handler functions, e.g.:
    // opcode_table[0x00] = cpu_core_special;
    // opcode_table[0x02] = cpu_core_j;
    // ...
}

// --- Example Handler (Stub) ---
void cpu_core_special(CpuState* cpu, uint32_t instruction) {
    // Dispatch to SPECIAL table based on function field
    uint8_t funct = instruction & 0x3F;
    if (special_table[funct]) {
        special_table[funct](cpu, instruction);
    } else {
        // TODO: Handle unknown SPECIAL function
    }
}

// ... Add more handler stubs and comments as needed ...

// --- Initialization Entry Point ---
void cpu_core_init(CpuState* cpu) {
    cpu_core_init_tables();
    // TODO: Initialize CPU state as needed
} 