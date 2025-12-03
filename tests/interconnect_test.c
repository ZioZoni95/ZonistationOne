#include "interconnect.h"
#include "ram.h"
#include "bios.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#define TEST_OK(cond, msg) \
    printf("%s: %s\n", (cond) ? "\u2705 PASS" : "\u274C FAIL", msg)

int main() {
    printf("=== INTERCONNECT TESTS (nocash/PSX-Spex compliance) ===\n\n");
    Ram ram;
    Bios bios;
    Interconnect inter;
    // Fill RAM and BIOS with known patterns
    ram_init(&ram);
    memset(&bios, 0xA5, sizeof(Bios));
    interconnect_init(&inter, &bios, &ram);

    // 1. RAM region
    interconnect_store32(&inter, 0x00000000, 0x12345678);
    TEST_OK(interconnect_load32(&inter, 0x00000000) == 0x12345678, "RAM 32-bit read/write");
    interconnect_store16(&inter, 0x00000004, 0xBEEF);
    TEST_OK(interconnect_load16(&inter, 0x00000004) == 0xBEEF, "RAM 16-bit read/write");
    interconnect_store8(&inter, 0x00000006, 0x7F);
    TEST_OK(interconnect_load8(&inter, 0x00000006) == 0x7F, "RAM 8-bit read/write");
    // Unaligned access
    TEST_OK(interconnect_load32(&inter, 0x00000002) == 0xBADBAD32, "RAM unaligned 32-bit read triggers error/garbage");

    // 2. BIOS ROM
    TEST_OK(interconnect_load8(&inter, 0x1FC00000) == 0xA5, "BIOS 8-bit read");
    TEST_OK(interconnect_load16(&inter, 0x1FC00000) == 0xA5A5, "BIOS 16-bit read");
    TEST_OK(interconnect_load32(&inter, 0x1FC00000) == 0xA5A5A5A5, "BIOS 32-bit read");
    interconnect_store8(&inter, 0x1FC00000, 0xFF); // Should not change
    TEST_OK(interconnect_load8(&inter, 0x1FC00000) == 0xA5, "BIOS write ignored");
    TEST_OK(interconnect_load8(&inter, 0x1FC80000) == 0, "BIOS out-of-bounds returns 0");

    // 3. VRAM region (stubbed)
    TEST_OK(interconnect_load32(&inter, 0x1F000000) == 0, "VRAM 32-bit read stubbed");
    interconnect_store32(&inter, 0x1F000000, 0xDEADBEEF); // Should not crash
    printf("\u2705 PASS: VRAM 32-bit write stubbed (no crash)\n");

    // 4. Expansion 1/2
    TEST_OK(interconnect_load32(&inter, 0x1F000000) == 0xFFFFFFFF, "Expansion 1 32-bit read");
    TEST_OK(interconnect_load16(&inter, 0x1F000000) == 0xFFFF, "Expansion 1 16-bit read");
    TEST_OK(interconnect_load8(&inter, 0x1F000000) == 0xFF, "Expansion 1 8-bit read");
    interconnect_store32(&inter, 0x1F000000, 0xCAFEBABE); // Should not crash
    printf("\u2705 PASS: Expansion 1 32-bit write ignored (no crash)\n");

    // 5. I/O ports (GPU, CDROM, timers, etc.)
    // Just test that mapped addresses do not crash and return stub values
    TEST_OK(interconnect_load32(&inter, 0x1F801810) == 0, "GPU GPUREAD 32-bit read stubbed");
    interconnect_store32(&inter, 0x1F801810, 0x12345678); // GP0
    printf("\u2705 PASS: GPU GP0 32-bit write stubbed (no crash)\n");
    // Timers
    TEST_OK(interconnect_load32(&inter, 0x1F801100) == 0, "Timer0 32-bit read stubbed");
    interconnect_store32(&inter, 0x1F801100, 0x1111); // Should not crash
    printf("\u2705 PASS: Timer0 32-bit write stubbed (no crash)\n");

    // 6. KSEG2/KSEG3
    TEST_OK(interconnect_load32(&inter, 0xC0000000) == 0, "KSEG2 unmapped read returns 0");
    interconnect_store32(&inter, 0xC0000000, 0xDEAD); // Should not crash
    printf("\u2705 PASS: KSEG2 write ignored (no crash)\n");

    // 7. Unmapped/invalid
    TEST_OK(interconnect_load32(&inter, 0x20000000) == 0, "Unmapped region read returns 0");
    interconnect_store32(&inter, 0x20000000, 0xBEEF); // Should not crash
    printf("\u2705 PASS: Unmapped region write ignored (no crash)\n");

    // 8. Alignment
    TEST_OK(interconnect_load16(&inter, 0x00000001) == 0xBADB, "Unaligned 16-bit read triggers error/garbage");
    interconnect_store16(&inter, 0x00000001, 0xBEEF); // Should not crash
    printf("\u2705 PASS: Unaligned 16-bit write handled (no crash)\n");

    // 9. Mirroring
    interconnect_store32(&inter, 0x00000000, 0xCAFEBABE);
    TEST_OK(interconnect_load32(&inter, 0x80000000) == 0xCAFEBABE, "KSEG0 mirrors RAM");
    TEST_OK(interconnect_load32(&inter, 0xA0000000) == 0xCAFEBABE, "KSEG1 mirrors RAM");
    // BIOS mirroring
    TEST_OK(interconnect_load32(&inter, 0x9FC00000) == 0xA5A5A5A5, "KSEG0 mirrors BIOS");
    TEST_OK(interconnect_load32(&inter, 0xBFC00000) == 0xA5A5A5A5, "KSEG1 mirrors BIOS");

    // 10. Side effects: I_STAT
    inter.irq_status = 0x3;
    TEST_OK(interconnect_load16(&inter, 0x1F801070) == 0x3, "I_STAT read returns IRQ status");
    interconnect_store16(&inter, 0x1F801070, 0x1); // Acknowledge IRQ0
    TEST_OK(inter.irq_status == 0x2, "I_STAT write acknowledges IRQ0");

    printf("\n=== INTERCONNECT TESTS COMPLETE ===\n");
    return 0;
} 