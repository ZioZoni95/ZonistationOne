#include "cdrom.h"
#include "interconnect.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    Cdrom cdrom = {0};
    Interconnect inter = {0};
    cdrom_init(&cdrom, &inter);

    // Simulate BIOS: Write Test (0x19) command
    cdrom.index = 0; // Command index
    cdrom_write_register(&cdrom, 0x1f801801, 0x19); // Write command
    cdrom.index = 0; // Param index
    cdrom_write_register(&cdrom, 0x1f801802, 0x00); // Subcommand 0x00

    // CDROM should now have response ready and IRQ2 set
    assert(inter.irq_status & (1 << 2)); // IRQ2 set

    // Simulate BIOS reading response FIFO
    cdrom.index = 1; // Response index
    for (int i = 0; i < 5; ++i) {
        (void)cdrom_read_register(&cdrom, 0x1f801801);
    }

    // Simulate BIOS writing to I_STAT to clear IRQ2
    inter.irq_status |= (1 << 2); // Set bit (simulate hardware)
    interconnect_store32(&inter, 0x1f801070, 0xFFFFFFFF); // Write to I_STAT
    assert((inter.irq_status & (1 << 2)) == 0); // IRQ2 cleared

    // CDROM should not re-assert IRQ2 until new command
    // (simulate a few steps)
    for (int i = 0; i < 10; ++i) {
        cdrom_step(&cdrom, 100);
        assert((inter.irq_status & (1 << 2)) == 0);
    }

    printf("CDROM IRQ2 clear test passed!\n");
    return 0;
} 