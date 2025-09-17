#include "../include/psx_spu.h"
#include <stdio.h>

// PSX-SPX: Sound Processing Unit implementation (skeleton)
static psx_spu_t spu;

void spu_init(void) {
    spu.dummy = 0;
    printf("[SPU] SPU initialized (skeleton)\n");
}

void spu_reset(void) {
    spu.dummy = 0;
    printf("[SPU] SPU reset (skeleton)\n");
}

void spu_step(void) {
    // TODO: Implement SPU processing
}

u32 spu_read32(u32 addr) {
    printf("[SPU] TODO: Read at 0x%08X\n", addr);
    return 0;
}

void spu_write32(u32 addr, u32 value) {
    printf("[SPU] TODO: Write at 0x%08X = 0x%08X\n", addr, value);
}