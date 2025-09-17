#include "../include/psx_timer.h"
#include "../include/psx_irq.h"
#include <stdio.h>
#include <string.h>

// PSX-SPX: Timer implementation (skeleton)
static psx_timer_system_t timer_system;

void timer_init(void) {
    memset(&timer_system, 0, sizeof(timer_system));
    timer_reset();
    printf("[TIMER] Timer system initialized\n");
}

void timer_reset(void) {
    // PSX-SPX: Reset all timers
    for (int i = 0; i < 3; i++) {
        timer_system.timers[i].counter_value = 0;
        timer_system.timers[i].counter_mode = 0;
        timer_system.timers[i].counter_target = 0;
        timer_system.timers[i].current_value = 0;
        timer_system.timers[i].reached_target = false;
        timer_system.timers[i].reached_overflow = false;
        timer_system.timers[i].irq_request = false;
        timer_system.timers[i].clock_source = CLOCK_SYSTEM;
    }
    
    printf("[TIMER] Timer system reset\n");
}

void timer_step(u32 cycles) {
    // Update all timers
    for (int i = 0; i < 3; i++) {
        timer_update(i, cycles);
    }
}

u32 timer_read32(u32 addr) {
    int timer_num = -1;
    int reg = -1;
    
    // Decode timer and register
    if (addr >= 0x1F801100 && addr <= 0x1F80112C) {
        timer_num = (addr - 0x1F801100) / 0x10;
        reg = (addr - 0x1F801100) % 0x10;
    }
    
    if (timer_num >= 0 && timer_num < 3) {
        psx_timer_t* timer = &timer_system.timers[timer_num];
        
        switch (reg) {
            case 0x0: // Counter Value
                printf("[TIMER] Timer%d counter read = 0x%08X\n", timer_num, timer->current_value);
                return timer->current_value;
                
            case 0x4: // Counter Mode
                {
                    u32 mode = timer->counter_mode;
                    
                    // PSX-SPX: Clear flags on read
                    if (timer->reached_target) {
                        mode |= TIMER_MODE_REACHED_TARGET;
                        timer->reached_target = false;
                    }
                    if (timer->reached_overflow) {
                        mode |= TIMER_MODE_REACHED_OVERFLOW;
                        timer->reached_overflow = false;
                    }
                    
                    printf("[TIMER] Timer%d mode read = 0x%08X\n", timer_num, mode);
                    return mode;
                }
                
            case 0x8: // Counter Target
                printf("[TIMER] Timer%d target read = 0x%08X\n", timer_num, timer->counter_target);
                return timer->counter_target;
        }
    }
    
    printf("[TIMER] ERROR: Unmapped read32 at 0x%08X\n", addr);
    return 0;
}

void timer_write32(u32 addr, u32 value) {
    int timer_num = -1;
    int reg = -1;
    
    // Decode timer and register
    if (addr >= 0x1F801100 && addr <= 0x1F80112C) {
        timer_num = (addr - 0x1F801100) / 0x10;
        reg = (addr - 0x1F801100) % 0x10;
    }
    
    if (timer_num >= 0 && timer_num < 3) {
        psx_timer_t* timer = &timer_system.timers[timer_num];
        
        switch (reg) {
            case 0x0: // Counter Value
                timer->counter_value = value;
                timer->current_value = value;
                printf("[TIMER] Timer%d counter = 0x%08X\n", timer_num, value);
                break;
                
            case 0x4: // Counter Mode
                timer->counter_mode = value & 0x3FF;  // PSX-SPX: Only 10 bits writable
                
                // Reset counter on mode write
                timer->current_value = 0;
                timer->reached_target = false;
                timer->reached_overflow = false;
                
                // Decode clock source
                int clock_bits = (value >> 8) & 3;
                switch (timer_num) {
                    case 0:
                    case 1:
                        timer->clock_source = (clock_bits == 0) ? CLOCK_SYSTEM : 
                                            (clock_bits == 1) ? CLOCK_DOTCLOCK : CLOCK_HBLANK;
                        break;
                    case 2:
                        timer->clock_source = (clock_bits == 0 || clock_bits == 2) ? CLOCK_SYSTEM : CLOCK_SYSTEM_DIV8;
                        break;
                }
                
                printf("[TIMER] Timer%d mode = 0x%08X, clock_source = %d\n", 
                       timer_num, value, timer->clock_source);
                break;
                
            case 0x8: // Counter Target
                timer->counter_target = value & 0xFFFF;  // PSX-SPX: 16-bit target
                printf("[TIMER] Timer%d target = 0x%08X\n", timer_num, timer->counter_target);
                break;
        }
        return;
    }
    
    printf("[TIMER] ERROR: Unmapped write32 at 0x%08X = 0x%08X\n", addr, value);
}

void timer_update(int timer_num, u32 cycles) {
    if (timer_num < 0 || timer_num >= 3) return;
    
    psx_timer_t* timer = &timer_system.timers[timer_num];
    
    // TODO: Handle different clock sources properly
    u32 increment = cycles;
    if (timer->clock_source == CLOCK_SYSTEM_DIV8) {
        increment = cycles / 8;
    }
    
    timer->current_value += increment;
    
    // Check for target reached
    if (timer->counter_target > 0 && timer->current_value >= timer->counter_target) {
        timer->reached_target = true;
        
        if (timer->counter_mode & TIMER_MODE_IRQ_ON_TARGET) {
            timer->irq_request = true;
            irq_trigger(IRQ_TIMER0 + timer_num);
        }
        
        if (timer->counter_mode & TIMER_MODE_RESET_TO_ZERO) {
            timer->current_value = 0;
        }
    }
    
    // Check for overflow (0xFFFF)
    if (timer->current_value > 0xFFFF) {
        timer->reached_overflow = true;
        timer->current_value &= 0xFFFF;
        
        if (timer->counter_mode & TIMER_MODE_IRQ_ON_OVERFLOW) {
            timer->irq_request = true;
            irq_trigger(IRQ_TIMER0 + timer_num);
        }
    }
    
    timer->counter_value = timer->current_value;
}

void timer_check_irq(int timer_num) {
    // TODO: Implement IRQ logic
}