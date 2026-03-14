// timers.c
#include "timers.h"
#include "interconnect.h" // Needed for interconnect_request_irq and IRQ defines
#include <stdio.h>
#include "gpu.h"
#include <string.h>
#include <math.h> // For floor()
#include "log.h"
#include "event_scheduler.h" // For eventq_schedule

#define PSX_CPU_HZ 33868800.0
#define PSX_SYSCLK_HZ PSX_CPU_HZ // System Clock is the same as the CPU clock for timers
#define DOTCLOCK_NTSC_HZ 25175000.0
#define DOTCLOCK_PAL_HZ 25200000.0 // PAL frequency, for completeness
#define HBLANK_NTSC_HZ 15625.0 // Horizontal blanking frequency for NTSC
// --- VBlank timing constant (NTSC: 33868800 / 60) ---
#define VBLANK_CYCLES 564480
#define TIMER_MODE_OFFSET 0x4

// Logging: Only use LOG_ERROR for timer hardware faults. No per-frame or per-IRQ logs.

// BIOS syscall handler for SetRCnt (0xBC)
#include "cpu.h"
void timers_handle_setrcnt(Timers* timers, Cpu* cpu) {
    // TODO: Implement SetRCnt logic based on BIOS arguments in cpu registers
    // Example: uint32_t timer_id = cpu_reg(cpu, 4); uint32_t mode = cpu_reg(cpu, 5);
    LOG_DEBUG("[TIMER] SetRCnt handler entered: timer_id=%u, mode=%u", cpu_reg(cpu, 4), cpu_reg(cpu, 5));
    // For now, just log the call. Implement full logic per DOCS/timers.md and DuckStation.
}

// Add this prototype at the top of the file
static void timer_force_bios_boot_config(Timers* timers);

/**
 * @brief Helper function to decode the mode register into internal state flags.
 * Called whenever the mode register is written.
 * @param timers Pointer to the Timers structure.
 * @param timer Pointer to the Timer instance to update.
 * @param timer_index Index of the timer (0, 1, or 2).
 */
static void timer_update_internal_state(Timers* timers, Timer* timer, int timer_index) {
    (void)timer_index;
    uint16_t mode = timer->mode;

    timer->sync_enable       = (mode & (1 << 0)) != 0;
    timer->sync_mode         = (mode >> 1) & 0x3;
    timer->reset_on_target   = (mode & (1 << 3)) != 0;
    timer->irq_on_target     = (mode & (1 << 4)) != 0;
    timer->irq_on_ffff       = (mode & (1 << 5)) != 0;
    timer->irq_repeat        = (mode & (1 << 6)) != 0;
    timer->irq_pulse         = (mode & (1 << 7)) != 0;
    timer->clock_source      = (mode >> 8) & 0x3;

    // Writing to mode register acknowledges/clears sticky IRQ flags (Bits 11, 12)
    // Also clear our internal tracking flags.
    timer->reached_target_flag = false;
    timer->reached_ffff_flag   = false;
    timer->interrupt_requested = false;
    // Clear Mode[10] interrupt request flag in the actual hardware register value
    timer->mode &= ~(1 << 10);
    // After clearing, if timer is still at target/overflow and IRQ enabled, re-assert IRQ
    if ((timer->irq_on_target && timer->counter == timer->target && (timer->mode & 0x100)) ||
        (timer->irq_on_ffff && timer->counter == 0xFFFF && (timer->mode & 0x100))) {
        timer->interrupt_requested = true;
        timer->mode |= (1 << 10);
        LOG_TIMER_DEBUG("[TIMER] Timer%d re-asserting IRQ%d after mode write (mode=0x%04x, counter=0x%04x, target=0x%04x)", timer_index, timer->irq, timer->mode, timer->counter, timer->target);
        interconnect_request_irq(timers->inter, timer->irq, "Timer re-assert after mode write");
    }
}

/**
 * @brief Initializes the state of all three timers.
 * @param timers Pointer to the Timers structure.
 * @param inter Pointer to the Interconnect (needed for requesting interrupts).
 */
void timers_init(Timers* timers, struct Interconnect* inter) {
    LOG_TIMER_DEBUG("Timers initialized");
    timers->inter = inter; // Store interconnect pointer

    // Initialize all three timers
    for (int i = 0; i < 3; ++i) {
        Timer* t = &timers->timers[i];
        t->counter = 0;
        t->mode    = 0;
        t->target  = 0;
        timer_update_internal_state(timers, t, i);
        t->reached_target_flag = false;
        t->reached_ffff_flag   = false;
        t->interrupt_requested = false;
        timers->fractional_ticks[i] = 0.0;
        // Set new fields
        t->rate = (i == 2) ? 8 : 1; // Default: Timer2 is /8, others are /1
        t->irq = (i == 0) ? TIMER0_IRQ : (i == 1) ? TIMER1_IRQ : TIMER2_IRQ;
        t->counter_state = TIMER_COUNT_TO_OVERFLOW;
        t->irq_state = 0;
        t->cycle = 0;
        t->cycle_start = 0;
    }
}


/**
 * @brief Reads a 16-bit value from a timer register.
 * @param timers Pointer to the Timers structure.
 * @param timer_index Index of the timer (0, 1, or 2).
 * @param offset Register offset (0x0, 0x4, 0x8).
 * @return The 16-bit value read.
 */
uint16_t timer_read16(Timers* timers, int timer_index, uint32_t offset) {
    if (timer_index < 0 || timer_index > 2) {
        LOG_TIMER_ERROR("Timer Read Error: Invalid timer index %d\n", timer_index);
        return 0;
    }
    Timer* t = &timers->timers[timer_index];

    switch (offset) {
        case TMR_REG_VAL: // 0x0: Counter Value
            // For Timer1 in dotclock/hblank mode, BIOS/games may use retry/median filtering for accuracy
            // (see PSX-Spex/nocash). Emulator could optionally implement this for lightgun support.
            return t->counter;
        case TMR_REG_MODE: // 0x4: Mode Register
            {
                // Update read-only status bits before returning mode value
                uint16_t mode = t->mode & ~0x1F00; // Clear status bits 12:10
                mode |= (uint16_t)t->reached_target_flag << 11;
                mode |= (uint16_t)t->reached_ffff_flag << 12;
                // Bit 10 (IRQ flag) reflects internal request state combined with enable bits
                bool irq_flag = (t->reached_target_flag && t->irq_on_target) ||
                                (t->reached_ffff_flag && t->irq_on_ffff);
                mode |= (uint16_t)irq_flag << 10;
                
                // HIGH PRIORITY FIX: PSX-SPEX mandates clearing flags after read
                t->reached_target_flag = false;
                t->reached_ffff_flag = false;
                
                return mode;
            }
        case TMR_REG_TARGET: // 0x8: Target Value
            return t->target;
        default:
            LOG_TIMER_ERROR("Timer Read Error: Unhandled timer%d offset 0x%x\n", timer_index, offset);
            return 0;
    }
}

/**
 * @brief Reads a 32-bit value from a timer register pair (Not standard PSX access).
 * For simplicity, just reads the lower 16 bits from the specified offset.
 * @param timers Pointer to the Timers structure.
 * @param timer_index Index of the timer (0, 1, or 2).
 * @param offset Register offset (0x0, 0x4, 0x8).
 * @return The 32-bit value (16-bit register zero-extended).
 */
uint32_t timer_read32(Timers* timers, int timer_index, uint32_t offset) {
    // 32-bit reads to timer registers likely only read the first 16 bits
    return (uint32_t)timer_read16(timers, timer_index, offset);
}


/**
 * @brief Writes a 16-bit value to a timer register.
 * @param timers Pointer to the Timers structure.
 * @param timer_index Index of the timer (0, 1, or 2).
 * @param offset Register offset (0x0, 0x4, 0x8).
 * @param value The 16-bit value to write.
 */
void timer_write16(Timers* timers, int timer_index, uint32_t offset, uint16_t value) {
    Timer* t = &timers->timers[timer_index];
    LOG_TIMER_DEBUG("[Timer%d] Write16: offset=0x%x value=0x%04x", timer_index, offset, value);
    if (offset == TIMER_MODE_OFFSET) {
        t->mode = value;
        t->counter = 0;  // CRITICAL FIX: PSX-SPEX mandates reset to 0000h on mode write
        t->interrupt_requested = false; // Clear IRQ state on mode write
        t->reached_target_flag = false;
        t->reached_ffff_flag = false;
        t->mode &= ~(1 << 10); // Clear IRQ request bit
        timer_update_internal_state(timers, t, timer_index);
        // Re-assert IRQ if condition is still true after mode write
        if (((t->irq_on_target && t->counter == t->target) ||
             (t->irq_on_ffff && t->counter == 0xFFFF)) && (t->mode & 0x100)) {
            t->interrupt_requested = true;
            t->mode |= (1 << 10);
            interconnect_request_irq(timers->inter, t->irq, "Timer re-assert after mode write");
        }
        return;
    }
    switch (offset) {
        case TMR_REG_VAL:
            t->counter = value;
            break;
        case TMR_REG_MODE:
            t->mode = value;
            timer_update_internal_state(timers, t, timer_index);
            LOG_TIMER_DEBUG("[Timer%d] Mode register written: 0x%04x", timer_index, value);
            break;
        case TMR_REG_TARGET:
            t->target = value;
            LOG_TIMER_DEBUG("[Timer%d] Target set: 0x%04x", timer_index, value);
            break;
        default:
            LOG_TIMER_ERROR("Timer Write Error: Unhandled timer%d offset 0x%x", timer_index, offset);
            break;
    }
}

/**
 * @brief Writes a 32-bit value to a timer register pair (Not standard PSX access).
 * Writes the lower 16 bits of the value to the specified register offset.
 * @param timers Pointer to the Timers structure.
 * @param timer_index Index of the timer (0, 1, or 2).
 * @param offset Register offset (0x0, 0x4, 0x8).
 * @param value The 32-bit value (lower 16 bits are used).
 */
void timer_write32(Timers* timers, int timer_index, uint32_t offset, uint32_t value) {
    // 32-bit writes to timer registers likely only write the lower 16 bits
    timer_write16(timers, timer_index, offset, (uint16_t)value);
}

/**
 * @brief Steps the timers forward by a number of elapsed CPU clock cycles.
 * Updates counters based on selected clock source, checks for target/overflow,
 * and requests interrupts via the interconnect. Uses fractional accumulation.
 * @param timers Pointer to the Timers structure.
 * @param cpu_cycles Number of CPU clock cycles presumed to have passed since last call.
 */
void timers_step(Timers* timers, uint32_t cpu_cycles) {
    if (cpu_cycles == 0) return;
    static int frame_counter = 0;
    frame_counter++;
    // For each timer, step according to its clock source and mode
    for (int i = 0; i < 3; ++i) {
        Timer* t = &timers->timers[i];
        double ticks_to_add = timers->fractional_ticks[i];
        double timer_clock_hz = 0.0;
        bool use_hblank = false, use_dotclock = false, use_div8 = false;

        // --- Clock source selection per DOCS/timers.md ---
        switch (i) {
            case 0: // Timer0: System clock or dotclock
                if (t->clock_source == 0) {
                    timer_clock_hz = PSX_SYSCLK_HZ;
                } else if (t->clock_source == 1) {
                    timer_clock_hz = DOTCLOCK_NTSC_HZ; // TODO: PAL support
                    use_dotclock = true;
                }
                break;
            case 1: // Timer1: System clock, HBlank, or dotclock
                if (t->clock_source == 0) {
                    timer_clock_hz = PSX_SYSCLK_HZ;
                } else if (t->clock_source == 1) {
                    timer_clock_hz = DOTCLOCK_NTSC_HZ; // Not commonly used
                    use_dotclock = true;
                } else if (t->clock_source == 2) {
                    use_hblank = true;
                }
                break;
            case 2: // Timer2: System clock or system clock / 8
                if (t->clock_source == 0) {
                    timer_clock_hz = PSX_SYSCLK_HZ;
                } else if (t->clock_source == 3) {
                    timer_clock_hz = PSX_SYSCLK_HZ / 8.0;
                    use_div8 = true;
                }
                break;
        }

        // --- Stepping logic ---
        if (use_hblank) {
            // HBlank: increment by number of HBlanks in cpu_cycles
            // HBlank rate: 33868800 / (263 * 60) = ~21477 Hz (NTSC)
            double hblank_cycles = PSX_CPU_HZ / (60.0 * 263.0);
            double hblanks = (double)cpu_cycles / hblank_cycles;
            ticks_to_add += hblanks;
        } else if (timer_clock_hz > 0.0) {
            // System clock or dotclock or div8
            ticks_to_add += (double)cpu_cycles * (timer_clock_hz / PSX_CPU_HZ);
        } else {
            // Unused clock source, skip
            timers->fractional_ticks[i] = ticks_to_add;
            continue;
        }

        uint32_t whole_ticks = (uint32_t)floor(ticks_to_add);
        timers->fractional_ticks[i] = ticks_to_add - (double)whole_ticks;
        if (whole_ticks == 0)
            continue;

        // --- Main counter increment and event logic ---
        for (uint32_t tick = 0; tick < whole_ticks; ++tick) {
            t->counter++;
            // Target reached?
            if (t->reset_on_target && t->target != 0 && t->counter == t->target) {
                t->reached_target_flag = true;
                if (t->irq_on_target && (t->mode & 0x100)) {
                    t->interrupt_requested = true;
                    t->mode |= (1 << 10);
                    interconnect_request_irq(timers->inter, t->irq, "Timer IRQ (target)");
                }
                t->counter = 0; // Reset on target
                continue;
            }
            // Overflow?
            if (t->counter == 0x10000) {
                t->reached_ffff_flag = true;
                if (t->irq_on_ffff && (t->mode & 0x100)) {
                    t->interrupt_requested = true;
                    t->mode |= (1 << 10);
                    interconnect_request_irq(timers->inter, t->irq, "Timer IRQ (overflow)");
                }
                t->counter = 0; // Always reset on overflow
            }
        }
    }
}

// --- Event scheduling for timers ---
void timers_schedule_next_event(Timers* timers, int timer_index) {
    LOG_TIMER_TRACE("[TIMER] timers_schedule_next_event for Timer%d", timer_index);
    Timer* t = &timers->timers[timer_index];
    
    // Special handling for Timer0 (VBlank) - schedule every frame, not every cycle
    if (timer_index == 0) {
        // Timer0 should fire every frame (60Hz), not every few cycles
        uint32_t frame_cycles = timers_calculate_frame_cycles();
        eventq_schedule(timers->inter, EVQ_TIMER0, frame_cycles);
        return;
    }
    
    // For other timers, use the original logic
    uint32_t cycles_until_event = 0;
    if (t->reset_on_target && t->target != 0) {
        if (t->counter < t->target)
            cycles_until_event = t->target - t->counter;
        else
            cycles_until_event = 0x10000 - t->counter + t->target;
    } else {
        cycles_until_event = 0x10000 - t->counter;
    }
    double clock_div = (t->rate == 8) ? 8.0 : (t->rate == 5) ? 5.0 : 1.0;
    uint32_t cpu_cycles = (uint32_t)(cycles_until_event * clock_div);
    eventq_schedule(timers->inter, EVQ_TIMER0 + timer_index, cpu_cycles);
}

// --- Frame/line timing functions ---
uint32_t timers_calculate_frame_cycles(void) {
    // NTSC: 33868800 / 60, PAL: 33868800 / 50
    // TODO: Use region flag if available
    return (uint32_t)(PSX_CPU_HZ / 60.0);
}

uint32_t timers_calculate_line_cycles(void) {
    // NTSC: 33868800 / (60 * 263)
    return (uint32_t)(PSX_CPU_HZ / (60.0 * 263.0));
}

// --- Lightgun/Scanline IRQ10 X coordinate conversion (PSX-Spex/nocash) ---
static inline float timer0_to_x_coord(uint16_t timer0, bool is_ntsc) {
    // Subtract 140 as per docs, then apply region-specific factor
    float base = (float)timer0 - 140.0f;
    return base * (is_ntsc ? 0.198166f : 0.196358f);
}

// --- BIOS Boot Helper: Force Timer0 Configuration ---
// Per PSX-Spex/nocash, if the BIOS doesn't configure Timer0 for VBlank IRQ0,
// we need to force it to prevent the BIOS from getting stuck in a loop.
static void timer_force_bios_boot_config(Timers* timers) {
    Timer* t0 = &timers->timers[0];
    
    // Force configuration if Timer0 is not properly configured for VBlank IRQ0
    // (mode doesn't have IRQ enable bit set, or target is zero)
    if ((t0->mode & 0x0100) == 0 || t0->target == 0x0000) {
        LOG_TIMER_INFO("[Timer0] BIOS Boot Helper: Forcing Timer0 configuration for VBlank IRQ0");
        
        // Configure Timer0 for VBlank IRQ0 (NTSC timing)
        // Mode: Enable counting, enable IRQ, IRQ on target
        t0->mode = 0x0110;  // Bit 8: IRQ enable, Bit 4: IRQ on target, Bit 0: Timer enable
        t0->target = 0xFFFF; // Target for VBlank timing
        t0->counter = 0x0000; // Start from 0
        
        // Update internal state
        timer_update_internal_state(timers, t0, 0);
        
        LOG_TIMER_INFO("[Timer0] Forced config: mode=0x%04x, target=0x%04x [PSX-Spex: VBlank IRQ0 enabled]", t0->mode, t0->target);
    }
}

// --- BIOS Timer Functions (stubs, not used by BIOS itself) ---
// See PSX-Spex kernelbios for details
int bios_init_timer(int t, uint16_t reload, uint16_t flags) { (void)t; (void)reload; (void)flags; return 1; }
int bios_get_timer(int t) { (void)t; return 0; }
int bios_enable_timer_irq(int t) { (void)t; return 1; }
int bios_disable_timer_irq(int t) { (void)t; return 1; }
int bios_restart_timer(int t) { (void)t; return 1; }
int bios_ChangeClearRCnt(int t, int flag) { (void)t; (void)flag; return 0; }

// --- BEGIN: PCSX ReARMed-inspired Timer Event Handlers ---
// Copyright (c) PCSX ReARMed authors. Used under open source license.
// These handlers are called by the event queue when a timer event fires.
static void timer_event_handler(Timers* timers, int timer_index) {
    static uint32_t timer_event_count[3] = {0, 0, 0};
    timer_event_count[timer_index]++;
    
    // Only log every 100 timer events to reduce spam
    if (timer_event_count[timer_index] % 100 == 1) {
        LOG_TIMER_DEBUG("[Timer%d] Event #%u (counter=0x%04x, target=0x%04x)", 
                       timer_index, timer_event_count[timer_index],
                       timers->timers[timer_index].counter, 
                       timers->timers[timer_index].target);
    }
    Timer* t = &timers->timers[timer_index];
    // Set sticky flag for target or overflow
    if (t->reset_on_target && t->target != 0 && t->counter == t->target) {
        t->reached_target_flag = true;
    }
    if (t->counter == 0xFFFF) {
        t->reached_ffff_flag = true;
    }
    // Only request IRQ if not already requested and enabled
    bool irq_enabled = (t->irq_on_target && t->reached_target_flag) || (t->irq_on_ffff && t->reached_ffff_flag);
    if (irq_enabled && !t->interrupt_requested && (t->mode & 0x100)) {
        t->interrupt_requested = true;
        t->mode |= (1 << 10); // Set IRQ request bit
        LOG_TIMER_DEBUG("[TIMER] Timer%d requesting IRQ%d (mode=0x%04x, counter=0x%04x, target=0x%04x)", timer_index, t->irq, t->mode, t->counter, t->target);
        interconnect_request_irq(timers->inter, t->irq, "Timer event handler");
    }
    // Reset counter if needed
    if (t->reset_on_target && t->reached_target_flag) {
        t->counter = 0;
        t->reached_target_flag = false;
    } else if (!t->reset_on_target && t->reached_ffff_flag) {
        // CRITICAL FIX: When reset_on_target=0, counter resets on FFFFh overflow
        t->counter = 0;
        t->reached_ffff_flag = false;
    }
    // Schedule next event
    timers_schedule_next_event(timers, timer_index);
}

// Expose C-callable wrappers for event_scheduler.c
void timer0_event_handler(struct Interconnect* sys) { timer_event_handler(&sys->timers_state, 0); }
void timer1_event_handler(struct Interconnect* sys) { timer_event_handler(&sys->timers_state, 1); }
void timer2_event_handler(struct Interconnect* sys) { timer_event_handler(&sys->timers_state, 2); }
// --- END: PCSX ReARMed-inspired Timer Event Handlers ---

// VBlank event handler for event_scheduler.c
void timers_on_vblank(Timers* timers) {
    static uint32_t vblank_count = 0;
    vblank_count++;
    
    // Only log every 300 frames (~5 seconds at 60fps)
    if (vblank_count % 300 == 1) {
        LOG_TIMER_DEBUG("[VBlank] Frame %u - VBlank running normally", vblank_count);
    }
    
    Timer* t0 = &timers->timers[0];
    t0->counter = 0;
    t0->reached_target_flag = false;
    
    // Only clear interrupt_requested if the IRQ was acknowledged
    if (t0->interrupt_requested) {
        t0->interrupt_requested = false;
    }
    
    // If Timer0 IRQ is enabled, request IRQ0 only if not already requested
    if ((t0->mode & 0x0100) && (t0->mode & 0x0010) && !t0->interrupt_requested) {
        if (timers->inter) {
            interconnect_request_irq(timers->inter, 0, "VBlank");
        } else {
            LOG_ERROR("[VBlank] ERROR: timers->inter is NULL!");
        }
        t0->interrupt_requested = true;
        t0->reached_target_flag = true;
    }
}