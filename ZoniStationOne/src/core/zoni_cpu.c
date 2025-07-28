/**
 * @file zoni_cpu.c
 * @brief MIPS R3000A CPU emulation for ZoniStationOne
 * 
 * This file implements the CPU interpreter following the PCSX-ReARMed structure
 * but with ZoniStationOne naming conventions and modern C practices.
 */

#include "zoni_cpu.h"
#include "zoni_memory.h"
#include <string.h>

// Global CPU instance
static zoni_cpu_config_t g_cpu_config;
static zoni_memory_t* g_memory = NULL;

// Instruction execution function pointers
typedef void (*zoni_instruction_func_t)(zoni_cpu_regs_t* regs, u32 code);

// Basic instruction function arrays (will be populated later)
static zoni_instruction_func_t psx_basic[64];
static zoni_instruction_func_t psx_special[64];

// Load delay handling (following PCSX-ReARMed pattern)
void zoni_cpu_do_load(zoni_cpu_regs_t* cpu, u32 r, u32 val) {
    int sel = cpu->dload_sel ^ 1;
    ZONI_ASSERT(cpu->dload_reg[sel] == 0);
    cpu->dload_reg[sel] = r;
    cpu->dload_val[sel] = r ? val : 0;
    if (cpu->dload_reg[sel ^ 1] == r) {
        cpu->dload_val[sel ^ 1] = cpu->dload_reg[sel ^ 1] = 0;
    }
}

void zoni_cpu_dload_rt(zoni_cpu_regs_t* cpu, u32 r, u32 val) {
    int sel = cpu->dload_sel;
    if (cpu->dload_reg[sel] == r) {
        cpu->dload_val[sel] = cpu->dload_reg[sel] = 0;
    }
    cpu->gpr.r[r] = r ? val : 0;
}

void zoni_cpu_dload_step(zoni_cpu_regs_t* cpu) {
    int sel = cpu->dload_sel;
    if (cpu->dload_reg[sel] != 0) {
        cpu->gpr.r[cpu->dload_reg[sel]] = cpu->dload_val[sel];
    }
    cpu->dload_val[sel] = cpu->dload_reg[sel] = 0;
    cpu->dload_sel ^= 1;
    ZONI_ASSERT(cpu->gpr.r[0] == 0);
}

void zoni_cpu_dload_flush(zoni_cpu_regs_t* cpu) {
    cpu->gpr.r[cpu->dload_reg[0]] = cpu->dload_val[0];
    cpu->gpr.r[cpu->dload_reg[1]] = cpu->dload_val[1];
    cpu->dload_val[0] = cpu->dload_val[1] = 0;
    cpu->dload_reg[0] = cpu->dload_reg[1] = 0;
    ZONI_ASSERT(cpu->gpr.r[0] == 0);
}

void zoni_cpu_dload_clear(zoni_cpu_regs_t* cpu) {
    cpu->dload_val[0] = cpu->dload_val[1] = 0;
    cpu->dload_reg[0] = cpu->dload_reg[1] = 0;
    cpu->dload_sel = 0;
}

// Exception handling
static void zoni_cpu_exception(zoni_cpu_regs_t* cpu, zoni_exception_t cause, u32 epc) {
    zoni_log(ZONI_LOG_DEBUG, "CPU Exception: %d at PC=0x%08X", cause, epc);
    
    cpu->cp0.n.Cause = cause;
    cpu->cp0.n.EPC = epc;
    
    // Set exception vector based on cause
    switch (cause) {
        case ZONI_EXCEPTION_INT:
            cpu->pc = 0x80000080; // Interrupt vector
            break;
        case ZONI_EXCEPTION_SYSCALL:
            cpu->pc = 0x80000040; // Syscall vector
            break;
        case ZONI_EXCEPTION_BP:
            cpu->pc = 0x80000048; // Breakpoint vector
            break;
        default:
            cpu->pc = 0x80000080; // General exception vector
            break;
    }
    
    // Set exception mode in Status Register
    cpu->cp0.n.SR |= 0x00000002; // EXL bit
}

void zoni_cpu_trigger_exception(zoni_cpu_regs_t* cpu, zoni_exception_t cause, u32 epc) {
    zoni_cpu_exception(cpu, cause, epc);
}

void zoni_cpu_handle_interrupt(zoni_cpu_regs_t* cpu, u32 interrupt) {
    zoni_log(ZONI_LOG_DEBUG, "CPU Interrupt: 0x%08X", interrupt);
    cpu->interrupt |= interrupt;
}

// Memory access functions (delegate to memory system)
zoni_error_t zoni_cpu_read8(zoni_cpu_regs_t* cpu, u32 address, u8* value) {
    ZONI_UNUSED(cpu);
    if (!g_memory) return ZONI_ERROR_INITIALIZATION_FAILED;
    return zoni_memory_read8(g_memory, address, value);
}

zoni_error_t zoni_cpu_read16(zoni_cpu_regs_t* cpu, u32 address, u16* value) {
    ZONI_UNUSED(cpu);
    if (!g_memory) return ZONI_ERROR_INITIALIZATION_FAILED;
    return zoni_memory_read16(g_memory, address, value);
}

zoni_error_t zoni_cpu_read32(zoni_cpu_regs_t* cpu, u32 address, u32* value) {
    ZONI_UNUSED(cpu);
    if (!g_memory) return ZONI_ERROR_INITIALIZATION_FAILED;
    return zoni_memory_read32(g_memory, address, value);
}

zoni_error_t zoni_cpu_write8(zoni_cpu_regs_t* cpu, u32 address, u8 value) {
    ZONI_UNUSED(cpu);
    if (!g_memory) return ZONI_ERROR_INITIALIZATION_FAILED;
    return zoni_memory_write8(g_memory, address, value);
}

zoni_error_t zoni_cpu_write16(zoni_cpu_regs_t* cpu, u32 address, u16 value) {
    ZONI_UNUSED(cpu);
    if (!g_memory) return ZONI_ERROR_INITIALIZATION_FAILED;
    return zoni_memory_write16(g_memory, address, value);
}

zoni_error_t zoni_cpu_write32(zoni_cpu_regs_t* cpu, u32 address, u32 value) {
    ZONI_UNUSED(cpu);
    if (!g_memory) return ZONI_ERROR_INITIALIZATION_FAILED;
    return zoni_memory_write32(g_memory, address, value);
}

// Register access functions
u32 zoni_cpu_get_register(zoni_cpu_regs_t* cpu, u8 reg) {
    if (reg >= 34) return 0;
    return cpu->gpr.r[reg];
}

void zoni_cpu_set_register(zoni_cpu_regs_t* cpu, u8 reg, u32 value) {
    if (reg >= 34) return;
    if (reg == 0) return; // r0 is always zero
    cpu->gpr.r[reg] = value;
}

// CPU initialization
zoni_error_t zoni_cpu_init(zoni_cpu_regs_t* cpu, const zoni_cpu_config_t* config) {
    if (!cpu || !config) {
        return ZONI_ERROR_INVALID_PARAMETER;
    }
    
    // Store global references
    g_cpu_config = *config;
    
    // Initialize CPU registers
    memset(cpu, 0, sizeof(zoni_cpu_regs_t));
    
    // Set initial PC to BIOS entry point
    cpu->pc = 0xBFC00000;
    
    // Initialize coprocessor 0 registers
    cpu->cp0.n.SR = 0x00000000;  // Status Register
    cpu->cp0.n.Cause = 0x00000000; // Cause Register
    cpu->cp0.n.EPC = 0x00000000;   // Exception Program Counter
    cpu->cp0.n.PRid = 0x00000002;  // Processor ID (R3000A)
    
    // Initialize load delay slots
    zoni_cpu_dload_clear(cpu);
    
    // Initialize instruction function arrays (will be populated later)
    memset(psx_basic, 0, sizeof(psx_basic));
    memset(psx_special, 0, sizeof(psx_special));
    
    zoni_log(ZONI_LOG_INFO, "CPU initialized successfully");
    return ZONI_SUCCESS;
}

void zoni_cpu_reset(zoni_cpu_regs_t* cpu) {
    if (!cpu) return;
    
    // Reset CPU state
    cpu->pc = 0xBFC00000;
    cpu->cycle = 0;
    cpu->interrupt = 0;
    cpu->stop = 0;
    cpu->branch_seen = 0;
    cpu->branching = 0;
    
    // Reset coprocessor 0 registers
    cpu->cp0.n.SR = 0x00000000;
    cpu->cp0.n.Cause = 0x00000000;
    cpu->cp0.n.EPC = 0x00000000;
    
    // Clear load delay slots
    zoni_cpu_dload_clear(cpu);
    
    // Reset performance counters
    cpu->instruction_count = 0;
    cpu->cycle_count = 0;
    
    zoni_log(ZONI_LOG_INFO, "CPU reset");
}

void zoni_cpu_shutdown(zoni_cpu_regs_t* cpu) {
    ZONI_UNUSED(cpu);
    zoni_log(ZONI_LOG_INFO, "CPU shutdown");
}

// CPU execution (placeholder for now)
zoni_error_t zoni_cpu_execute(zoni_cpu_regs_t* cpu, u32 cycles) {
    ZONI_UNUSED(cpu);
    ZONI_UNUSED(cycles);
    
    // TODO: Implement instruction execution
    zoni_log(ZONI_LOG_DEBUG, "CPU execute: %u cycles", cycles);
    return ZONI_ERROR_NOT_IMPLEMENTED;
}

zoni_error_t zoni_cpu_step(zoni_cpu_regs_t* cpu) {
    ZONI_UNUSED(cpu);
    
    // TODO: Implement single instruction execution
    zoni_log(ZONI_LOG_DEBUG, "CPU step");
    return ZONI_ERROR_NOT_IMPLEMENTED;
}

// Debug functions
void zoni_cpu_dump_registers(zoni_cpu_regs_t* cpu) {
    if (!cpu) return;
    
    zoni_log(ZONI_LOG_INFO, "CPU Registers:");
    zoni_log(ZONI_LOG_INFO, "PC: 0x%08X", cpu->pc);
    zoni_log(ZONI_LOG_INFO, "Code: 0x%08X", cpu->code);
    zoni_log(ZONI_LOG_INFO, "Cycle: %u", cpu->cycle);
    
    // Dump GPR registers
    for (int i = 0; i < 32; i += 4) {
        zoni_log(ZONI_LOG_INFO, "R%02d: 0x%08X  R%02d: 0x%08X  R%02d: 0x%08X  R%02d: 0x%08X",
                 i, cpu->gpr.r[i], i+1, cpu->gpr.r[i+1], i+2, cpu->gpr.r[i+2], i+3, cpu->gpr.r[i+3]);
    }
    
    // Dump special registers
    zoni_log(ZONI_LOG_INFO, "LO: 0x%08X  HI: 0x%08X", cpu->gpr.r[32], cpu->gpr.r[33]);
    
    // Dump CP0 registers
    zoni_log(ZONI_LOG_INFO, "SR: 0x%08X  Cause: 0x%08X  EPC: 0x%08X",
             cpu->cp0.n.SR, cpu->cp0.n.Cause, cpu->cp0.n.EPC);
}

void zoni_cpu_disassemble_instruction(zoni_cpu_regs_t* cpu, u32 address, char* buffer, size_t buffer_size) {
    ZONI_UNUSED(cpu);
    ZONI_UNUSED(address);
    
    // TODO: Implement instruction disassembly
    snprintf(buffer, buffer_size, "NOP");
}

// Set memory reference (called from emulator)
void zoni_cpu_set_memory(zoni_memory_t* memory) {
    g_memory = memory;
} 