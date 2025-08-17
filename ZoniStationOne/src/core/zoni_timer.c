#include "zoni_timer.h"
#include "zoni_common.h"

// Global timer system instance
static zoni_timer_system_t g_timer_system;

// Timer states (following PCSX ReARMed)
#define ZONI_TIMER_COUNT_TO_OVERFLOW 0
#define ZONI_TIMER_COUNT_TO_TARGET   1

// Timer mode bits (following PCSX ReARMed)
#define ZONI_TIMER_MODE_SYNC_ENABLE  0x0001
#define ZONI_TIMER_MODE_BLANK_PAUSE  0x0002
#define ZONI_TIMER_MODE_COUNT_TARGET 0x0008
#define ZONI_TIMER_MODE_IRQ_OVERFLOW 0x0020
#define ZONI_TIMER_MODE_IRQ_REGEN    0x0040
#define ZONI_TIMER_MODE_PIXEL_CLOCK  0x0100
#define ZONI_TIMER_MODE_HSYNC_CLOCK  0x0100
#define ZONI_TIMER_MODE_ONE_EIGHTH   0x0200
#define ZONI_TIMER_MODE_COUNT_EQ_TARGET 0x0800
#define ZONI_TIMER_MODE_OVERFLOW     0x1000

// PlayStation clock constants
#define ZONI_PSX_CLOCK 33868800
#define ZONI_HSYNC_TOTAL_NTSC 263
#define ZONI_HSYNC_TOTAL_PAL  314
#define ZONI_VBLANK_START 240

zoni_error_t zoni_timer_init(zoni_timer_system_t* timer_system) {
    if (!timer_system) {
        return ZONI_ERROR_INVALID_PARAMETER;
    }

    // Initialize timer 0 (following PCSX ReARMed)
    timer_system->timers[0].mode = 0;
    timer_system->timers[0].target = 0;
    timer_system->timers[0].rate = 1;
    timer_system->timers[0].irq = ZONI_TIMER_IRQ_0;
    timer_system->timers[0].counter_state = ZONI_TIMER_COUNT_TO_OVERFLOW;
    timer_system->timers[0].irq_state = 0;
    timer_system->timers[0].cycle = 0x10000; // Start with overflow cycle
    timer_system->timers[0].cycle_start = 0;

    // Initialize timer 1 (following PCSX ReARMed)
    timer_system->timers[1].mode = 0;
    timer_system->timers[1].target = 0;
    timer_system->timers[1].rate = 1;
    timer_system->timers[1].irq = ZONI_TIMER_IRQ_1;
    timer_system->timers[1].counter_state = ZONI_TIMER_COUNT_TO_OVERFLOW;
    timer_system->timers[1].irq_state = 0;
    timer_system->timers[1].cycle = 0x10000; // Start with overflow cycle
    timer_system->timers[1].cycle_start = 0;

    // Initialize timer 2 (following PCSX ReARMed)
    timer_system->timers[2].mode = 0;
    timer_system->timers[2].target = 0;
    timer_system->timers[2].rate = 1;
    timer_system->timers[2].irq = ZONI_TIMER_IRQ_2;
    timer_system->timers[2].counter_state = ZONI_TIMER_COUNT_TO_OVERFLOW;
    timer_system->timers[2].irq_state = 0;
    timer_system->timers[2].cycle = 0x10000; // Start with overflow cycle
    timer_system->timers[2].cycle_start = 0;

    // Initialize base counters
    timer_system->h_sync_count = 0;
    timer_system->frame_counter = 0;
    timer_system->initialized = true;

    zoni_log(ZONI_LOG_INFO, "Timer system initialized");
    return ZONI_SUCCESS;
}

void zoni_timer_reset(zoni_timer_system_t* timer_system) {
    if (!timer_system || !timer_system->initialized) {
        return;
    }

    for (int i = 0; i < ZONI_TIMER_COUNT; i++) {
        timer_system->timers[i].counter_state = ZONI_TIMER_COUNT_TO_OVERFLOW;
        timer_system->timers[i].irq_state = 0;
        timer_system->timers[i].cycle = 0x10000;
        timer_system->timers[i].cycle_start = 0;
    }

    timer_system->h_sync_count = 0;
    timer_system->frame_counter = 0;

    zoni_log(ZONI_LOG_INFO, "Timer system reset");
}

// Calculate line cycles based on region (following PCSX ReARMed)
static u32 zoni_timer_line_cycles(bool is_pal) {
    if (is_pal) {
        return ZONI_PSX_CLOCK / 50 / ZONI_HSYNC_TOTAL_PAL;
    } else {
        return ZONI_PSX_CLOCK / 60 / ZONI_HSYNC_TOTAL_NTSC;
    }
}

// Reset timer logic (following PCSX ReARMed)
static void zoni_timer_reset_counter(zoni_timer_system_t* timer_system, u32 index, u32 current_cycle) {
    zoni_timer_t* timer = &timer_system->timers[index];
    u32 rcycles;

    timer->mode |= 0x0400; // Set unknown bit 10

    if (timer->counter_state == ZONI_TIMER_COUNT_TO_TARGET) {
        rcycles = current_cycle - timer->cycle_start;
        
        if (timer->mode & ZONI_TIMER_MODE_COUNT_TARGET) {
            rcycles -= timer->target * timer->rate;
            timer->cycle_start = current_cycle - rcycles;
        } else {
            timer->cycle = 0x10000 * timer->rate;
            timer->counter_state = ZONI_TIMER_COUNT_TO_OVERFLOW;
        }

        if (timer->mode & ZONI_TIMER_MODE_IRQ_TARGET) {
            if ((timer->mode & ZONI_TIMER_MODE_IRQ_REGEN) || (!timer->irq_state)) {
                timer->irq_state = timer->irq;
                zoni_log(ZONI_LOG_DEBUG, "Timer %d target interrupt triggered", index);
            }
        }

        timer->mode |= ZONI_TIMER_MODE_COUNT_EQ_TARGET;

        if (rcycles < 0x10000 * timer->rate) {
            return;
        }
    }

    if (timer->counter_state == ZONI_TIMER_COUNT_TO_OVERFLOW) {
        rcycles = current_cycle - timer->cycle_start;
        rcycles -= 0x10000 * timer->rate;

        timer->cycle_start = current_cycle - rcycles;

        if (rcycles < timer->target * timer->rate) {
            timer->cycle = timer->target * timer->rate;
            timer->counter_state = ZONI_TIMER_COUNT_TO_TARGET;
        }

        if (timer->mode & ZONI_TIMER_MODE_IRQ_OVERFLOW) {
            if ((timer->mode & ZONI_TIMER_MODE_IRQ_REGEN) || (!timer->irq_state)) {
                timer->irq_state = timer->irq;
                zoni_log(ZONI_LOG_DEBUG, "Timer %d overflow interrupt triggered", index);
            }
        }

        timer->mode |= ZONI_TIMER_MODE_OVERFLOW;
    }
}

void zoni_timer_update(zoni_timer_system_t* timer_system, u32 cycles) {
    if (!timer_system || !timer_system->initialized) {
        return;
    }

    // Update timer 0 (following PCSX ReARMed logic)
    u32 cycles_passed = cycles - timer_system->timers[0].cycle_start;
    while (cycles_passed >= timer_system->timers[0].cycle) {
        // Check for special sync modes
        if (((timer_system->timers[0].mode & 7) == (ZONI_TIMER_MODE_SYNC_ENABLE | 1) ||
             (timer_system->timers[0].mode & 7) == (ZONI_TIMER_MODE_SYNC_ENABLE | 2)) &&
            cycles_passed > zoni_timer_line_cycles(false)) { // Assume NTSC for now
            u32 q = cycles_passed / (zoni_timer_line_cycles(false) + 1);
            timer_system->timers[0].cycle_start += q * zoni_timer_line_cycles(false);
            break;
        } else {
            zoni_timer_reset_counter(timer_system, 0, cycles);
        }
        cycles_passed = cycles - timer_system->timers[0].cycle_start;
    }

    // Update timer 1
    while (cycles - timer_system->timers[1].cycle_start >= timer_system->timers[1].cycle) {
        zoni_timer_reset_counter(timer_system, 1, cycles);
    }

    // Update timer 2
    while (cycles - timer_system->timers[2].cycle_start >= timer_system->timers[2].cycle) {
        zoni_timer_reset_counter(timer_system, 2, cycles);
    }

    // Update horizontal sync counter
    timer_system->h_sync_count += cycles;
}

// Timer register read functions
u32 zoni_timer_read_count(zoni_timer_system_t* timer_system, u32 index) {
    if (!timer_system || !timer_system->initialized || index >= ZONI_TIMER_COUNT) {
        return 0;
    }

    zoni_timer_t* timer = &timer_system->timers[index];
    u32 count = timer->counter_state;
    
    // Clear interrupt flags (following PCSX ReARMed)
    timer->mode &= 0xE7FF;
    
    return count;
}

u32 zoni_timer_read_mode(zoni_timer_system_t* timer_system, u32 index) {
    if (!timer_system || !timer_system->initialized || index >= ZONI_TIMER_COUNT) {
        return 0;
    }

    u16 mode = timer_system->timers[index].mode;
    // Clear interrupt flags (following PCSX ReARMed)
    timer_system->timers[index].mode &= 0xE7FF;
    
    return mode;
}

u32 zoni_timer_read_target(zoni_timer_system_t* timer_system, u32 index) {
    if (!timer_system || !timer_system->initialized || index >= ZONI_TIMER_COUNT) {
        return 0;
    }

    return timer_system->timers[index].target;
}

// Timer register write functions
void zoni_timer_write_count(zoni_timer_system_t* timer_system, u32 index, u32 value) {
    if (!timer_system || !timer_system->initialized || index >= ZONI_TIMER_COUNT) {
        return;
    }

    zoni_timer_t* timer = &timer_system->timers[index];
    value &= 0xFFFF;

    timer->cycle_start = 0; // Reset cycle start
    timer->cycle_start -= value * timer->rate;

    // Set appropriate counter state (following PCSX ReARMed)
    if (value < timer->target) {
        timer->cycle = timer->target * timer->rate;
        timer->counter_state = ZONI_TIMER_COUNT_TO_TARGET;
    } else {
        timer->cycle = 0x10000 * timer->rate;
        timer->counter_state = ZONI_TIMER_COUNT_TO_OVERFLOW;
    }

    timer->irq_state = 0; // Clear interrupt state
}

void zoni_timer_write_mode(zoni_timer_system_t* timer_system, u32 index, u32 value) {
    if (!timer_system || !timer_system->initialized || index >= ZONI_TIMER_COUNT) {
        return;
    }

    zoni_timer_t* timer = &timer_system->timers[index];
    timer->mode = value & 0xFFFF;
    timer->irq_state = 0; // Clear interrupt state

    // Set timer rate based on mode (following PCSX ReARMed)
    switch (index) {
        case 0:
            if (value & ZONI_TIMER_MODE_PIXEL_CLOCK) {
                timer->rate = 5;
            } else {
                timer->rate = 1;
            }
            break;
        case 1:
            if (value & ZONI_TIMER_MODE_HSYNC_CLOCK) {
                timer->rate = zoni_timer_line_cycles(false); // Assume NTSC for now
            } else {
                timer->rate = 1;
            }
            break;
        case 2:
            if (value & ZONI_TIMER_MODE_ONE_EIGHTH) {
                timer->rate = 8;
            } else {
                timer->rate = 1;
            }
            break;
    }
}

void zoni_timer_write_target(zoni_timer_system_t* timer_system, u32 index, u32 value) {
    if (!timer_system || !timer_system->initialized || index >= ZONI_TIMER_COUNT) {
        return;
    }

    timer_system->timers[index].target = value & 0xFFFF;
}

// Interrupt handling
bool zoni_timer_check_interrupts(zoni_timer_system_t* timer_system) {
    if (!timer_system || !timer_system->initialized) {
        return false;
    }

    for (int i = 0; i < ZONI_TIMER_COUNT; i++) {
        if (timer_system->timers[i].irq_state != 0) {
            return true;
        }
    }
    
    return false;
}

u32 zoni_timer_get_interrupt_status(zoni_timer_system_t* timer_system) {
    if (!timer_system || !timer_system->initialized) {
        return 0;
    }

    u32 status = 0;
    for (int i = 0; i < ZONI_TIMER_COUNT; i++) {
        status |= timer_system->timers[i].irq_state;
    }
    
    return status;
}

// Global timer system access
zoni_timer_system_t* zoni_timer_get_system(void) {
    return &g_timer_system;
}
