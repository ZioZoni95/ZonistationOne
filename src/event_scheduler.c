#include "event_scheduler.h"
#include "interconnect.h"
#include <stddef.h>
#include <stdint.h>
#include "log.h"
#include "dma.h"           // For DMA structures and helpers
#include "gpu.h"           // For GPU DMA transfer

// ===============================
// Event Scheduler Implementation
// ===============================
// This file implements the central event/timing system for the emulator.
// All hardware events (timers, VBlank, DMA, etc.) are scheduled and dispatched here.
// Naming and structure are original and distinct from PCSX ReARMed.

#ifndef LOG_EVENT_DEBUG
#define LOG_EVENT_DEBUG(...) log_component("event_scheduler", LOG_LEVEL_DEBUG, __VA_ARGS__)
#endif

// --- Event Handler Table ---
// Forward declarations for event handler functions (to be implemented elsewhere)
typedef void (*EventQueueHandler)(struct Interconnect*);

// Event handler stubs for all timer events
static void evq_handle_vblank(struct Interconnect* sys); // VBlank event
static void evq_handle_timer0(struct Interconnect* sys); // Timer0 event
static void evq_handle_timer1(struct Interconnect* sys); // Timer1 event
static void evq_handle_timer2(struct Interconnect* sys); // Timer2 event
static void evq_handle_dma_gpu(struct Interconnect* sys);   // GPU DMA event
static void evq_handle_dma_cdrom(struct Interconnect* sys); // CDROM DMA event
// ... add more as needed for other event types

// Table of event handlers, indexed by EventQueueType
typedef EventQueueHandler EventHandlerTable[EVQ_EVENT_COUNT];
static EventHandlerTable evq_handlers = {
    evq_handle_vblank,   // EVQ_VBLANK
    evq_handle_timer0,   // EVQ_TIMER0
    evq_handle_timer1,   // EVQ_TIMER1
    evq_handle_timer2,   // EVQ_TIMER2
    evq_handle_dma_gpu,  // EVQ_DMA_GPU
    evq_handle_dma_cdrom,// EVQ_DMA_CDROM
    NULL,                // EVQ_DMA_SPU
    NULL,                // EVQ_DMA_OTC
    NULL,                // EVQ_SIO
    NULL,                // EVQ_CDROM (non-DMA)
    NULL,                // EVQ_GPU (non-DMA)
    NULL,                // EVQ_MDEC
    NULL                 // EVQ_SPU
};

// --- Event Scheduling ---
/**
 * @brief Schedule an event to occur after a given number of cycles from now.
 */
void eventq_schedule(struct Interconnect* sys, EventQueueType event, uint32_t cycles_from_now) {
    sys->evq_pending |= (1u << event);
    sys->evq_target_cycle[event] = sys->cpu_cycle_counter + cycles_from_now;
    // --- Defensive, rate-limited logging for Timer0 event scheduling ---
    static int timer0_sched_count = 0;
    if (event == EVQ_TIMER0) {
        if (timer0_sched_count < 10 || timer0_sched_count % 1000 == 0) {
            LOG_EVENT_DEBUG("[EventQ][DEF] Scheduled Timer0 event: now=%u, target=%u, pending=0x%X [count=%d]", sys->cpu_cycle_counter, sys->evq_target_cycle[EVQ_TIMER0], sys->evq_pending, timer0_sched_count);
        }
        timer0_sched_count++;
    }
    // Update the next event cycle if this event is sooner
    if (sys->evq_next_cycle > sys->evq_target_cycle[event] || sys->evq_next_cycle <= sys->cpu_cycle_counter) {
        sys->evq_next_cycle = sys->evq_target_cycle[event];
    }
}

// --- Event Dispatch ---
/**
 * @brief Check and dispatch any events that are due at the current cycle.
 */
void eventq_dispatch_due(struct Interconnect* sys) {
    uint32_t now = sys->cpu_cycle_counter;
    uint32_t pending = sys->evq_pending;
    static int timer0_fire_count = 0;
    for (EventQueueType event = 0; event < EVQ_EVENT_COUNT; ++event) {
        if ((pending & (1u << event)) && (int32_t)(now - sys->evq_target_cycle[event]) >= 0) {
            // --- Defensive, rate-limited logging for Timer0 event firing ---
            if (event == EVQ_TIMER0) {
                if (timer0_fire_count < 10 || timer0_fire_count % 1000 == 0) {
                    LOG_EVENT_DEBUG("[EventQ][DEF] Firing Timer0 event: now=%u, target=%u, pending=0x%X [count=%d]", now, sys->evq_target_cycle[EVQ_TIMER0], sys->evq_pending, timer0_fire_count);
                }
                timer0_fire_count++;
            } else {
                LOG_EVENT_DEBUG("[EventQ] Firing event %d at cycle %u", event, now);
            }
            // Clear the pending bit
            sys->evq_pending &= ~(1u << event);
            // Call the event handler if registered
            if (evq_handlers[event]) {
                evq_handlers[event](sys);
            }
        }
    }
    // Recalculate the next event cycle
    uint32_t soonest = UINT32_MAX;
    for (EventQueueType event = 0; event < EVQ_EVENT_COUNT; ++event) {
        if (sys->evq_pending & (1u << event)) {
            uint32_t target = sys->evq_target_cycle[event];
            if (target > now && target < soonest) {
                soonest = target;
            }
        }
    }
    sys->evq_next_cycle = soonest;
}

// --- Next Event Cycle Query ---
/**
 * @brief Get the cycle of the next scheduled event (for main loop efficiency).
 */
uint32_t eventq_next_cycle(const struct Interconnect* sys) {
    return sys->evq_next_cycle;
}

// --- Example Event Handlers (Stubs) ---
// These should be implemented to set IRQs, reschedule themselves, etc.

#define VBLANK_CYCLES 564480 // NTSC: 33868800 / 60
#define TIMER0_CYCLES 1000   // Placeholder, tune as needed

static void evq_handle_vblank(struct Interconnect* sys) {
    eventq_schedule(sys, EVQ_VBLANK, VBLANK_CYCLES);
    LOG_EVENT_DEBUG("[VBlank] Handler called. Next VBlank scheduled at cycle: %u", sys->evq_target_cycle[EVQ_VBLANK]);
    timers_on_vblank(&sys->timers_state);
}

static void evq_handle_timer0(struct Interconnect* sys) {
    Timers* timers = &sys->timers_state;
    Timer* t = &timers->timers[0];
    LOG_EVENT_DEBUG("[Timer0][EVENT HANDLER] Called. Counter=%u, Target=%u, Mode=0x%04x, interrupt_requested=%d", t->counter, t->target, t->mode, t->interrupt_requested);
    if (!t->interrupt_requested) {
        t->interrupt_requested = true;
        t->reached_target_flag = true;
        t->mode |= (1 << 10);
        LOG_EVENT_DEBUG("[Timer0][EVENT HANDLER] IRQ0 requested by Timer0 event handler");
        interconnect_request_irq(sys, t->irq, "Timer0 event");
    } else {
        LOG_EVENT_DEBUG("[Timer0][EVENT HANDLER] IRQ0 NOT requested (already requested)");
    }
    // Do NOT reschedule Timer0 event here; wait for timer reset or mode write
}

static void evq_handle_timer1(struct Interconnect* sys) {
    Timers* timers = &sys->timers_state;
    Timer* t = &timers->timers[1];
    LOG_EVENT_DEBUG("[Timer1][EVENT HANDLER] Called. Counter=%u, Target=%u, Mode=0x%04x, interrupt_requested=%d", t->counter, t->target, t->mode, t->interrupt_requested);
    if (!t->interrupt_requested) {
        t->interrupt_requested = true;
        t->reached_target_flag = true;
        t->mode |= (1 << 10);
        LOG_EVENT_DEBUG("[Timer1][EVENT HANDLER] IRQ1 requested by Timer1 event handler");
        interconnect_request_irq(sys, t->irq, "Timer1 event");
    } else {
        LOG_EVENT_DEBUG("[Timer1][EVENT HANDLER] IRQ1 NOT requested (already requested)");
    }
}

static void evq_handle_timer2(struct Interconnect* sys) {
    Timers* timers = &sys->timers_state;
    Timer* t = &timers->timers[2];
    LOG_EVENT_DEBUG("[Timer2][EVENT HANDLER] Called. Counter=%u, Target=%u, Mode=0x%04x, interrupt_requested=%d", t->counter, t->target, t->mode, t->interrupt_requested);
    if (!t->interrupt_requested) {
        t->interrupt_requested = true;
        t->reached_target_flag = true;
        t->mode |= (1 << 10);
        LOG_EVENT_DEBUG("[Timer2][EVENT HANDLER] IRQ2 requested by Timer2 event handler");
        interconnect_request_irq(sys, t->irq, "Timer2 event");
    } else {
        LOG_EVENT_DEBUG("[Timer2][EVENT HANDLER] IRQ2 NOT requested (already requested)");
    }
}

static void evq_handle_dma_gpu(struct Interconnect* sys) {
    LOG_EVENT_DEBUG("[DMA] GPU DMA event handler called");
    Dma* dma = &sys->dma;
    DmaChannel* ch = &dma->channels[2]; // Channel 2: GPU
    if (!ch->enable) {
        LOG_EVENT_DEBUG("[DMA] GPU DMA event fired, but channel not enabled (already done?)");
        return;
    }
    extern void perform_gpu_dma_transfer(struct Interconnect* sys, DmaChannel* ch);
    perform_gpu_dma_transfer(sys, ch);
    dma_channel_done(ch);
    // --- DMA IRQ logic (PCSX ReARMed style) ---
    // Set channel IRQ flag for channel 2
    dma->channel_irq_flags |= (1 << 2);
    // If channel IRQ enable and master IRQ enable are set, set master IRQ flag
    if ((dma->channel_irq_enable & (1 << 2)) && dma->master_irq_enable) {
        dma->master_irq_flag = true;
    }
    // If master IRQ flag is set, set IRQ3 (DMA IRQ) in irq_status
    if (dma->master_irq_flag) {
        LOG_EVENT_DEBUG("[DMA] GPU DMA IRQ3 triggered (master IRQ flag set)");
        sys->irq_status |= (1u << 3); // IRQ3 is DMA
    }
}

static void evq_handle_dma_cdrom(struct Interconnect* sys) {
    LOG_EVENT_DEBUG("[DMA] CDROM DMA event handler called (stub)");
    // TODO: Complete the CDROM DMA transfer and set IRQ when ready
} 