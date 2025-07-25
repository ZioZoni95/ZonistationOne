// cpu_core.h
// Migrated from cpu.c: main MIPS interpreter logic (header)
// TODO: Move CPU state struct and function prototypes here.

#ifndef CPU_CORE_H
#define CPU_CORE_H

#include <stdint.h>
#include <stdbool.h>

// --- CPU State Structure ---
typedef struct {
    uint32_t pc;         // Program counter
    uint32_t regs[32];   // General purpose registers
    uint32_t hi, lo;     // HI/LO registers for multiplication/division
    bool stopped;        // Stop flag for interpreter loop
    // TODO: Add coprocessor state, exception state, delay slot, etc.
} CpuState;

// --- Interpreter API ---
// Run the interpreter loop until stopped
void cpu_core_run(CpuState* cpu);
// Execute a single instruction (fetch, decode, execute)
void cpu_core_step(CpuState* cpu);
// Fetch an instruction from memory (to be implemented)
uint32_t cpu_core_fetch(CpuState* cpu, uint32_t pc);
// Initialize opcode tables and CPU state
void cpu_core_init(CpuState* cpu);

#endif // CPU_CORE_H 