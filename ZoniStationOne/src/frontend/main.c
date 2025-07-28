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
    
    // Test instruction fetch and decode
    zoni_log(ZONI_LOG_INFO, "Testing instruction fetch and decode...");
    
    // Write test instructions to RAM (which is writable)
    zoni_memory_write32(&memory, 0x00001000, 0x00000000); // NOP
    zoni_memory_write32(&memory, 0x00001004, 0x2001007B); // ADDI $1, $0, 123
    zoni_memory_write32(&memory, 0x00001008, 0x200200FF); // ADDI $2, $0, 255
    zoni_memory_write32(&memory, 0x0000100C, 0x00430820); // ADD $1, $2, $3
    
    // Debug: Print the actual instruction values
    zoni_log(ZONI_LOG_DEBUG, "Written instructions:");
    zoni_log(ZONI_LOG_DEBUG, "  0x00001000: 0x%08X", 0x00000000);
    zoni_log(ZONI_LOG_DEBUG, "  0x00001004: 0x%08X", 0x2001007B);
    zoni_log(ZONI_LOG_DEBUG, "  0x00001008: 0x%08X", 0x200200FF);
    zoni_log(ZONI_LOG_DEBUG, "  0x0000100C: 0x%08X", 0x00430820);
    
    // Set PC to RAM address
    cpu.pc = 0x00001000;
    
    // Fetch and decode the first instruction (NOP)
    zoni_instruction_t instruction;
    result = zoni_cpu_fetch_instruction(&cpu, &instruction);
    if (result == ZONI_SUCCESS) {
        zoni_log(ZONI_LOG_INFO, "Instruction fetch successful");
        zoni_log(ZONI_LOG_DEBUG, "Fetched instruction: 0x%08X", instruction.raw);
        
        // Decode the instruction
        char disasm[256];
        result = zoni_cpu_decode_instruction(&instruction, disasm, sizeof(disasm));
        if (result == ZONI_SUCCESS) {
            zoni_log(ZONI_LOG_INFO, "Instruction decode: %s", disasm);
            zoni_log(ZONI_LOG_DEBUG, "Raw instruction: 0x%08X, Opcode: 0x%02X", 
                     instruction.raw, instruction.r.opcode);
            u32 big_endian = zoni_instruction_to_big_endian(instruction.raw);
            zoni_log(ZONI_LOG_DEBUG, "Big-endian instruction: 0x%08X", big_endian);
            zoni_log(ZONI_LOG_DEBUG, "Expected ADDI: 0x2001007B -> 0x7B010020");
            zoni_log(ZONI_LOG_DEBUG, "Expected ADD: 0x00430820 -> 0x20084300");
        } else {
            zoni_log(ZONI_LOG_ERROR, "Instruction decode failed");
        }
    } else {
        zoni_log(ZONI_LOG_ERROR, "Instruction fetch failed");
    }
    
    // Test with ADDI instruction
    cpu.pc = 0x00001004;
    result = zoni_cpu_fetch_instruction(&cpu, &instruction);
    if (result == ZONI_SUCCESS) {
        char disasm[256];
        result = zoni_cpu_decode_instruction(&instruction, disasm, sizeof(disasm));
        if (result == ZONI_SUCCESS) {
            zoni_log(ZONI_LOG_INFO, "ADDI instruction decode: %s", disasm);
            u32 big_endian = zoni_instruction_to_big_endian(instruction.raw);
            zoni_log(ZONI_LOG_DEBUG, "Raw instruction: 0x%08X, Opcode: 0x%02X", 
                     instruction.raw, instruction.r.opcode);
            zoni_log(ZONI_LOG_DEBUG, "Big-endian instruction: 0x%08X", big_endian);
        }
    }
    
    // Test with ADD instruction
    cpu.pc = 0x0000100C;
    result = zoni_cpu_fetch_instruction(&cpu, &instruction);
    if (result == ZONI_SUCCESS) {
        char disasm[256];
        result = zoni_cpu_decode_instruction(&instruction, disasm, sizeof(disasm));
        if (result == ZONI_SUCCESS) {
            zoni_log(ZONI_LOG_INFO, "ADD instruction decode: %s", disasm);
            u32 big_endian = zoni_instruction_to_big_endian(instruction.raw);
            zoni_log(ZONI_LOG_DEBUG, "Raw instruction: 0x%08X, Opcode: 0x%02X", 
                     instruction.raw, instruction.r.opcode);
            zoni_log(ZONI_LOG_DEBUG, "Big-endian instruction: 0x%08X", big_endian);
        }
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