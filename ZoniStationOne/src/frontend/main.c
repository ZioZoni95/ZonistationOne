/**
 * @file main.c
 * @brief Main entry point for ZoniStationOne
 */

#include "zoni_common.h"
#include "zoni_memory.h"
#include "zoni_cpu.h"
#include "zoni_emulator.h"

int main(int argc, char* argv[]) {
    ZONI_UNUSED(argc);
    ZONI_UNUSED(argv);
    
    zoni_log(ZONI_LOG_INFO, "ZoniStationOne v%s starting up...", ZONI_VERSION_STRING);
    
    // Test memory system
    zoni_memory_t memory;
    zoni_error_t result = zoni_memory_init(&memory);
    if (result != ZONI_SUCCESS) {
        zoni_log(ZONI_LOG_ERROR, "Failed to initialize memory system: %s", 
                 zoni_error_to_string(result));
        return 1;
    }
    
    zoni_log(ZONI_LOG_INFO, "Memory system initialized successfully");
    
    // Test memory access
    u8 test_value = 0x42;
    zoni_memory_write8(&memory, 0x1000, test_value);
    
    u8 read_value;
    result = zoni_memory_read8(&memory, 0x1000, &read_value);
    if (result == ZONI_SUCCESS && read_value == test_value) {
        zoni_log(ZONI_LOG_INFO, "Memory read/write test passed");
    } else {
        zoni_log(ZONI_LOG_ERROR, "Memory read/write test failed");
    }
    
    // Test 32-bit memory access
    u32 test32 = 0x12345678;
    zoni_memory_write32(&memory, 0x2000, test32);
    
    u32 read32;
    result = zoni_memory_read32(&memory, 0x2000, &read32);
    if (result == ZONI_SUCCESS && read32 == test32) {
        zoni_log(ZONI_LOG_INFO, "32-bit memory access test passed");
    } else {
        zoni_log(ZONI_LOG_ERROR, "32-bit memory access test failed");
    }
    
    // Test CPU system
    zoni_cpu_regs_t cpu;
    zoni_cpu_config_t cpu_config = {
        .mode = ZONI_CPU_MODE_INTERPRETER,
        .enable_icache = true,
        .enable_dcache = true,
        .precise_exceptions = true,
        .cycle_multiplier = 100
    };
    
    result = zoni_cpu_init(&cpu, &cpu_config);
    if (result != ZONI_SUCCESS) {
        zoni_log(ZONI_LOG_ERROR, "Failed to initialize CPU: %s", 
                 zoni_error_to_string(result));
        zoni_memory_shutdown(&memory);
        return 1;
    }
    
    zoni_log(ZONI_LOG_INFO, "CPU system initialized successfully");
    
    // Connect CPU to memory
    zoni_cpu_set_memory(&memory);
    
    // Test CPU register access
    zoni_cpu_set_register(&cpu, 1, 0x12345678);
    u32 reg_value = zoni_cpu_get_register(&cpu, 1);
    if (reg_value == 0x12345678) {
        zoni_log(ZONI_LOG_INFO, "CPU register access test passed");
    } else {
        zoni_log(ZONI_LOG_ERROR, "CPU register access test failed");
    }
    
    // Test load delay slots
    zoni_cpu_do_load(&cpu, 2, 0xDEADBEEF);
    zoni_cpu_dload_step(&cpu);
    zoni_cpu_dload_step(&cpu);
    if (cpu.gpr.r[2] == 0xDEADBEEF) {
        zoni_log(ZONI_LOG_INFO, "CPU load delay test passed");
    } else {
        zoni_log(ZONI_LOG_ERROR, "CPU load delay test failed");
    }
    
    // Test CPU memory access
    u8 cpu_read_value;
    result = zoni_cpu_read8(&cpu, 0x1000, &cpu_read_value);
    if (result == ZONI_SUCCESS && cpu_read_value == test_value) {
        zoni_log(ZONI_LOG_INFO, "CPU memory access test passed");
    } else {
        zoni_log(ZONI_LOG_ERROR, "CPU memory access test failed");
    }
    
    // Dump CPU registers
    zoni_cpu_dump_registers(&cpu);
    
    // Dump memory statistics
    zoni_memory_dump_stats(&memory);
    
    // Test memory dump
    zoni_log(ZONI_LOG_INFO, "Testing memory dump:");
    zoni_memory_dump_region(&memory, PSX_MEM_RAM, 0x1000, 32);
    
    // Cleanup
    zoni_cpu_shutdown(&cpu);
    zoni_memory_shutdown(&memory);
    
    zoni_log(ZONI_LOG_INFO, "ZoniStationOne shutdown complete");
    
    return 0;
} 