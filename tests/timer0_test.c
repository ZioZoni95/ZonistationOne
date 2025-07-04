#include "../include/timers.h"
#include <stdio.h>
#include <stdbool.h>

// Logging macro stubs for standalone test
#define LOG_TIMERS_INFO(fmt, ...)   printf("[INFO] " fmt "\n", ##__VA_ARGS__)
#define LOG_TIMERS_DEBUG(fmt, ...)  // no-op
#define LOG_TIMERS_WARN(fmt, ...)   printf("[WARN] " fmt "\n", ##__VA_ARGS__)
#define LOG_TIMERS_ERROR(fmt, ...)  printf("[ERROR] " fmt "\n", ##__VA_ARGS__)

// Dummy interconnect with IRQ0 request tracking
static bool irq0_requested = false;
void interconnect_request_irq(struct Interconnect* inter, uint32_t irq, const char* source) {
    if (irq == 0) {
        irq0_requested = true;
        printf("[TEST] IRQ0 requested by Timer0 at %s\n", source);
    }
}

int main() {
    Timers timers;
    timers_init(&timers, NULL); // No real interconnect needed

    // Set Timer0 mode: 0x0110 (IRQ enable, IRQ on target, timer enable)
    timer_write16(&timers, 0, TMR_REG_MODE, 0x0110);
    // Set Timer0 target: 10000
    timer_write16(&timers, 0, TMR_REG_TARGET, 10000);
    // Start counter at 0
    timer_write16(&timers, 0, TMR_REG_VAL, 0x0000);

    printf("[TEST] Timer0 initialized: mode=0x%04x, target=%u\n", timers.timers[0].mode, timers.timers[0].target);

    // Step the timer in chunks until we reach/past the target
    irq0_requested = false;
    uint32_t total_cycles = 0;
    while (!irq0_requested && total_cycles < 0x20000) {
        timers_step(&timers, 1000); // Step by 1000 cycles at a time
        total_cycles += 1000;
        printf("[TEST] Counter=%u, Target=%u, Mode=0x%04x\n", timers.timers[0].counter, timers.timers[0].target, timers.timers[0].mode);
    }

    if (irq0_requested) {
        printf("[TEST] SUCCESS: IRQ0 was requested when counter reached target.\n");
    } else {
        printf("[TEST] FAILURE: IRQ0 was NOT requested after stepping.\n");
    }
    return irq0_requested ? 0 : 1;
} 