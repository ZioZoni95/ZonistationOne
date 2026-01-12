// SPDX-License-Identifier: MIT
// Timer Core Implementation
// Based on DuckStation and No$PSX documentation

#include "timers/timer_core.h"
#include "timers/timer_types.h"
#include "interconnect.h"
#include "irq/irq_core.h"
#include "log.h"
#include <stdio.h>
#include <string.h>
#include <limits.h>

// =============================================================================
// INTERNAL FUNCTION PROTOTYPES
// =============================================================================

static void update_counting_enabled(TimerState* timer, uint32_t timer_index);
static void check_for_irq(TimersState* timers, struct Interconnect* inter,
                         uint32_t timer_index, uint32_t old_counter);
static void trigger_timer_irq(TimersState* timers, struct Interconnect* inter,
                              uint32_t timer_index);

// =============================================================================
// LIFECYCLE MANAGEMENT
// =============================================================================

void timers_init(TimersState* timers, struct Interconnect* inter) {
    // Initialize mutex
    mutex_init(&timers->lock);
    
    // Store interconnect pointer
    timers->inter = inter;
    
    // Initialize global state
    timers->sysclk_ticks_carry = 0;
    timers->sysclk_div_8_carry = 0;
    
    // Initialize all three timers
    for (uint32_t i = 0; i < TIMER_COUNT; i++) {
        TimerState* timer = &timers->timers[i];
        
        // Hardware registers
        timer->mode.bits = 0;
        timer->mode.interrupt_request_n = 1; // IRQ inactive (inverted logic)
        timer->counter = 0;
        timer->target = 0;
        
        // Runtime state
        timer->gate = false;
        timer->use_external_clock = false;
        timer->external_counting_enabled = false;
        timer->counting_enabled = true;     // Default: free-running
        timer->irq_done = false;
        
        // Cycle tracking
        timer->sysclk_div_8_carry = 0;
        timer->pause_counter = 0;
    }
    
    LOG_INFO("[TIMER] Timers initialized: 3 hardware timers ready");
}

void timers_reset(TimersState* timers) {
    mutex_lock(&timers->lock);
    
    // Reset global state
    timers->sysclk_ticks_carry = 0;
    timers->sysclk_div_8_carry = 0;
    
    // Reset all timers (same as init but preserve inter pointer)
    for (uint32_t i = 0; i < TIMER_COUNT; i++) {
        TimerState* timer = &timers->timers[i];
        
        timer->mode.bits = 0;
        timer->mode.interrupt_request_n = 1;
        timer->counter = 0;
        timer->target = 0;
        timer->gate = false;
        timer->use_external_clock = false;
        timer->external_counting_enabled = false;
        timer->counting_enabled = true;
        timer->irq_done = false;
        timer->sysclk_div_8_carry = 0;
        timer->pause_counter = 0;
    }
    
    mutex_unlock(&timers->lock);
    LOG_INFO("[TIMER] Timers reset to initial state");
}

void timers_shutdown(TimersState* timers) {
    mutex_destroy(&timers->lock);
    LOG_INFO("[TIMER] Timers shutdown complete");
}

// =============================================================================
// INTERNAL HELPER FUNCTIONS
// =============================================================================

/**
 * @brief Update counting_enabled flag based on sync mode and gate state
 * 
 * Timer 2 special case: sync modes 0/3 stop forever, 1/2 ignore sync
 */
static void update_counting_enabled(TimerState* timer, uint32_t timer_index) {
    if (!timer->mode.sync_enable) {
        // Sync disabled: always count
        timer->counting_enabled = true;
    } else {
        // Timer 2 has different sync behavior
        if (timer_index == 2) {
            TimerSyncMode sync_mode = (TimerSyncMode)timer->mode.sync_mode;
            if (sync_mode == TIMER_SYNC_PAUSE_IN_GATE || sync_mode == TIMER_SYNC_FREE_RUN) {
                // Modes 0/3: stop forever
                timer->counting_enabled = false;
            } else {
                // Modes 1/2: free run (ignore sync)
                timer->counting_enabled = true;
            }
        } else {
            // Timer 0/1: standard sync behavior
            TimerSyncMode sync_mode = (TimerSyncMode)timer->mode.sync_mode;
            switch (sync_mode) {
                case TIMER_SYNC_PAUSE_IN_GATE:
                    // Pause while gate active
                    timer->counting_enabled = !timer->gate;
                    break;
                
                case TIMER_SYNC_RESET_ON_GATE:
                    // Always counting (reset happens on gate transition)
                    timer->counting_enabled = true;
                    break;
                
                case TIMER_SYNC_RESET_AND_RUN:
                case TIMER_SYNC_FREE_RUN:
                    // Count only while gate active (or after first gate for FREE_RUN)
                    timer->counting_enabled = timer->gate;
                    break;
            }
        }
    }
    
    // External counting requires both external clock and counting enabled
    timer->external_counting_enabled = timer->use_external_clock && timer->counting_enabled;
}

/**
 * @brief Check if timer conditions trigger an IRQ
 * 
 * Checks both target and overflow conditions, handles one-shot vs repeat,
 * pulse vs toggle modes, and triggers IRQ through interconnect.
 */
static void check_for_irq(TimersState* timers, struct Interconnect* inter,
                         uint32_t timer_index, uint32_t old_counter) {
    TimerState* timer = &timers->timers[timer_index];
    bool interrupt_request = false;
    
    // Check target condition
    if (timer->counter >= timer->target && (old_counter < timer->target || timer->target == 0)) {
        timer->mode.reached_target = 1;
        interrupt_request |= timer->mode.irq_at_target;
        
        // Reset to 0 if reset_at_target enabled and target > 0
        if (timer->mode.reset_at_target && timer->target > 0) {
            timer->counter %= timer->target;
            timer->pause_counter = TIMER_RESET_DELAY_CYCLES;
        }
    }
    
    // Check overflow condition
    if (timer->counter >= 0xFFFF) {
        timer->mode.reached_overflow = 1;
        interrupt_request |= timer->mode.irq_on_overflow;
        timer->counter %= 0xFFFF;
        timer->pause_counter = TIMER_OVERFLOW_DELAY_CYCLES;
    }
    
    // If IRQ condition met, trigger it
    if (interrupt_request) {
        trigger_timer_irq(timers, inter, timer_index);
    }
}

/**
 * @brief Trigger timer IRQ through interconnect
 * 
 * Handles pulse vs toggle mode, one-shot vs repeat mode.
 */
static void trigger_timer_irq(TimersState* timers, struct Interconnect* inter,
                              uint32_t timer_index) {
    TimerState* timer = &timers->timers[timer_index];
    uint32_t irq_line = timer_get_irq_line(timer_index);
    
    // Check one-shot mode
    if (!timer->mode.irq_repeat && timer->irq_done) {
        // One-shot already fired, suppress IRQ
        return;
    }
    
    if (timer_is_pulse_mode(timer)) {
        // Pulse mode: brief pulse (bit 10 goes 0 then back to 1)
        TIMER_LOG_DEBUG("[TIMER] Timer%u IRQ pulse triggered", timer_index);
        timer->mode.interrupt_request_n = 1; // Set high briefly
        interconnect_request_irq(inter, irq_line, "Timer pulse");
    } else {
        // Toggle mode: invert bit 10
        timer->mode.interrupt_request_n ^= 1;
        TIMER_LOG_DEBUG("[TIMER] Timer%u IRQ toggle: %u", timer_index, 
                       !timer->mode.interrupt_request_n);
        
        // Only trigger IRQ on 1->0 transition
        if (!timer->mode.interrupt_request_n) {
            interconnect_request_irq(inter, irq_line, "Timer toggle");
        }
    }
    
    // Mark IRQ as done (for one-shot mode)
    timer->irq_done = true;
}

// =============================================================================
// REGISTER I/O
// =============================================================================

uint32_t timers_read_register(TimersState* timers, uint32_t offset) {
    mutex_lock(&timers->lock);
    
    int timer_index = timer_index_from_offset(offset);
    if (timer_index < 0) {
        LOG_ERROR("[TIMER] Read from invalid offset 0x%02X", offset);
        mutex_unlock(&timers->lock);
        return 0xFFFFFFFF;
    }
    
    uint32_t reg_offset = timer_reg_from_offset(offset);
    TimerState* timer = &timers->timers[timer_index];
    uint32_t value;
    
    switch (reg_offset) {
        case TIMER_REG_COUNTER:
            // Read current counter value
            value = timer->counter & 0xFFFF;
            TIMER_LOG_DEBUG("[TIMER] Timer%d read counter: 0x%04X", timer_index, value);
            break;
        
        case TIMER_REG_MODE:
            // Read mode register (clears sticky flags on read)
            value = timer->mode.bits & 0xFFFF;
            timer->mode.reached_target = 0;
            timer->mode.reached_overflow = 0;
            TIMER_LOG_DEBUG("[TIMER] Timer%d read mode: 0x%04X [flags cleared]", timer_index, value);
            break;
        
        case TIMER_REG_TARGET:
            // Read target value
            value = timer->target & 0xFFFF;
            TIMER_LOG_DEBUG("[TIMER] Timer%d read target: 0x%04X", timer_index, value);
            break;
        
        default:
            LOG_ERROR("[TIMER] Read from unknown register: timer%d offset 0x%02X", 
                     timer_index, reg_offset);
            value = 0xFFFFFFFF;
            break;
    }
    
    mutex_unlock(&timers->lock);
    return value;
}

void timers_write_register(TimersState* timers, struct Interconnect* inter, 
                           uint32_t offset, uint32_t value) {
    mutex_lock(&timers->lock);
    
    int timer_index = timer_index_from_offset(offset);
    if (timer_index < 0) {
        LOG_ERROR("[TIMER] Write to invalid offset 0x%02X = 0x%08X", offset, value);
        mutex_unlock(&timers->lock);
        return;
    }
    
    uint32_t reg_offset = timer_reg_from_offset(offset);
    TimerState* timer = &timers->timers[timer_index];
    
    switch (reg_offset) {
        case TIMER_REG_COUNTER: {
            // Write counter value
            uint32_t old_counter = timer->counter;
            timer->counter = value & 0xFFFF;
            timer->pause_counter = TIMER_WRITE_DELAY_CYCLES;
            TIMER_LOG_DEBUG("[TIMER] Timer%d write counter: 0x%04X (was 0x%04X)", 
                           timer_index, timer->counter, old_counter);
            check_for_irq(timers, inter, timer_index, old_counter);
            break;
        }
        
        case TIMER_REG_MODE: {
            // Write mode register
            static const uint32_t WRITE_MASK = 0b1110001111111111; // Bits 0-9, 12-14
            timer->mode.bits = (value & WRITE_MASK) | (timer->mode.bits & ~WRITE_MASK);
            
            // Reset counter to 0 on mode write
            timer->counter = 0;
            timer->pause_counter = TIMER_RESET_DELAY_CYCLES;
            
            // Update use_external_clock based on clock source
            uint32_t clock_source = timer->mode.clock_source;
            timer->use_external_clock = timer_uses_external_clock(timer_index, clock_source);
            
            // Clear IRQ state
            timer->irq_done = false;
            timer->mode.interrupt_request_n = 1; // Bit 10 is active low (1 = inactive)
            timer->mode.reached_target = 0;
            timer->mode.reached_overflow = 0;
            
            // Update counting enabled based on sync settings
            update_counting_enabled(timer, timer_index);
            
            TIMER_LOG_DEBUG("[TIMER] Timer%d write mode: 0x%04X [counter reset, sync=%u, clock=%u, ext=%u]",
                           timer_index, timer->mode.bits & 0xFFFF, 
                           timer->mode.sync_enable, clock_source, timer->use_external_clock);
            
            // Do NOT re-assert IRQ here. The mode write acts as an acknowledgment.
            // A new IRQ will only be generated when the counter hits the target/overflow again.
            break;
        }
        
        case TIMER_REG_TARGET: {
            // Write target value
            timer->target = value & 0xFFFF;
            TIMER_LOG_DEBUG("[TIMER] Timer%d write target: 0x%04X", timer_index, timer->target);
            
            // Check if counter already >= new target (immediate IRQ)
            if (timer->counter >= timer->target && timer->mode.irq_at_target) {
                uint32_t old_counter = timer->counter;
                check_for_irq(timers, inter, timer_index, old_counter);
            }
            break;
        }
        
        default:
            LOG_ERROR("[TIMER] Write to unknown register: timer%d offset 0x%02X = 0x%08X",
                     timer_index, reg_offset, value);
            break;
    }
    
    mutex_unlock(&timers->lock);
}

// =============================================================================
// PER-TIMER ACCESS
// =============================================================================

uint32_t timer_read_counter(TimersState* timers, uint32_t timer_index) {
    if (timer_index >= TIMER_COUNT) return 0;
    
    mutex_lock(&timers->lock);
    uint32_t value = timers->timers[timer_index].counter & 0xFFFF;
    mutex_unlock(&timers->lock);
    return value;
}

uint32_t timer_read_mode(TimersState* timers, uint32_t timer_index) {
    if (timer_index >= TIMER_COUNT) return 0;
    
    mutex_lock(&timers->lock);
    TimerState* timer = &timers->timers[timer_index];
    uint32_t value = timer->mode.bits & 0xFFFF;
    // Clear sticky flags on read
    timer->mode.reached_target = 0;
    timer->mode.reached_overflow = 0;
    mutex_unlock(&timers->lock);
    return value;
}

uint32_t timer_read_target(TimersState* timers, uint32_t timer_index) {
    if (timer_index >= TIMER_COUNT) return 0;
    
    mutex_lock(&timers->lock);
    uint32_t value = timers->timers[timer_index].target & 0xFFFF;
    mutex_unlock(&timers->lock);
    return value;
}

void timer_write_counter(TimersState* timers, uint32_t timer_index, uint32_t value) {
    if (timer_index >= TIMER_COUNT) return;
    
    mutex_lock(&timers->lock);
    TimerState* timer = &timers->timers[timer_index];
    timer->counter = value & 0xFFFF;
    timer->pause_counter = TIMER_WRITE_DELAY_CYCLES;
    mutex_unlock(&timers->lock);
}

void timer_write_mode(TimersState* timers, struct Interconnect* inter, 
                     uint32_t timer_index, uint32_t value) {
    if (timer_index >= TIMER_COUNT) return;
    
    mutex_lock(&timers->lock);
    TimerState* timer = &timers->timers[timer_index];
    
    // Apply write mask
    static const uint32_t WRITE_MASK = 0b1110001111111111;
    timer->mode.bits = (value & WRITE_MASK) | (timer->mode.bits & ~WRITE_MASK);
    
    // Reset counter
    timer->counter = 0;
    timer->pause_counter = TIMER_RESET_DELAY_CYCLES;
    
    // Update clock source
    uint32_t clock_source = timer->mode.clock_source;
    timer->use_external_clock = timer_uses_external_clock(timer_index, clock_source);
    
    // Clear IRQ done flag
    timer->irq_done = false;
    
    // Update counting state
    update_counting_enabled(timer, timer_index);
    
    // Check IRQ
    check_for_irq(timers, inter, timer_index, 0);
    
    mutex_unlock(&timers->lock);
}

void timer_write_target(TimersState* timers, struct Interconnect* inter,
                       uint32_t timer_index, uint32_t value) {
    if (timer_index >= TIMER_COUNT) return;
    
    mutex_lock(&timers->lock);
    TimerState* timer = &timers->timers[timer_index];
    timer->target = value & 0xFFFF;
    
    // Check if counter already >= new target
    if (timer->counter >= timer->target && timer->mode.irq_at_target) {
        uint32_t old_counter = timer->counter;
        check_for_irq(timers, inter, timer_index, old_counter);
    }
    
    mutex_unlock(&timers->lock);
}

// =============================================================================
// CYCLE STEPPING
// =============================================================================

void timers_add_sysclk_ticks(TimersState* timers, struct Interconnect* inter, 
                             uint32_t sysclk_ticks) {
    mutex_lock(&timers->lock);
    
    // Scale ticks for overclock (placeholder - no overclocking yet)
    uint32_t scaled_ticks = sysclk_ticks;
    
    // Timer 0: sysclk (if not using external clock)
    TimerState* t0 = &timers->timers[0];
    if (!t0->external_counting_enabled && t0->counting_enabled) {
        timer_add_ticks(timers, inter, 0, scaled_ticks);
    }
    
    // Timer 1: sysclk (if not using external clock)
    TimerState* t1 = &timers->timers[1];
    if (!t1->external_counting_enabled && t1->counting_enabled) {
        timer_add_ticks(timers, inter, 1, scaled_ticks);
    }
    
    // Timer 2: sysclk or sysclk/8
    TimerState* t2 = &timers->timers[2];
    if (t2->counting_enabled) {
        if (t2->use_external_clock) {
            // Sysclk / 8 mode
            uint32_t total_ticks = scaled_ticks + timers->sysclk_div_8_carry;
            uint32_t div8_ticks = total_ticks / 8;
            timers->sysclk_div_8_carry = total_ticks % 8;
            if (div8_ticks > 0) {
                timer_add_ticks(timers, inter, 2, div8_ticks);
            }
        } else {
            // Sysclk mode
            timer_add_ticks(timers, inter, 2, scaled_ticks);
        }
    }
    
    mutex_unlock(&timers->lock);
}

/**
 * Add dot clock ticks (external clock for Timer 0)
 * Called by GPU during rendering
 */
void timers_add_dotclock_ticks(TimersState* timers, struct Interconnect* inter, 
                              uint32_t dotclock_ticks) {
    mutex_lock(&timers->lock);
    
    TimerState* t0 = &timers->timers[0];
    if (t0->external_counting_enabled && t0->counting_enabled) {
        timer_add_ticks(timers, inter, 0, dotclock_ticks);
    }
    
    mutex_unlock(&timers->lock);
}

/**
 * Add HBlank ticks (external clock for Timer 1)
 * Called when HBlank occurs
 */
void timers_add_hblank_ticks(TimersState* timers, struct Interconnect* inter, 
                            uint32_t hblank_ticks) {
    mutex_lock(&timers->lock);
    
    TimerState* t1 = &timers->timers[1];
    if (t1->external_counting_enabled && t1->counting_enabled) {
        timer_add_ticks(timers, inter, 1, hblank_ticks);
    }
    
    mutex_unlock(&timers->lock);
}

void timer_add_ticks(TimersState* timers, struct Interconnect* inter, 
                    uint32_t timer_index, uint32_t ticks) {
    if (timer_index >= TIMER_COUNT) return;
    
    // Note: Caller should already hold mutex for efficiency
    // If called externally, should acquire mutex first
    
    TimerState* timer = &timers->timers[timer_index];
    
    // Handle pause delay
    if (timer->pause_counter > 0) {
        if (ticks >= timer->pause_counter) {
            ticks -= timer->pause_counter;
            timer->pause_counter = 0;
        } else {
            timer->pause_counter -= ticks;
            return; // Still paused
        }
    }
    
    // Don't increment if not counting
    if (!timer->counting_enabled) {
        return;
    }
    
    // Store old counter for IRQ checking
    uint32_t old_counter = timer->counter;
    
    // Increment counter
    timer->counter += ticks;
    
    // Check for IRQ conditions
    check_for_irq(timers, inter, timer_index, old_counter);
}

// =============================================================================
// GATE CONTROL
// =============================================================================

void timer_set_gate(TimersState* timers, struct Interconnect* inter,
                   uint32_t timer_index, bool state) {
    if (timer_index >= TIMER_COUNT) return;
    
    mutex_lock(&timers->lock);
    
    TimerState* timer = &timers->timers[timer_index];
    
    // No change?
    if (timer->gate == state) {
        mutex_unlock(&timers->lock);
        return;
    }
    
    timer->gate = state;
    
    // Only matters if sync enabled
    if (!timer->mode.sync_enable) {
        mutex_unlock(&timers->lock);
        return;
    }
    
    TIMER_LOG_DEBUG("[TIMER] Timer%u gate %s (sync_mode=%u)", 
                   timer_index, state ? "ACTIVE" : "INACTIVE", timer->mode.sync_mode);
    
    // Handle sync mode transitions
    TimerSyncMode sync_mode = (TimerSyncMode)timer->mode.sync_mode;
    
    switch (sync_mode) {
        case TIMER_SYNC_PAUSE_IN_GATE:
            // Update counting state (paused when gate active)
            update_counting_enabled(timer, timer_index);
            break;
        
        case TIMER_SYNC_RESET_ON_GATE:
            // Reset counter when gate ends
            if (!state) {
                timer->counter = 0;
                timer->pause_counter = TIMER_RESET_DELAY_CYCLES;
                TIMER_LOG_DEBUG("[TIMER] Timer%u counter reset on gate end", timer_index);
            }
            break;
        
        case TIMER_SYNC_RESET_AND_RUN:
            // Reset and run when gate starts, pause outside gate
            if (state) {
                timer->counter = 0;
                timer->pause_counter = TIMER_RESET_DELAY_CYCLES;
                TIMER_LOG_DEBUG("[TIMER] Timer%u counter reset on gate start", timer_index);
            }
            update_counting_enabled(timer, timer_index);
            break;
        
        case TIMER_SYNC_FREE_RUN:
            // Disable sync after first gate
            if (state) {
                timer->mode.sync_enable = 0;
                TIMER_LOG_DEBUG("[TIMER] Timer%u sync disabled after first gate", timer_index);
            }
            update_counting_enabled(timer, timer_index);
            break;
    }
    
    mutex_unlock(&timers->lock);
}

// =============================================================================
// QUERY FUNCTIONS
// =============================================================================

bool timer_is_using_external_clock(const TimersState* timers, uint32_t timer_index) {
    if (timer_index >= TIMER_COUNT) return false;
    return timers->timers[timer_index].use_external_clock;
}

bool timer_is_sync_enabled(const TimersState* timers, uint32_t timer_index) {
    if (timer_index >= TIMER_COUNT) return false;
    return timers->timers[timer_index].mode.sync_enable != 0;
}

bool timer_is_external_irq_enabled(const TimersState* timers, uint32_t timer_index) {
    if (timer_index >= TIMER_COUNT) return false;
    
    const TimerState* timer = &timers->timers[timer_index];
    return timer->external_counting_enabled && 
           (timer->mode.irq_at_target || timer->mode.irq_on_overflow);
}

int32_t timer_get_ticks_until_irq(const TimersState* timers, uint32_t timer_index) {
    if (timer_index >= TIMER_COUNT) return INT32_MAX;
    
    const TimerState* timer = &timers->timers[timer_index];
    
    // Not counting?
    if (!timer->counting_enabled) return INT32_MAX;
    
    // No IRQ enabled?
    if (!timer->mode.irq_at_target && !timer->mode.irq_on_overflow) return INT32_MAX;
    
    // One-shot already fired?
    if (!timer->mode.irq_repeat && timer->irq_done) return INT32_MAX;
    
    int32_t min_ticks = INT32_MAX;
    
    // Check target IRQ
    if (timer->mode.irq_at_target) {
        if (timer->counter <= timer->target) {
            int32_t ticks = timer->target - timer->counter;
            if (ticks < min_ticks) min_ticks = ticks;
        } else {
            // Wrapped case: need to reach 0xFFFF then count to target
            int32_t ticks = (0xFFFF - timer->counter) + timer->target;
            if (ticks < min_ticks) min_ticks = ticks;
        }
    }
    
    // Check overflow IRQ
    if (timer->mode.irq_on_overflow) {
        int32_t ticks = 0xFFFF - timer->counter;
        if (ticks < min_ticks) min_ticks = ticks;
    }
    
    return min_ticks;
}

bool timer_is_counting(const TimersState* timers, uint32_t timer_index) {
    if (timer_index >= TIMER_COUNT) return false;
    return timers->timers[timer_index].counting_enabled;
}

// =============================================================================
// STATE MANAGEMENT
// =============================================================================

size_t timers_get_state_size(void) {
    return sizeof(TimersState);
}

size_t timers_save_state(const TimersState* timers, void* buffer) {
    memcpy(buffer, timers, sizeof(TimersState));
    return sizeof(TimersState);
}

bool timers_load_state(TimersState* timers, const void* buffer, size_t size) {
    if (size != sizeof(TimersState)) {
        LOG_ERROR("[TIMER] Invalid state size: %zu (expected %zu)", size, sizeof(TimersState));
        return false;
    }
    
    mutex_lock(&timers->lock);
    
    // Preserve mutex and interconnect pointer
    Mutex saved_lock = timers->lock;
    struct Interconnect* saved_inter = timers->inter;
    
    // Load state
    memcpy(timers, buffer, sizeof(TimersState));
    
    // Restore mutex and interconnect
    timers->lock = saved_lock;
    timers->inter = saved_inter;
    
    mutex_unlock(&timers->lock);
    LOG_INFO("[TIMER] State loaded successfully");
    return true;
}

// =============================================================================
// DEBUGGING/DIAGNOSTICS
// =============================================================================

int timers_get_state_string(const TimersState* timers, uint32_t timer_index, 
                           char* buffer, size_t size) {
    if (timer_index >= TIMER_COUNT || !buffer || size == 0) return 0;
    
    const TimerState* timer = &timers->timers[timer_index];
    
    return snprintf(buffer, size,
                   "Timer%u: counter=0x%04X target=0x%04X mode=0x%04X%s%s%s%s%s",
                   timer_index,
                   timer->counter & 0xFFFF,
                   timer->target & 0xFFFF,
                   timer->mode.bits & 0xFFFF,
                   timer->counting_enabled ? " [COUNTING]" : " [PAUSED]",
                   timer->mode.sync_enable ? " [SYNC]" : "",
                   timer->mode.irq_at_target ? " [IRQ_TARGET]" : "",
                   timer->mode.irq_on_overflow ? " [IRQ_OVERFLOW]" : "",
                   timer->use_external_clock ? " [EXT_CLK]" : "");
}

void timers_dump_state(const TimersState* timers) {
    char buffer[256];
    
    LOG_INFO("[TIMER] ==== Timer State Dump ====");
    for (uint32_t i = 0; i < TIMER_COUNT; i++) {
        timers_get_state_string(timers, i, buffer, sizeof(buffer));
        LOG_INFO("[TIMER] %s", buffer);
    }
    LOG_INFO("[TIMER] ===========================");
}
