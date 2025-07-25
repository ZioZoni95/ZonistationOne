#ifndef NEWCORE_CPU_H
#define NEWCORE_CPU_H

#include <stdint.h>
#include <stdbool.h>
#include "../include/gte.h"
struct NcInterconnect;

// Register index type and constants
typedef uint32_t NcRegIndex;
#define NC_REG_ZERO ((NcRegIndex)0)
#define NC_REG_RA   ((NcRegIndex)31)

// Exception cause codes
typedef enum {
    NC_EXC_INTERRUPT = 0x00,
    NC_EXC_LOAD_ADDR_ERR = 0x04,
    NC_EXC_STORE_ADDR_ERR = 0x05,
    NC_EXC_SYSCALL = 0x08,
    NC_EXC_BREAK = 0x09,
    NC_EXC_ILLEGAL = 0x0a,
    NC_EXC_COPROCESSOR = 0x0b,
    NC_EXC_OVERFLOW = 0x0c
} NcExceptionCause;

// Instruction cache geometry
#define NC_ICACHE_LINES 256
#define NC_ICACHE_WORDS 4

// Instruction cache line
typedef struct {
    uint32_t tag;
    bool valid[NC_ICACHE_WORDS];
    uint32_t data[NC_ICACHE_WORDS];
} NcICacheLine;

// CPU state structure
typedef struct NcCpu {
    uint32_t pc, next_pc, current_pc;
    uint32_t regs[32];
    uint32_t out_regs[32];
    NcRegIndex load_reg_idx;
    uint32_t load_value;
    uint32_t hi, lo;
    bool branch_taken, in_delay_slot, exception_pending;
    uint32_t sr, cause, epc;
    struct NcInterconnect* inter;
    NcICacheLine icache[NC_ICACHE_LINES];
    NcGte gte; // Add GTE coprocessor state
} NcCpu;

// CPU API
void nc_cpu_init(NcCpu* cpu, struct NcInterconnect* inter);
// ... (stubs for run, decode, exception, etc.)

#endif // NEWCORE_CPU_H 