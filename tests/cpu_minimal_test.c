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
    
    printf("\n");

    // Test 3: CAUSE Register for SYSCALL
    printf("TEST 3: CAUSE Register for SYSCALL\n");
    cpu_init(&cpu, &inter);
    cpu.pc = 0x0;
    cpu.next_pc = 0x4;
    ram_store32(&ram, 0, 0x0000000C); // SYSCALL
    cpu_run_next_instruction(&cpu);
    printf("CAUSE after SYSCALL: 0x%08X (Expected: 0x00000020)\n", cpu.cause);
    if ((cpu.cause & 0x7C) == 0x20) { // 0x20 == 0x08 << 2
        printf("\u2705 PASS: CAUSE register set correctly for SYSCALL\n");
    } else {
        printf("\u274C FAIL: CAUSE register not set correctly\n");
    }
    printf("\n");

    // Test 4: BD flag/EPC for branch delay exception
    printf("TEST 4: BD flag/EPC for branch delay exception\n");
    cpu_init(&cpu, &inter);
    // Place BEQ (branch if equal) at 0x0, target 0x8, and SYSCALL at 0x4
    ram_store32(&ram, 0, 0x10840002); // beq $a0, $a0, +2 (to 0xC)
    ram_store32(&ram, 4, 0x0000000C); // SYSCALL
    ram_store32(&ram, 8, 0x00000000); // NOP
    ram_store32(&ram, 12, 0x00000000); // NOP
    cpu.regs[4] = 1; // $a0 == $a0
    cpu.pc = 0x0;
    cpu.next_pc = 0x4;
    cpu_run_next_instruction(&cpu); // BEQ
    cpu_run_next_instruction(&cpu); // SYSCALL in branch delay
    // After SYSCALL in branch delay, BD should be set, EPC should be 0x4
    printf("EPC: 0x%08X (Expected: 0x4), BD: %d (Expected: 1)\n", cpu.epc, (cpu.cause >> 31) & 1);
    if (cpu.epc == 0x4 && ((cpu.cause >> 31) & 1)) {
        printf("\u2705 PASS: BD flag/EPC correct for branch delay exception\n");
    } else {
        printf("\u274C FAIL: BD flag/EPC incorrect\n");
    }
    printf("\n");

    // Test 5: Interrupt delivery and return
    printf("TEST 5: Interrupt delivery and return\n");
    cpu_init(&cpu, &inter);
    cpu.pc = 0x0;
    cpu.next_pc = 0x4;
    cpu.sr = 0x1; // Enable interrupts
    inter.irq_status = 0x1; // IRQ0 pending
    inter.irq_mask = 0x1;   // IRQ0 enabled
    ram_store32(&ram, 0, 0x00000000); // NOP
    cpu_run_next_instruction(&cpu); // Should take interrupt
    printf("PC after IRQ: 0x%08X (Expected: 0x80000080)\n", cpu.pc);
    if (cpu.pc == 0x80000080) {
        printf("\u2705 PASS: Interrupt delivered to exception vector\n");
    } else {
        printf("\u274C FAIL: Interrupt not delivered\n");
    }
    printf("Interrupt test complete. Continuing to next tests...\n");
    printf("Re-initializing before Test 6...\n");
    ram_init(&ram);
    interconnect_init(&inter, NULL, &ram);
    cpu_init(&cpu, &inter);
    printf("Starting Test 6: Unaligned access (address error)\n");

    // Test 6: Unaligned access (address error)
    cpu_init(&cpu, &inter);
    cpu.pc = 0x0;
    cpu.next_pc = 0x4;
    ram_store32(&ram, 0, 0x84450001); // lh $a1, 1($v0) (unaligned)
    cpu.regs[2] = 0x3; // $v0 = 0x3 (unaligned)
    cpu_run_next_instruction(&cpu);
    printf("PC after unaligned access: 0x%08X (Expected: 0x80000080)\n", cpu.pc);
    if (cpu.pc == 0x80000080) {
        printf("\u2705 PASS: Address error exception taken\n");
    } else {
        printf("\u274C FAIL: Address error not taken\n");
    }
    printf("\n");

    // Test 7: BREAK opcode (exception vector 0x80000080)\n");
    printf("TEST 7: BREAK opcode (exception vector 0x80000080)\n");
    cpu_init(&cpu, &inter);
    cpu.pc = 0x0;
    cpu.next_pc = 0x4;
    ram_store32(&ram, 0, 0x0000000D); // BREAK
    cpu_run_next_instruction(&cpu);
    printf("PC after BREAK: 0x%08X (Expected: 0x80000080)\n", cpu.pc);
    if (cpu.pc == 0x80000080) {
        printf("\u2705 PASS: BREAK exception taken\n");
    } else {
        printf("\u274C FAIL: BREAK exception not taken\n");
    }
    printf("\n");

    // Test 8: Nested exception (trigger exception in handler)
    printf("TEST 8: Nested exception (trigger exception in handler)\n");
    cpu_init(&cpu, &inter);
    cpu.pc = 0x0;
    cpu.next_pc = 0x4;
    ram_store32(&ram, 0, 0x0000000C); // SYSCALL
    cpu_run_next_instruction(&cpu); // Take exception
    // Now, while in exception handler, trigger another exception
    cpu.pc = 0x80000080;
    cpu.next_pc = 0x80000084;
    ram_store32(&ram, 0x80000080, 0x0000000C); // SYSCALL in handler
    cpu_run_next_instruction(&cpu);
    printf("PC after nested exception: 0x%08X (Expected: 0x80000080)\n", cpu.pc);
    if (cpu.pc == 0x80000080) {
        printf("\u2705 PASS: Nested exception handled\n");
    } else {
        printf("\u274C FAIL: Nested exception not handled\n");
    }
    printf("\n");

    // Test 9: GTE/branch delay note (stub)
    printf("TEST 9: GTE/branch delay (stub)\n");
    printf("(GTE not implemented, test skipped)\n");
    printf("\n");

    // Test 10: BadVaddr (Address Error)
    printf("TEST 10: BadVaddr (Address Error)\n");
    cpu_init(&cpu, &inter);
    cpu.pc = 0x0;
    cpu.next_pc = 0x4;
    ram_store32(&ram, 0, 0x84450001); // lh $a1, 1($v0) (unaligned)
    cpu.regs[2] = 0x3; // $v0 = 0x3 (unaligned)
    cpu_run_next_instruction(&cpu);
    // BadVaddr is not implemented in struct, so just print a stub result
    printf("(Stub) Would check BadVaddr == 0x3 for address error\n");
    printf("\n");

    // Test 11: COP0 Breakpoint Exception (jump to 0x80000040, EXCODE=0x09)
    printf("TEST 11: COP0 Breakpoint Exception (jump to 0x80000040, EXCODE=0x09)\n");
    cpu_init(&cpu, &inter);
    cpu.pc = 0x0;
    cpu.next_pc = 0x4;
    // Simulate a COP0 breakpoint exception (not implemented, so stub)
    printf("(Stub) Would set BPC/BPCM and check PC=0x80000040, CAUSE EXCODE=0x09\n");
    printf("\n");

    // Test 12: GTE Interrupt Quirk (stub)
    printf("TEST 12: GTE Interrupt Quirk (stub)\n");
    printf("(Stub) Would trigger interrupt on GTE command and check EPC incremented by 4\n");
    printf("\n");

    // Test 13: Illegal/Unknown Opcode
    printf("TEST 13: Illegal/Unknown Opcode\n");
    cpu_init(&cpu, &inter);
    cpu.pc = 0x0;
    cpu.next_pc = 0x4;
    ram_store32(&ram, 0, 0xFFFFFFFF); // Illegal opcode
    cpu_run_next_instruction(&cpu);
    printf("PC after illegal opcode: 0x%08X (Expected: 0x80000080)\n", cpu.pc);
    if (cpu.pc == 0x80000080) {
        printf("\u2705 PASS: Illegal opcode exception taken\n");
    } else {
        printf("\u274C FAIL: Illegal opcode exception not taken\n");
    }
    printf("\n");

    // Test 14: Coprocessor Unusable Exception (COP1, COP3)
    printf("TEST 14: Coprocessor Unusable Exception (COP1, COP3)\n");
    cpu_init(&cpu, &inter);
    cpu.pc = 0x0;
    cpu.next_pc = 0x4;
    ram_store32(&ram, 0, 0x41000000); // COP1 opcode (FPU, not present)
    cpu_run_next_instruction(&cpu);
    printf("PC after COP1: 0x%08X (Expected: 0x80000080)\n", cpu.pc);
    if (cpu.pc == 0x80000080) {
        printf("\u2705 PASS: COP1 unusable exception taken\n");
    } else {
        printf("\u274C FAIL: COP1 unusable exception not taken\n");
    }
    cpu_init(&cpu, &inter);
    cpu.pc = 0x0;
    cpu.next_pc = 0x4;
    ram_store32(&ram, 0, 0x4B000000); // COP3 opcode (not present)
    cpu_run_next_instruction(&cpu);
    printf("PC after COP3: 0x%08X (Expected: 0x80000080)\n", cpu.pc);
    if (cpu.pc == 0x80000080) {
        printf("\u2705 PASS: COP3 unusable exception taken\n");
    } else {
        printf("\u274C FAIL: COP3 unusable exception not taken\n");
    }
    printf("\n=== ALL TESTS (nocash/PSX-Spex) COMPLETE ===\n");

    return 0;
} 