#include "../include/psx_cdrom.h"
#include <stdio.h>

// PSX-SPX: CD-ROM Controller implementation (skeleton)
static psx_cdrom_t cdrom;

void cdrom_init(void) {
    cdrom.dummy = 0;
    printf("[CDROM] CDROM initialized (skeleton)\n");
}

void cdrom_reset(void) {
    cdrom.dummy = 0;
    printf("[CDROM] CDROM reset (skeleton)\n");
}

void cdrom_step(void) {
    // TODO: Implement CDROM processing
}

u32 cdrom_read32(u32 addr) {
    printf("[CDROM] TODO: Read at 0x%08X\n", addr);
    return 0;
}

void cdrom_write32(u32 addr, u32 value) {
    printf("[CDROM] TODO: Write at 0x%08X = 0x%08X\n", addr, value);
}