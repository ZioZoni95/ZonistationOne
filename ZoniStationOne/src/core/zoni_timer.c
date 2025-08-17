#include "zoni_timer.h"
#include "zoni_log.h"

// Global timer system instance
static zoni_timer_system_t g_timer_system;

zoni_error_t zoni_timer_init(zoni_timer_system_t* timer_system) {
    if (!timer_system) {
        return ZONI_ERROR_INVALID_PARAMETER;
    }

    // Initialize timer 0 (following PCSX ReARMed)
    timer_system->timers[0].rate = 1;
    timer_system->timers[0].irq = ZONI_TIMER_IRQ_0;
    timer_system->timers[0].mode = 0;
    timer_system->timers[0].target = 0;
    timer_system->timers[0].counter_state = 0;
    timer_system->timers[0].irq_state = 0;
    timer_system->timers[0].cycle = 0;
    timer_system->timers[0].cycle_start = 0;

    // Initialize timer 1 (following PCSX ReARMed)
    timer_system->timers[1].rate = 1;
    timer_system->timers[1].irq = ZONI_TIMER_IRQ_1;
    timer_system->timers[1].mode = 0;
    timer_system->timers[1].target = 0;
    timer_system->timers[1].counter_state = 0;
    timer_system->timers[1].irq_state = 0;
    timer_system->timers[1].cycle = 0;
    timer_system->timers[1].cycle_start = 0;

    // Initialize timer 2 (following PCSX ReARMed)
    timer_system->timers[2].rate = 1;
    timer_system->timers[2].irq = ZONI_TIMER_IRQ_2;
    timer_system->timers[2].mode = 0;
    timer_system->timers[2].target = 0;
    timer_system->timers[2].counter_state = 0;
    timer_system->timers[2].irq_state = 0;
    timer_system->timers[2].cycle = 0;
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
        timer_system->timers[i].counter_state = 0;
        timer_system->timers[i].irq_state = 0;
        timer_system->timers[i].cycle = 0;
        timer_system->timers[i].cycle_start = 0;
    }

    timer_system->h_sync_count = 0;
    timer_system->frame_counter = 0;

    zoni_log(ZONI_LOG_INFO, "Timer system reset");
}

void zoni_timer_update(zoni_timer_system_t* timer_system, u32 cycles) {
    if (!timer_system || !timer_system->initialized) {
        return;
    }

    // Update each timer
    for (int i = 0; i < ZONI_TIMER_COUNT; i++) {
        zoni_timer_t* timer = &timer_system->timers[i];
        
        // Update counter based on rate
        u32 count = (cycles - timer->cycle_start) / timer->rate;
        if (count > 0) {
            timer->counter_state = (timer->counter_state + count) & 0xFFFF;
            timer->cycle_start = cycles;
            
            // Check if target reached and interrupt enabled
            if ((timer->mode & ZONI_TIMER_MODE_IRQ_ENABLE) && 
                (timer->mode & ZONI_TIMER_MODE_IRQ_TARGET) &&
                (timer->counter_state >= timer->target)) {
                
                timer->irq_state = timer->irq;
                zoni_log(ZONI_LOG_DEBUG, "Timer %d interrupt triggered", i);
            }
        }
    }

    // Update horizontal sync counter
    timer_system->h_sync_count += cycles;
}

// Timer register read functions
u32 zoni_timer_read_count(zoni_timer_system_t* timer_system, u32 index) {
    if (!timer_system || !timer_system->initialized || index >= ZONI_TIMER_COUNT) {
        return 0;
    }

    u32 count = timer_system->timers[index].counter_state;
    timer_system->timers[index].mode &= 0xE7FF; // Clear interrupt flags (following PCSX ReARMed)
    
    return count;
}

u32 zoni_timer_read_mode(zoni_timer_system_t* timer_system, u32 index) {
    if (!timer_system || !timer_system->initialized || index >= ZONI_TIMER_COUNT) {
        return 0;
    }

    u16 mode = timer_system->timers[index].mode;
    timer_system->timers[index].mode &= 0xE7FF; // Clear interrupt flags (following PCSX ReARMed)
    
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

    timer_system->timers[index].counter_state = value & 0xFFFF;
    timer_system->timers[index].cycle_start = 0; // Reset cycle start
    timer_system->timers[index].irq_state = 0;   // Clear interrupt state
}

void zoni_timer_write_mode(zoni_timer_system_t* timer_system, u32 index, u32 value) {
    if (!timer_system || !timer_system->initialized || index >= ZONI_TIMER_COUNT) {
        return;
    }

    timer_system->timers[index].mode = value & 0xFFFF;
    timer_system->timers[index].irq_state = 0; // Clear interrupt state
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
