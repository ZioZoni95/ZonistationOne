#include "cpu.h"
#include "ram.h"
#include "interconnect.h"
#include <stdio.h>
#include <string.h>

// Test 1: Exception Vector Jump Test
// Test 2: ERET Instruction Recognition Test

int main() {
    Ram ram;
    Interconnect inter;
    Cpu cpu;

    // Initialize RAM and Interconnect
    ram_init(&ram);
    interconnect_init(&inter, NULL, &ram);

    printf("=== CPU MINIMAL TEST ===\n\n");

    // Test 1: Exception Vector Jump
    printf("TEST 1: Exception Vector Jump\n");
    printf("Expected: SYSCALL should jump to 0x80000080\n");
    
    // Place SYSCALL at 0x0
    ram_store32(&ram, 0, 0x0000000C); // SYSCALL
    printf("Placed SYSCALL instruction 0x%08X at address 0x0\n", 0x0000000C);
    
    // Initialize CPU
    cpu_init(&cpu, &inter);
    cpu.pc = 0x0;
    cpu.next_pc = 0x4; // Set next_pc correctly
    
    // Invalidate instruction cache to ensure fresh fetch
    for (int i = 0; i < 256; ++i) {
        cpu.icache[i].tag = 0xFFFFFFFF; // Invalid tag
        for (int j = 0; j < 4; ++j) {
            cpu.icache[i].valid[j] = false;
        }
    }
    
    // Set a known syscall number in $a0 (register 4) that will NOT be handled
    cpu.regs[4] = 0xFF; // Unknown syscall number
    
    printf("Before SYSCALL: PC=0x%08X, Syscall#=0x%02X\n", cpu.pc, cpu.regs[4]);
    
    // Debug: Check what instruction will be fetched
    uint32_t instruction = interconnect_load32(&inter, 0x0);
    printf("Instruction at 0x0 (via interconnect): 0x%08X\n", instruction);
    
    // Debug: Check what instruction cache will fetch
    uint32_t cached_instruction = cpu_icache_fetch(&cpu, 0x0);
    printf("Instruction at 0x0 (via cache): 0x%08X\n", cached_instruction);
    
    cpu_run_next_instruction(&cpu);
    printf("After SYSCALL:  PC=0x%08X (Expected: 0x80000080)\n", cpu.pc);
    
    if (cpu.pc == 0x80000080) {
        printf("✅ PASS: Exception vector jump works correctly\n");
    } else {
        printf("❌ FAIL: Exception vector jump failed\n");
    }
    
    printf("\n");

    // Test 2: ERET Instruction Recognition
    printf("TEST 2: ERET Instruction Recognition\n");
    printf("Expected: 0x42000018 should be recognized as ERET, not illegal\n");
    
    // Reset CPU
    cpu_init(&cpu, &inter);
    cpu.pc = 0x0;
    cpu.next_pc = 0x4; // Set next_pc correctly
    
    // Place ERET instruction at 0x0
    ram_store32(&ram, 0, 0x42000010); // COP0: RFE (ERET) - correct encoding
    printf("Placed ERET instruction 0x%08X at address 0x0\n", 0x42000010);
    
    // Invalidate instruction cache again
    for (int i = 0; i < 256; ++i) {
        cpu.icache[i].tag = 0xFFFFFFFF;
        for (int j = 0; j < 4; ++j) {
            cpu.icache[i].valid[j] = false;
        }
    }
    
    printf("Before ERET: PC=0x%08X\n", cpu.pc);
    cpu_run_next_instruction(&cpu);
    printf("After ERET:  PC=0x%08X\n", cpu.pc);
    
    // Check if ERET was recognized (no illegal instruction exception)
    if (cpu.pc == 0x4) {
        printf("✅ PASS: ERET instruction recognized correctly\n");
    } else {
        printf("❌ FAIL: ERET instruction not recognized (treated as illegal)\n");
    }
    
    printf("\n=== TEST COMPLETE ===\n");
    return 0;
} 