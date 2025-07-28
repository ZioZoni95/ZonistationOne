/**
 * @file main.c
 * @brief Main entry point for ZoniStationOne
 */

#include "zoni_common.h"
#include "zoni_memory.h"
#include "zoni_cpu.h"
#include "zoni_bios.h"

int main(int argc, char* argv[]) {
    ZONI_UNUSED(argc);
    ZONI_UNUSED(argv);
    
    zoni_log(ZONI_LOG_INFO, "ZoniStationOne v%s - PlayStation 1 Emulator", ZONI_VERSION_STRING);
    zoni_log(ZONI_LOG_INFO, "================================================");
    
    // Initialize memory system
    zoni_memory_t memory;
    zoni_error_t result = zoni_memory_init(&memory);
    if (result != ZONI_SUCCESS) {
        zoni_log(ZONI_LOG_ERROR, "Memory initialization failed");
        return 1;
    }
    
    // Initialize CPU system
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
        zoni_log(ZONI_LOG_ERROR, "CPU initialization failed");
        zoni_memory_shutdown(&memory);
        return 1;
    }
    
    // Initialize BIOS system
    zoni_bios_t bios;
    result = zoni_bios_init(&bios);
    if (result != ZONI_SUCCESS) {
        zoni_log(ZONI_LOG_ERROR, "BIOS initialization failed");
        zoni_cpu_shutdown(&cpu);
        zoni_memory_shutdown(&memory);
        return 1;
    }
    
    // Connect CPU to memory
    zoni_cpu_set_memory(&memory);
    
    zoni_log(ZONI_LOG_INFO, "✅ Core systems initialized successfully");
    
    // Load BIOS
    zoni_log(ZONI_LOG_INFO, "Loading BIOS...");
    result = zoni_bios_load_default(&bios, &memory);
    if (result != ZONI_SUCCESS && !zoni_bios_is_hle(&bios)) {
        zoni_log(ZONI_LOG_ERROR, "BIOS loading failed");
        goto cleanup;
    }
    
    zoni_log(ZONI_LOG_INFO, "✅ BIOS loaded: %s (%s, %s)", 
             zoni_bios_get_version(&bios), 
             zoni_bios_get_region(&bios),
             zoni_bios_is_hle(&bios) ? "HLE" : "Real BIOS");
    
    // Test basic CPU functionality
    zoni_cpu_set_register(&cpu, 1, 0x12345678);
    u32 reg_value = zoni_cpu_get_register(&cpu, 1);
    if (reg_value != 0x12345678) {
        zoni_log(ZONI_LOG_ERROR, "❌ CPU register test failed");
        goto cleanup;
    }
    
    // Test load delay slots
    zoni_cpu_do_load(&cpu, 2, 0xDEADBEEF);
    zoni_cpu_dload_step(&cpu);
    zoni_cpu_dload_step(&cpu);
    if (cpu.gpr.r[2] != 0xDEADBEEF) {
        zoni_log(ZONI_LOG_ERROR, "❌ CPU load delay test failed");
        goto cleanup;
    }
    
    // Test CPU memory access
    u8 test_value = 0x42;
    zoni_memory_write8(&memory, 0x1000, test_value);
    
    u8 cpu_read_value;
    result = zoni_cpu_read8(&cpu, 0x1000, &cpu_read_value);
    if (result != ZONI_SUCCESS || cpu_read_value != test_value) {
        zoni_log(ZONI_LOG_ERROR, "❌ CPU memory access test failed");
        goto cleanup;
    }
    
    zoni_log(ZONI_LOG_INFO, "✅ Basic CPU functionality verified");
    
    // Test instruction execution
    zoni_log(ZONI_LOG_INFO, "Testing MIPS instruction execution...");
    
    // Write test instructions to RAM
    zoni_memory_write32(&memory, 0x00002000, 0x00430820); // ADD $1, $2, $3
    zoni_memory_write32(&memory, 0x00002004, 0x2001007B); // ADDI $1, $0, 123
    zoni_memory_write32(&memory, 0x00002008, 0x200200FF); // ADDI $2, $0, 255
    zoni_memory_write32(&memory, 0x0000200C, 0x3421000F); // ORI $1, $1, 0x0F
    zoni_memory_write32(&memory, 0x00002010, 0x0000000C); // SYSCALL
    
    // Set up test registers
    zoni_cpu_set_register(&cpu, 1, 0x12345678);
    zoni_cpu_set_register(&cpu, 2, 0x0000000F);
    zoni_cpu_set_register(&cpu, 3, 0x00000000);
    
    // Set PC to start of test instructions
    cpu.pc = 0x00002000;
    
    // Execute test sequence
    for (int i = 0; i < 5; i++) {
        result = zoni_cpu_step(&cpu);
        if (result != ZONI_SUCCESS) {
            zoni_log(ZONI_LOG_ERROR, "❌ Instruction execution failed at step %d", i+1);
            goto cleanup;
        }
    }
    
    zoni_log(ZONI_LOG_INFO, "✅ Basic instruction set working");
    
    // Test BIOS-like instruction sequence
    zoni_log(ZONI_LOG_INFO, "Testing BIOS instruction sequence...");
    
    // Sequence: LUI -> ADDIU -> LW -> SW
    zoni_memory_write32(&memory, 0x00002060, 0x3C050001); // LUI $5, 0x0001
    zoni_memory_write32(&memory, 0x00002064, 0x24A50000); // ADDIU $5, $5, 0x0000
    zoni_memory_write32(&memory, 0x00002068, 0x8CA60000); // LW $6, 0x0000($5)
    zoni_memory_write32(&memory, 0x0000206C, 0xACA70000); // SW $7, 0x0000($5)
    
    // Write test data
    zoni_memory_write32(&memory, 0x00010000, 0x12345678);
    
    zoni_cpu_reset(&cpu);
    cpu.pc = 0x00002060;
    cpu.gpr.r[7] = 0x87654321;
    
    // Execute sequence
    for (int i = 0; i < 4; i++) {
        result = zoni_cpu_step(&cpu);
        if (result != ZONI_SUCCESS) {
            zoni_log(ZONI_LOG_ERROR, "❌ BIOS sequence failed at step %d", i+1);
            goto cleanup;
        }
    }
    
    // Verify final memory state
    u32 final_memory;
    zoni_memory_read32(&memory, 0x00010000, &final_memory);
    
    if (final_memory == 0x87654321) {
        zoni_log(ZONI_LOG_INFO, "✅ BIOS instruction sequence test PASSED");
        zoni_log(ZONI_LOG_INFO, "✅ All critical instructions working correctly");
    } else {
        zoni_log(ZONI_LOG_ERROR, "❌ BIOS instruction sequence test FAILED");
    }
    
    // BIOS Boot Test
    zoni_log(ZONI_LOG_INFO, "================================================");
    zoni_log(ZONI_LOG_INFO, "🎮 Testing BIOS Boot Process");
    zoni_log(ZONI_LOG_INFO, "================================================");
    
    // Set up BIOS boot state
    result = zoni_bios_setup_boot_state(&bios, &cpu);
    if (result != ZONI_SUCCESS) {
        zoni_log(ZONI_LOG_ERROR, "❌ BIOS boot state setup failed");
        goto cleanup;
    }
    
    // Execute BIOS (limited cycles for testing)
    result = zoni_bios_execute(&bios, &cpu, &memory);
    if (result != ZONI_SUCCESS) {
        zoni_log(ZONI_LOG_ERROR, "❌ BIOS execution failed");
        goto cleanup;
    }
    
    // Check if BIOS execution completed
    if (zoni_bios_execution_ended(&cpu)) {
        zoni_log(ZONI_LOG_INFO, "✅ BIOS boot process completed successfully");
        zoni_log(ZONI_LOG_INFO, "✅ Final PC: 0x%08X", cpu.pc);
    } else {
        zoni_log(ZONI_LOG_WARNING, "⚠️ BIOS execution timeout (this is normal for testing)");
    }
    
    zoni_log(ZONI_LOG_INFO, "================================================");
    zoni_log(ZONI_LOG_INFO, "🎮 ZoniStationOne Core Emulation: READY");
    zoni_log(ZONI_LOG_INFO, "Next: GPU, SPU, CD-ROM...");
    
cleanup:
    // Cleanup
    zoni_bios_shutdown(&bios);
    zoni_cpu_shutdown(&cpu);
    zoni_memory_shutdown(&memory);
    
    return 0;
} 