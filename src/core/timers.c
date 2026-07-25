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
// For now, just log the call. Implement full logic per DOCS/timers.md and DuckStation.
}

// Derived-counter model forward declarations (definitions further down).
static uint32_t timer_rate_cycles(Timers* timers, Timer* t, int i);
static void timers_catch_up_one(Timers* timers, int i);
static void timer_rebase(Timers* timers, int i);

/**
 * @brief Recomputes counting_enabled from sync_enable/sync_mode/gate.
 * Matches DuckStation's Timers::UpdateCountingEnabled exactly, including
 * Timer2's gate-less special case (its sync bit selects a stop/free-run
 * behavior instead of real gating).
 */
static void timer_update_counting_enabled(Timer* t, int timer_index) {
    if (timer_index != 2) {
        if (t->sync_enable) {
            switch (t->sync_mode) {
                case 0: /* PauseWhileGateActive */
                    t->counting_enabled = !t->gate;
                    break;
                case 1: /* ResetOnGateEnd */
                    t->counting_enabled = true;
                    break;
                case 2: /* ResetAndRunOnGateStart */
                case 3: /* FreeRunOnGateEnd */
                    t->counting_enabled = t->gate;
                    break;
            }
        } else {
            t->counting_enabled = true;
        }
    } else {
        /* Timer2: no gate source. Sync modes 0/3 stop counting, 1/2 free-run. */
        t->counting_enabled = !t->sync_enable || t->sync_mode == 1 || t->sync_mode == 2;
    }
}

/**
 * @brief Sets the external gate line level for a timer and applies the PS1
 * sync-mode edge behavior (counter reset/pause per mode). See timers.h.
 */
void timers_set_gate(Timers* timers, int timer_index, bool state) {
    if (timer_index < 0 || timer_index > 2) return;
    Timer* t = &timers->timers[timer_index];
    if (t->gate == state) return;

    /* Freeze the live counter into t->counter before the gate edge changes
     * counting/reset state, so the derived value doesn't jump. */
    timers_catch_up_one(timers, timer_index);
    t->gate = state;

    if (t->sync_enable) {
        switch (t->sync_mode) {
            case 0: /* PauseWhileGateActive: no counter change on edge */
                break;
            case 1: /* ResetOnGateEnd: reset while gate is active */
                t->counter = state ? t->counter : 0;
                break;
            case 2: /* ResetAndRunOnGateStart: reset at gate-active edge */
                t->counter = state ? 0 : t->counter;
                break;
            case 3: /* FreeRunOnGateEnd: sync_enable latches off once gate clears */
                t->sync_enable = t->sync_enable && state;
                break;
        }
        timer_update_counting_enabled(t, timer_index);
    }

    /* Re-anchor to the (possibly reset) counter and re-arm — or, if the gate
     * just paused the timer, timers_reschedule no-ops and it stays frozen. */
    timer_rebase(timers, timer_index);
    timers_reschedule(timers, timer_index);
}

/**
 * @brief Helper function to decode the mode register into internal state flags.
 * Called whenever the mode register is written.
 * @param timers Pointer to the Timers structure.
 * @param timer Pointer to the Timer instance to update.
 * @param timer_index Index of the timer (0, 1, or 2).
 */
static void timer_update_internal_state(Timers* timers, Timer* timer, int timer_index) {
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
    /* Re-assert if counter already at target/overflow when mode is written */
    if ((timer->irq_on_target && timer->counter == timer->target) ||
        (timer->irq_on_ffff   && timer->counter == 0xFFFF)) {
        timer->interrupt_requested = true;
        timer->mode |= (1 << 10);
        interconnect_request_irq(timers->inter, timer->irq, "Timer re-assert after mode write");
    }

    timer_update_counting_enabled(timer, timer_index);
}

/**
 * @brief Initializes the state of all three timers.
 * @param timers Pointer to the Timers structure.
 * @param inter Pointer to the Interconnect (needed for requesting interrupts).
 */
void timers_init(Timers* timers, struct Interconnect* inter) {
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
        LOG_TIMER_ERROR("[TIMER] Timer Read Error: Invalid timer index %d", timer_index);
        return 0;
    }
    Timer* t = &timers->timers[timer_index];

    switch (offset) {
        case TMR_REG_VAL: // 0x0: Counter Value
            // Catch up to the current cycle so a busy-poll sees a live value
            // (derived from cycle_start), not one frozen until the next event.
            timers_catch_up_one(timers, timer_index);
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
            LOG_TIMER_ERROR("[TIMER] Timer Read Error: Unhandled timer%d offset 0x%x", timer_index, offset);
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
/* Re-anchor cycle_start so the derived counter equals t->counter at "now". */
static void timer_rebase(Timers* timers, int i) {
    Timer* t = &timers->timers[i];
    if (!timers->inter) { return; }
    uint32_t rate = timer_rate_cycles(timers, t, i);
    if (rate == 0) rate = 1;
    t->rate = rate;
    t->cycle_start = timers->inter->cpu_cycle_counter - (uint32_t)t->counter * rate;
}

void timer_write16(Timers* timers, int timer_index, uint32_t offset, uint16_t value) {
    Timer* t = &timers->timers[timer_index];
    /* Catch up to the exact write cycle before applying, so the change takes
     * effect from now (DuckStation/Redux both sync on register writes). */
    timers_catch_up_one(timers, timer_index);

    if (offset == TIMER_MODE_OFFSET) {
        LOG_TIMER_DEBUG("[TIMER] Timer%d mode <- 0x%04x (sync=%d clkSrc=%d irqTarget=%d irqOverflow=%d)",
                        timer_index, value,
                        (value & 1) != 0, (value >> 8) & 3,
                        (value >> 4) & 1, (value >> 5) & 1);
        t->mode = value;
        t->counter = 0;                 /* mode write resets the counter */
        t->interrupt_requested = false;
        t->reached_target_flag = false;
        t->reached_ffff_flag = false;
        t->mode &= ~(1 << 10);          /* clear IRQ request bit */
        timer_update_internal_state(timers, t, timer_index);
        timer_rebase(timers, timer_index);
        timers_reschedule(timers, timer_index);
        return;
    }
    switch (offset) {
        case TMR_REG_VAL:
            t->counter = value;
            timer_rebase(timers, timer_index);
            timers_reschedule(timers, timer_index);
            break;
        case TMR_REG_MODE:
            t->mode = value;
            timer_update_internal_state(timers, t, timer_index);
            timer_rebase(timers, timer_index);
            timers_reschedule(timers, timer_index);
            break;
        case TMR_REG_TARGET:
            t->target = value;          /* period changed → re-arm */
            timers_reschedule(timers, timer_index);
            break;
        default:
            LOG_TIMER_ERROR("[TIMER] Timer Write Error: Unhandled timer%d offset 0x%x", timer_index, offset);
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

/* =========================================================================
 * Derived-counter timer model (interpreter-native, PCSX-Redux style).
 *
 * The counter is NOT stepped tick-by-tick. Its live value is DERIVED from the
 * global cpu_cycle_counter: counter = (now - cycle_start) / rate. The IRQ/reset
 * fires from the scheduled EVQ_TIMER event (armed at the next target/overflow),
 * and any register read/write catches the timer up to "now" on demand — so a
 * busy-poll always sees a continuously-advancing value with no per-tick loop.
 * See pcsx-redux Counters::readCounterInternal / update / set (psxcounters.cc).
 * ====================================================================== */

/* CPU cycles per timer tick for this timer's clock source (integer, >=1,
 * like Redux's Rcnt.rate). Dotclock (Timer0) and hblank (Timer1) derive from
 * the GPU's active video mode (PAL/NTSC) — see gpu_dotclock_hz/gpu_hblank_hz. */
static uint32_t timer_rate_cycles(Timers* timers, Timer* t, int i) {
    if (t->clock_source <= 1) return 1;             /* sysclock */
    const Gpu* gpu = timers->inter ? &timers->inter->gpu : NULL;
    switch (i) {
        case 0: { /* Timer0 dotclock */
            double hz = gpu ? gpu_dotclock_hz(gpu) : DOTCLOCK_NTSC_HZ;
            uint32_t r = (uint32_t)(PSX_CPU_HZ / hz + 0.5);
            return r ? r : 1;
        }
        case 1: { /* Timer1 hblank: one tick per scanline */
            double hz = gpu ? gpu_hblank_hz(gpu) : (PSX_CPU_HZ / (60.0 * 263.0));
            uint32_t r = (uint32_t)(PSX_CPU_HZ / hz + 0.5);
            return r ? r : 1;
        }
        default: return 8;                          /* Timer2 sysclock/8 */
    }
}

/* Tick distance from counter==0 back to the reset point. reset_on_target with
 * a non-zero target resets at target; otherwise it wraps at 0x10000. */
static uint32_t timer_period_ticks(const Timer* t) {
    if (t->reset_on_target && t->target != 0) return (uint32_t)t->target;
    return 0x10000u;
}

/* Fire the timer IRQ, honoring pulse/toggle mode, the already-pending guard,
 * and one-shot (irq_repeat==0) disable. Extracted so the event handler and the
 * on-read catch-up share ONE path (the old code had two conflicting guards). */
static void timer_fire_irq(Timers* timers, Timer* t, int i, bool hit_target, bool hit_overflow) {
    bool already_pending = timers->inter &&
        (timers->inter->irq_status & (1u << t->irq)) != 0;

    if (t->irq_pulse) {
        /* Toggle mode: bit10 flips; CPU IRQ only on the 0->1 transition. */
        if (t->mode & (1 << 10)) {
            t->mode &= ~(1 << 10);
        } else {
            t->mode |= (1 << 10);
            LOG_TIMER_DEBUG("[TIMER] Timer%d IRQ toggle->1 (tgt=%d ov=%d)", i, hit_target, hit_overflow);
            if (!already_pending) interconnect_request_irq(timers->inter, t->irq, "Timer IRQ (toggle)");
        }
    } else {
        t->mode |= (1 << 10);
        LOG_TIMER_DEBUG("[TIMER] Timer%d IRQ pulse (tgt=%d ov=%d)", i, hit_target, hit_overflow);
        if (!already_pending) interconnect_request_irq(timers->inter, t->irq, "Timer IRQ (pulse)");
    }

    if (!t->irq_repeat) {  /* one-shot: disable after first fire */
        t->irq_on_target = false;
        t->irq_on_ffff   = false;
        t->mode &= ~((1 << 4) | (1 << 5));
    }
}

/* Catch a single timer up to the current cycle: cross any elapsed boundaries
 * (firing IRQ + rebasing cycle_start each time), then refresh the cached
 * counter value. Idempotent — re-entry after a boundary sees ticks<period and
 * does nothing, so the read path and the event path can both call it without
 * double-firing. No effect while paused (sync gate) or before inter is wired. */
static void timers_catch_up_one(Timers* timers, int i) {
    Timer* t = &timers->timers[i];
    if (!timers->inter || !t->counting_enabled) return;
    uint32_t rate = timer_rate_cycles(timers, t, i);
    if (rate == 0) rate = 1;
    t->rate = rate;

    uint32_t now = timers->inter->cpu_cycle_counter;
    for (;;) {
        uint32_t elapsed = (now - t->cycle_start) / rate;   /* ticks since last base */
        uint32_t period  = timer_period_ticks(t);
        if (elapsed < period) { t->counter = (uint16_t)elapsed; break; }

        /* Boundary crossed. */
        bool hit_target   = (t->reset_on_target && t->target != 0);
        bool hit_overflow = !hit_target;
        if (hit_target)   t->reached_target_flag = true;
        if (hit_overflow) t->reached_ffff_flag   = true;
        if ((hit_target && t->irq_on_target) || (hit_overflow && t->irq_on_ffff))
            timer_fire_irq(timers, t, i, hit_target, hit_overflow);

        t->cycle_start += period * rate;   /* rebase to the boundary (counter->0) */
        t->counter = 0;
    }
}

/* Arm EVQ_TIMER{i} to fire at this timer's next target/overflow. Skipped while
 * the timer is gated off (no event ⇒ it simply won't fire until re-enabled). */
void timers_reschedule(Timers* timers, int i) {
    Timer* t = &timers->timers[i];
    if (!timers->inter || !t->counting_enabled) return;
    uint32_t rate = timer_rate_cycles(timers, t, i);
    if (rate == 0) rate = 1;
    t->rate = rate;
    uint32_t period    = timer_period_ticks(t);
    uint32_t remaining = (t->counter < period) ? (period - t->counter) : 1;
    eventq_schedule(timers->inter, EVQ_TIMER0 + i, remaining * rate);
}

/* Legacy name kept for the write path; now just reschedules. */
void timers_schedule_next_event(Timers* timers, int timer_index) {
    timers_reschedule(timers, timer_index);
}

/* Anchor all three timers at the current cycle and arm their first events. */
void timers_start(Timers* timers) {
    for (int i = 0; i < 3; i++) {
        timer_rebase(timers, i);
        timers_reschedule(timers, i);
    }
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

// --- BEGIN: PCSX ReARMed-inspired Timer Event Handlers ---
// Copyright (c) PCSX ReARMed authors. Used under open source license.
// These handlers are called by the event queue when a timer event fires.
static void timer_event_handler(Timers* timers, int timer_index) {
    /* The event fires at the timer's next boundary: catch it up (which crosses
     * the boundary, sets sticky flags, and fires the IRQ through the single
     * shared path), then re-arm for the following boundary. */
    timers_catch_up_one(timers, timer_index);
    timers_reschedule(timers, timer_index);
}

// Expose C-callable wrappers for event_scheduler.c
void timer0_event_handler(struct Interconnect* sys) { timer_event_handler(&sys->timers_state, 0); }
void timer1_event_handler(struct Interconnect* sys) { timer_event_handler(&sys->timers_state, 1); }
void timer2_event_handler(struct Interconnect* sys) { timer_event_handler(&sys->timers_state, 2); }
// --- END: PCSX ReARMed-inspired Timer Event Handlers ---

