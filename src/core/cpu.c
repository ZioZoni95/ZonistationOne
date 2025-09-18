/*
 * ZonistationOne - PlayStation One Emulator
 * MIPS R3000A CPU Implementation (Stub)
 */

#include "cpu.h"
#include "logger.h"
#include <stdlib.h>
#include <string.h>

struct psx_cpu_s {
    /* General purpose registers */
    uint32_t gpr[32];
    
    /* Special registers */
    uint32_t pc;        /* Program counter */
    uint32_t hi, lo;    /* Multiply/divide result registers */
    
    /* Coprocessor 0 registers (System control) */
    uint32_t cop0_regs[32];
    
    /* Memory subsystem */
    psx_memory_t *memory;
    
    /* Execution state */
    uint32_t cycles;
    int debug_mode;
    int initialized;
};

psx_cpu_t *cpu_create(void) {
    psx_cpu_t *cpu = calloc(1, sizeof(psx_cpu_t));
    if (!cpu) {
        log_error("Failed to allocate CPU structure");
        return NULL;
    }
    
    log_debug("CPU structure created");
    return cpu;
}

int cpu_init(psx_cpu_t *cpu, psx_memory_t *memory) {
    if (!cpu || !memory) {
        log_error("Invalid parameters for CPU initialization");
        return -1;
    }
    
    if (cpu->initialized) {
        log_warn("CPU already initialized");
        return 0;
    }
    
    cpu->memory = memory;
    cpu_reset(cpu);
    
    cpu->initialized = 1;
    log_info("CPU initialized");
    
    return 0;
}

void cpu_reset(psx_cpu_t *cpu) {
    if (!cpu) return;
    
    /* Clear all general purpose registers */
    memset(cpu->gpr, 0, sizeof(cpu->gpr));
    
    /* Reset PC to BIOS entry point */
    cpu->pc = PSX_BIOS_BASE;
    
    /* Reset special registers */
    cpu->hi = 0;
    cpu->lo = 0;
    
    /* Reset coprocessor 0 registers */
    memset(cpu->cop0_regs, 0, sizeof(cpu->cop0_regs));
    
    /* Initialize some important COP0 registers */
    cpu->cop0_regs[15] = 0x00000002; /* PRid - Processor revision ID */
    
    cpu->cycles = 0;
    
    log_info("CPU reset (PC: 0x%08X)", cpu->pc);
}

int cpu_step(psx_cpu_t *cpu) {
    if (!cpu || !cpu->initialized) {
        log_error("CPU not initialized");
        return -1;
    }
    
    /* TODO: Implement instruction fetch, decode, and execute */
    
    /* For now, just increment cycles and PC as a placeholder */
    cpu->cycles++;
    cpu->pc += 4;
    
    /* Simple bounds check */
    if (cpu->pc >= PSX_BIOS_BASE + PSX_BIOS_SIZE) {
        log_warn("PC exceeded BIOS bounds: 0x%08X", cpu->pc);
        return -1;
    }
    
    return 0;
}

uint32_t cpu_get_cycles(psx_cpu_t *cpu) {
    return cpu ? cpu->cycles : 0;
}

uint32_t cpu_get_register(psx_cpu_t *cpu, int reg) {
    if (!cpu || reg < 0 || reg >= 32) {
        return 0;
    }
    
    /* Register 0 is always zero */
    if (reg == 0) {
        return 0;
    }
    
    return cpu->gpr[reg];
}

void cpu_set_register(psx_cpu_t *cpu, int reg, uint32_t value) {
    if (!cpu || reg <= 0 || reg >= 32) {
        return;
    }
    
    cpu->gpr[reg] = value;
}

uint32_t cpu_get_pc(psx_cpu_t *cpu) {
    return cpu ? cpu->pc : 0;
}

void cpu_set_pc(psx_cpu_t *cpu, uint32_t pc) {
    if (cpu) {
        cpu->pc = pc;
    }
}

void cpu_set_interrupt(psx_cpu_t *cpu, uint32_t mask) {
    if (!cpu) return;
    
    /* Set interrupt pending bits in Cause register */
    cpu->cop0_regs[13] |= (mask << 10);
    
    if (cpu->debug_mode) {
        log_debug("Interrupt set: 0x%08X", mask);
    }
}

void cpu_clear_interrupt(psx_cpu_t *cpu, uint32_t mask) {
    if (!cpu) return;
    
    /* Clear interrupt pending bits in Cause register */
    cpu->cop0_regs[13] &= ~(mask << 10);
    
    if (cpu->debug_mode) {
        log_debug("Interrupt cleared: 0x%08X", mask);
    }
}

void cpu_set_debug_mode(psx_cpu_t *cpu, int enabled) {
    if (cpu) {
        cpu->debug_mode = enabled;
        log_info("CPU debug mode %s", enabled ? "enabled" : "disabled");
    }
}

void cpu_shutdown(psx_cpu_t *cpu) {
    if (!cpu) return;
    
    cpu->initialized = 0;
    cpu->memory = NULL;
    
    log_info("CPU shutdown");
}

void cpu_destroy(psx_cpu_t *cpu) {
    if (cpu) {
        cpu_shutdown(cpu);
        free(cpu);
    }
}