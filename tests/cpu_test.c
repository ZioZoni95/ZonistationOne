#include "cpu.h"
#include "ram.h"
#include "interconnect.h"
#include <stdio.h>
#include <string.h>

// PS1 RAM is 2MB, mapped at 0x00000000
// BIOS exception vector is at 0x80000080 (KSEG0), which maps to 0x00000080 in RAM
// Reset vector is 0xbfc00000 (BIOS ROM), but for this test, we simulate it in RAM

// Minimal stub for BIOS exception handler at 0x80000080
uint32_t fake_bios[4] = {
    0x42000018, // COP0: RFE (ERET) instruction
    0x00000000, // NOP
    0x00000000, // NOP
    0x00000000  // NOP
};

int main() {
    Ram ram;
    Interconnect inter;
    Cpu cpu;

    // Initialize RAM and Interconnect
    ram_init(&ram);
    interconnect_init(&inter, NULL, &ram);

    // Patch fake BIOS handler at 0x00000080 (physical address for 0x80000080)
    memcpy(&ram.data[0x80 / 4], fake_bios, sizeof(fake_bios));

    // Place a SYSCALL instruction at 0x0
    ram.data[0] = 0x0000000C; // SYSCALL
    ram.data[1] = 0x00000000; // NOP
    ram.data[2] = 0x00000000; // NOP
    ram.data[3] = 0x00000000; // NOP

    // Initialize CPU
    cpu_init(&cpu, &inter);
    cpu.pc = 0x0;

    printf("[TEST] Starting CPU at 0x%08X\n", cpu.pc);
    for (int i = 0; i < 10; ++i) {
        printf("[TEST] Step %d: PC=0x%08X\n", i, cpu.pc);
        cpu_run_next_instruction(&cpu);
        // Log exception state
        printf("[TEST] After step: PC=0x%08X, SR=0x%08X, EPC=0x%08X\n", cpu.pc, cpu.sr, cpu.epc);
    }
    printf("[TEST] Done.\n");
    return 0;
} 