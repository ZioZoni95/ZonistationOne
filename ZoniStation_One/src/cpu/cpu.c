#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zonistation_common.h"
#include "zonistation_cpu.h"
#include "zonistation_memory.h"
#include "zonistation_config.h"

// CPU initialization
zs_error_t zs_cpu_init(zs_cpu_t** cpu_ptr, zs_memory_t* memory, const zs_config_t* config) {
    ZS_ASSERT(cpu_ptr != NULL);
    ZS_ASSERT(memory != NULL);
    ZS_ASSERT(config != NULL);
    
    zs_cpu_t* cpu = (zs_cpu_t*)malloc(sizeof(zs_cpu_t));
    if (cpu == NULL) {
        ZS_LOG_ERROR("Failed to allocate CPU structure");
        return ZS_ERROR_OUT_OF_MEMORY;
    }
    
    // Initialize CPU structure
    memset(cpu, 0, sizeof(zs_cpu_t));
    
    // Set up memory interface
    cpu->memory = memory;
    cpu->config = config;
    cpu->mode = config->cpu_mode;
    
    // Initialize registers structure
    memset(&cpu->regs, 0, sizeof(zs_cpu_registers_t));
    
    // Initialize GPR (General Purpose Registers)
    memset(&cpu->regs.gpr, 0, sizeof(zs_cpu_gpr_regs_t));
    
    // Initialize CP0 (Coprocessor 0) registers
    memset(&cpu->regs.cp0, 0, sizeof(zs_cpu_cp0_regs_t));
    
    // Initialize GTE (Geometry Transform Engine) registers
    memset(&cpu->regs.gte.data, 0, sizeof(zs_gte_data_regs_t));
    memset(&cpu->regs.gte.control, 0, sizeof(zs_gte_control_regs_t));
    
    // Set initial register values (migrated from PCSX-ReARMed)
    cpu->regs.pc = 0xBFC00000;                        // BIOS entry point
    
    // Initialize CP0 registers with default values (migrated from PCSX-ReARMed)
    cpu->regs.cp0.n.SR = 0x10600000;                  // COP0 enabled | BEV = 1 | TS = 1
    cpu->regs.cp0.n.PRid = 0x00000002;                // PRevID = Revision ID, same as R3000A
    
    // Enable COP2 (GTE) if HLE is enabled
    if (config->enable_hle_bios) {
        cpu->regs.cp0.n.SR |= 1u << 30;               // COP2 enabled
        cpu->regs.cp0.n.SR &= ~(1u << 22);            // RAM exception vector
    }
    
    // Initialize execution state
    cpu->regs.code = 0;                               // No instruction loaded
    cpu->regs.cycle = 0;                              // Start at cycle 0
    cpu->regs.interrupt = 0;                          // No interrupts pending
    
    // Initialize interrupt cycle tracking
    for (int i = 0; i < 20; i++) {
        cpu->regs.intCycle[i].sCycle = 0;
        cpu->regs.intCycle[i].cycle = 0;
        cpu->regs.event_cycles[i] = 0;
    }
    
    // Initialize timing
    cpu->regs.psxNextCounter = 0;
    cpu->regs.psxNextsCounter = 0;
    cpu->regs.next_interrupt = 0;
    cpu->regs.gteBusyCycle = 0;
    cpu->regs.muldivBusyCycle = 0;
    cpu->regs.subCycle = 0;
    cpu->regs.subCycleStep = 0;
    
    // Initialize CPU state
    cpu->regs.biuReg = 0;
    cpu->regs.stop = 0;
    cpu->regs.branchSeen = 0;
    cpu->regs.branching = ZS_CPU_BRANCH_NONE_OR_EXCEPTION;
    cpu->regs.dloadSel = 0;
    
    // Initialize delay slot handling
    memset(cpu->regs.dloadReg, 0, sizeof(cpu->regs.dloadReg));
    memset(cpu->regs.dloadVal, 0, sizeof(cpu->regs.dloadVal));
    
    // Initialize advanced features
    cpu->regs.biosBranchCheck = 0;
    cpu->regs.cpuInRecursion = 0;
    cpu->regs.gpuIdleAfter = 0;
    
    // Initialize state
    cpu->state.initialized = ZS_TRUE;
    cpu->state.running = ZS_FALSE;
    cpu->state.halted = ZS_FALSE;
    cpu->state.in_exception = ZS_FALSE;
    cpu->state.in_interrupt = ZS_FALSE;
    cpu->state.current_cycles = 0;
    cpu->state.total_cycles = 0;
    cpu->state.instruction_count = 0;
    
    // Initialize pipeline state
    cpu->delay_slot = ZS_FALSE;
    cpu->delay_slot_pc = 0;
    cpu->branch_taken = ZS_FALSE;
    
    // Initialize cache
    memset(cpu->icache, 0, sizeof(cpu->icache));
    memset(cpu->icache_tags, 0, sizeof(cpu->icache_tags));
    memset(cpu->icache_valid, 0, sizeof(cpu->icache_valid));
    
    // Initialize performance counters
    cpu->instructions_executed = 0;
    cpu->branches_taken = 0;
    cpu->cache_hits = 0;
    cpu->cache_misses = 0;
    
    *cpu_ptr = cpu;
    
    ZS_LOG_INFO("CPU initialized successfully with comprehensive register set");
    return ZS_SUCCESS;
}

zs_error_t zs_cpu_shutdown(zs_cpu_t* cpu) {
    if (cpu == NULL) {
        return ZS_ERROR_INVALID_PARAMETER;
    }
    
    ZS_LOG_INFO("Shutting down CPU...");
    free(cpu);
    return ZS_SUCCESS;
}

zs_error_t zs_cpu_reset(zs_cpu_t* cpu) {
    if (cpu == NULL || !cpu->state.initialized) {
        return ZS_ERROR_INVALID_PARAMETER;
    }
    
    ZS_LOG_INFO("Resetting CPU...");
    
    // Reset GPR (General Purpose Registers)
    memset(&cpu->regs.gpr, 0, sizeof(zs_cpu_gpr_regs_t));
    
    // Reset CP0 (Coprocessor 0) registers
    memset(&cpu->regs.cp0, 0, sizeof(zs_cpu_cp0_regs_t));
    
    // Reset GTE (Geometry Transform Engine) registers
    memset(&cpu->regs.gte.data, 0, sizeof(zs_gte_data_regs_t));
    memset(&cpu->regs.gte.control, 0, sizeof(zs_gte_control_regs_t));
    
    // Set initial register values (migrated from PCSX-ReARMed)
    cpu->regs.pc = 0xBFC00000;                        // BIOS entry point
    
    // Initialize CP0 registers with default values (migrated from PCSX-ReARMed)
    cpu->regs.cp0.n.SR = 0x10600000;                  // COP0 enabled | BEV = 1 | TS = 1
    cpu->regs.cp0.n.PRid = 0x00000002;                // PRevID = Revision ID, same as R3000A
    
    // Enable COP2 (GTE) if HLE is enabled
    if (cpu->config->enable_hle_bios) {
        cpu->regs.cp0.n.SR |= 1u << 30;               // COP2 enabled
        cpu->regs.cp0.n.SR &= ~(1u << 22);            // RAM exception vector
    }
    
    // Reset execution state
    cpu->regs.code = 0;                               // No instruction loaded
    cpu->regs.cycle = 0;                              // Start at cycle 0
    cpu->regs.interrupt = 0;                          // No interrupts pending
    
    // Reset interrupt cycle tracking
    for (int i = 0; i < 20; i++) {
        cpu->regs.intCycle[i].sCycle = 0;
        cpu->regs.intCycle[i].cycle = 0;
        cpu->regs.event_cycles[i] = 0;
    }
    
    // Reset timing
    cpu->regs.psxNextCounter = 0;
    cpu->regs.psxNextsCounter = 0;
    cpu->regs.next_interrupt = 0;
    cpu->regs.gteBusyCycle = 0;
    cpu->regs.muldivBusyCycle = 0;
    cpu->regs.subCycle = 0;
    cpu->regs.subCycleStep = 0;
    
    // Reset CPU state
    cpu->regs.biuReg = 0;
    cpu->regs.stop = 0;
    cpu->regs.branchSeen = 0;
    cpu->regs.branching = ZS_CPU_BRANCH_NONE_OR_EXCEPTION;
    cpu->regs.dloadSel = 0;
    
    // Reset delay slot handling
    memset(cpu->regs.dloadReg, 0, sizeof(cpu->regs.dloadReg));
    memset(cpu->regs.dloadVal, 0, sizeof(cpu->regs.dloadVal));
    
    // Reset advanced features
    cpu->regs.biosBranchCheck = 0;
    cpu->regs.cpuInRecursion = 0;
    cpu->regs.gpuIdleAfter = 0;
    
    // Reset state
    cpu->state.running = ZS_FALSE;
    cpu->state.halted = ZS_FALSE;
    cpu->state.in_exception = ZS_FALSE;
    cpu->state.in_interrupt = ZS_FALSE;
    cpu->state.current_cycles = 0;
    cpu->state.total_cycles = 0;
    cpu->state.instruction_count = 0;
    
    // Reset pipeline state
    cpu->delay_slot = ZS_FALSE;
    cpu->delay_slot_pc = 0;
    cpu->branch_taken = ZS_FALSE;
    
    // Clear cache
    memset(cpu->icache_valid, 0, sizeof(cpu->icache_valid));
    
    // Reset performance counters
    cpu->instructions_executed = 0;
    cpu->branches_taken = 0;
    cpu->cache_hits = 0;
    cpu->cache_misses = 0;
    
    ZS_LOG_INFO("CPU reset complete with comprehensive register reset");
    return ZS_SUCCESS;
}

// CPU execution
zs_error_t zs_cpu_run_cycles(zs_cpu_t* cpu, zs_u32 cycles) {
    if (cpu == NULL || !cpu->state.initialized) {
        return ZS_ERROR_INVALID_PARAMETER;
    }
    
    if (cpu->state.halted) {
        return ZS_SUCCESS;
    }
    
    zs_u32 cycles_executed = 0;
    zs_error_t result;
    
    while (cycles_executed < cycles) {
        // Execute one instruction
        result = zs_cpu_step_instruction(cpu);
        if (result != ZS_SUCCESS) {
            return result;
        }
        
        // Count cycles (simplified - each instruction takes 1 cycle)
        cycles_executed++;
        cpu->state.current_cycles++;
        cpu->state.total_cycles++;
    }
    
    return ZS_SUCCESS;
}

zs_error_t zs_cpu_step_instruction(zs_cpu_t* cpu) {
    if (cpu == NULL || !cpu->state.initialized) {
        return ZS_ERROR_INVALID_PARAMETER;
    }
    
    if (cpu->state.halted) {
        return ZS_SUCCESS;
    }
    
    // Fetch instruction (unused for now, but will be used when implementing instruction execution)
    ZS_UNUSED(zs_cpu_read_word(cpu, cpu->regs.pc));
    
    // Execute instruction
    zs_error_t result = zs_cpu_execute_instruction(cpu);
    if (result != ZS_SUCCESS) {
        return result;
    }
    
    // Update instruction counter
    cpu->state.instruction_count++;
    cpu->instructions_executed++;
    
    return ZS_SUCCESS;
}

zs_error_t zs_cpu_execute_instruction(zs_cpu_t* cpu) {
    // This is a placeholder - actual instruction execution will be implemented
    // in cpu_instructions.c with a full MIPS R3000A instruction set
    
    // For now, just advance PC
    cpu->regs.pc += 4;
    
    return ZS_SUCCESS;
}

// Register access functions
zs_u32 zs_cpu_read_register(zs_cpu_t* cpu, zs_u8 reg) {
    if (cpu == NULL || !cpu->state.initialized) {
        ZS_LOG_ERROR("Invalid CPU or CPU not initialized");
        return 0;
    }
    
    // Handle GPR registers (R0-R31, LO, HI)
    if (reg < 34) {
        return cpu->regs.gpr.r[reg];
    }
    
    ZS_LOG_WARN("Invalid register number: %d", reg);
    return 0;
}

zs_error_t zs_cpu_write_register(zs_cpu_t* cpu, zs_u8 reg, zs_u32 value) {
    if (cpu == NULL || !cpu->state.initialized) {
        return ZS_ERROR_INVALID_PARAMETER;
    }
    
    // Handle GPR registers (R0-R31, LO, HI)
    if (reg < 34) {
        // R0 is always zero - writing to it has no effect
        if (reg == 0) {
            return ZS_SUCCESS;
        }
        cpu->regs.gpr.r[reg] = value;
        return ZS_SUCCESS;
    }
    
    ZS_LOG_WARN("Invalid register number: %d", reg);
    return ZS_ERROR_INVALID_PARAMETER;
}

zs_error_t zs_cpu_get_registers(zs_cpu_t* cpu, zs_cpu_registers_t* regs) {
    if (cpu == NULL || !cpu->state.initialized || regs == NULL) {
        return ZS_ERROR_INVALID_PARAMETER;
    }
    
    // Copy the entire register structure
    memcpy(regs, &cpu->regs, sizeof(zs_cpu_registers_t));
    return ZS_SUCCESS;
}

zs_error_t zs_cpu_set_registers(zs_cpu_t* cpu, const zs_cpu_registers_t* regs) {
    if (cpu == NULL || !cpu->state.initialized || regs == NULL) {
        return ZS_ERROR_INVALID_PARAMETER;
    }
    
    // Copy the entire register structure
    memcpy(&cpu->regs, regs, sizeof(zs_cpu_registers_t));
    return ZS_SUCCESS;
}

// Memory access (delegated to memory system)
zs_error_t zs_cpu_read_memory(zs_cpu_t* cpu, zs_u32 address, zs_u8* data, zs_size_t size) {
    if (cpu == NULL || cpu->memory == NULL || data == NULL) {
        return ZS_ERROR_INVALID_PARAMETER;
    }
    
    ZS_UNUSED(address);
    ZS_UNUSED(size);
    // This will be implemented when memory system is available
    return ZS_ERROR_UNKNOWN;
}

zs_error_t zs_cpu_write_memory(zs_cpu_t* cpu, zs_u32 address, const zs_u8* data, zs_size_t size) {
    if (cpu == NULL || cpu->memory == NULL || data == NULL) {
        return ZS_ERROR_INVALID_PARAMETER;
    }
    
    ZS_UNUSED(address);
    ZS_UNUSED(size);
    // This will be implemented when memory system is available
    return ZS_ERROR_UNKNOWN;
}

zs_u8 zs_cpu_read_byte(zs_cpu_t* cpu, zs_u32 address) {
    zs_u8 value = 0;
    zs_cpu_read_memory(cpu, address, &value, 1);
    return value;
}

zs_u16 zs_cpu_read_halfword(zs_cpu_t* cpu, zs_u32 address) {
    zs_u8 data[2];
    zs_cpu_read_memory(cpu, address, data, 2);
    return (data[0] << 8) | data[1]; // Big-endian
}

zs_u32 zs_cpu_read_word(zs_cpu_t* cpu, zs_u32 address) {
    zs_u8 data[4];
    zs_cpu_read_memory(cpu, address, data, 4);
    return (data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3]; // Big-endian
}

zs_error_t zs_cpu_write_byte(zs_cpu_t* cpu, zs_u32 address, zs_u8 value) {
    return zs_cpu_write_memory(cpu, address, &value, 1);
}

zs_error_t zs_cpu_write_halfword(zs_cpu_t* cpu, zs_u32 address, zs_u16 value) {
    zs_u8 data[2] = {(zs_u8)(value >> 8), (zs_u8)(value & 0xFF)}; // Big-endian
    return zs_cpu_write_memory(cpu, address, data, 2);
}

zs_error_t zs_cpu_write_word(zs_cpu_t* cpu, zs_u32 address, zs_u32 value) {
    zs_u8 data[4] = {
        (zs_u8)(value >> 24),
        (zs_u8)((value >> 16) & 0xFF),
        (zs_u8)((value >> 8) & 0xFF),
        (zs_u8)(value & 0xFF)
    }; // Big-endian
    return zs_cpu_write_memory(cpu, address, data, 4);
}

// Control functions
zs_error_t zs_cpu_start(zs_cpu_t* cpu) {
    if (cpu == NULL || !cpu->state.initialized) {
        return ZS_ERROR_INVALID_PARAMETER;
    }
    
    cpu->state.running = ZS_TRUE;
    cpu->state.halted = ZS_FALSE;
    return ZS_SUCCESS;
}

zs_error_t zs_cpu_stop(zs_cpu_t* cpu) {
    if (cpu == NULL || !cpu->state.initialized) {
        return ZS_ERROR_INVALID_PARAMETER;
    }
    
    cpu->state.running = ZS_FALSE;
    return ZS_SUCCESS;
}

zs_error_t zs_cpu_halt(zs_cpu_t* cpu) {
    if (cpu == NULL || !cpu->state.initialized) {
        return ZS_ERROR_INVALID_PARAMETER;
    }
    
    cpu->state.halted = ZS_TRUE;
    cpu->state.running = ZS_FALSE;
    return ZS_SUCCESS;
}

zs_error_t zs_cpu_resume(zs_cpu_t* cpu) {
    if (cpu == NULL || !cpu->state.initialized) {
        return ZS_ERROR_INVALID_PARAMETER;
    }
    
    cpu->state.halted = ZS_FALSE;
    return ZS_SUCCESS;
}

// Status queries
zs_bool zs_cpu_is_initialized(const zs_cpu_t* cpu) {
    return (cpu != NULL) ? cpu->state.initialized : ZS_FALSE;
}

zs_bool zs_cpu_is_running(const zs_cpu_t* cpu) {
    return (cpu != NULL && cpu->state.initialized) ? cpu->state.running : ZS_FALSE;
}

zs_bool zs_cpu_is_halted(const zs_cpu_t* cpu) {
    return (cpu != NULL && cpu->state.initialized) ? cpu->state.halted : ZS_FALSE;
}

zs_u32 zs_cpu_get_pc(const zs_cpu_t* cpu) {
    return (cpu != NULL && cpu->state.initialized) ? cpu->regs.pc : 0;
}

zs_u32 zs_cpu_get_cycles(const zs_cpu_t* cpu) {
    return (cpu != NULL && cpu->state.initialized) ? cpu->state.total_cycles : 0;
}

zs_u64 zs_cpu_get_instruction_count(const zs_cpu_t* cpu) {
    return (cpu != NULL && cpu->state.initialized) ? cpu->instructions_executed : 0;
}

// Performance monitoring
zs_u64 zs_cpu_get_instructions_executed(const zs_cpu_t* cpu) {
    return (cpu != NULL && cpu->state.initialized) ? cpu->instructions_executed : 0;
}

zs_u64 zs_cpu_get_branches_taken(const zs_cpu_t* cpu) {
    return (cpu != NULL && cpu->state.initialized) ? cpu->branches_taken : 0;
}

zs_u64 zs_cpu_get_cache_hits(const zs_cpu_t* cpu) {
    return (cpu != NULL && cpu->state.initialized) ? cpu->cache_hits : 0;
}

zs_u64 zs_cpu_get_cache_misses(const zs_cpu_t* cpu) {
    return (cpu != NULL && cpu->state.initialized) ? cpu->cache_misses : 0;
}

zs_error_t zs_cpu_reset_performance_counters(zs_cpu_t* cpu) {
    if (cpu == NULL || !cpu->state.initialized) {
        return ZS_ERROR_INVALID_PARAMETER;
    }
    
    cpu->instructions_executed = 0;
    cpu->branches_taken = 0;
    cpu->cache_hits = 0;
    cpu->cache_misses = 0;
    
    ZS_LOG_INFO("Performance counters reset");
    return ZS_SUCCESS;
} 