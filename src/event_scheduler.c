#include "event_scheduler.h"
#include "interconnect.h"
#include <stddef.h>
#include <stdint.h>
#include "log.h"
#include "dma.h"           // For DMA structures and helpers
#include "gpu.h"           // For GPU DMA transfer
#include "timers.h"        // Add this include for timer event handler prototypes

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
static void evq_handle_cdrom(struct Interconnect* sys); // CDROM event
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
    evq_handle_cdrom,    // EVQ_CDROM (non-DMA)
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
    if (event == EVQ_TIMER0 && log_get_level() >= LOG_LEVEL_INFO) {
        LOG_EVENT_DEBUG("[EventQ][DEF] Scheduled Timer0 event: now=%u, target=%u, pending=0x%X", sys->cpu_cycle_counter, sys->evq_target_cycle[EVQ_TIMER0], sys->evq_pending);
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
    // Keep dispatching as long as any event is due
    while (1) {
        uint32_t pending = sys->evq_pending;
        int any_fired = 0;
        for (EventQueueType event = 0; event < EVQ_EVENT_COUNT; ++event) {
            if ((pending & (1u << event)) && (int32_t)(now - sys->evq_target_cycle[event]) >= 0) {
                if (event == EVQ_TIMER0 && log_get_level() >= LOG_LEVEL_INFO) {
                    LOG_EVENT_DEBUG("[EventQ][DEF] Firing Timer0 event: now=%u, target=%u, pending=0x%X", now, sys->evq_target_cycle[EVQ_TIMER0], sys->evq_pending);
                }
                sys->evq_pending &= ~(1u << event);
                if (evq_handlers[event]) {
                    evq_handlers[event](sys);
                }
                any_fired = 1;
            }
        }
        if (!any_fired) break;
        // After firing, update now in case event handler advanced cycles
        now = sys->cpu_cycle_counter;
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
    // FIX: Only reschedule VBlank if it's not already scheduled
    // This prevents the infinite VBlank loop that was causing the stuck state
    if (!(sys->evq_pending & (1u << EVQ_VBLANK))) {
        eventq_schedule(sys, EVQ_VBLANK, VBLANK_CYCLES);
        LOG_EVENT_DEBUG("[VBlank] Handler called. Next VBlank scheduled at cycle: %u", sys->evq_target_cycle[EVQ_VBLANK]);
    } else {
        LOG_EVENT_DEBUG("[VBlank] Handler called but VBlank already scheduled, skipping reschedule");
    }
    
    // FIX: Trigger VBlank interrupt (IRQ1) to wake up the BIOS
    if (sys->irq_mask & 0x0002) { // Check if IRQ1 (VBlank) is enabled
        sys->irq_status |= 0x0002; // Set IRQ1 bit
        LOG_EVENT_DEBUG("[VBlank] IRQ1 (VBlank) triggered: I_STAT=0x%04x", sys->irq_status);
    }
    
    timers_on_vblank(&sys->timers_state);
}

static void evq_handle_timer0(struct Interconnect* sys) {
    static int timer0_dispatch_count = 0;
    if (timer0_dispatch_count < 5) {
        LOG_EVENT_DEBUG("[EventQ] DISPATCH Timer0 event handler called (count=%d)", ++timer0_dispatch_count);
    }
    timer0_event_handler(sys);
}
static void evq_handle_timer1(struct Interconnect* sys) { timer1_event_handler(sys); }
static void evq_handle_timer2(struct Interconnect* sys) { timer2_event_handler(sys); }

static void evq_handle_dma_gpu(struct Interconnect* sys) {
    LOG_EVENT_DEBUG("[DMA] Entered GPU DMA event handler");
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
    LOG_EVENT_DEBUG("[DMA] Channel 2 IRQ flag set: channel_irq_flags=0x%02x", dma->channel_irq_flags);
    // If channel IRQ enable and master IRQ enable are set, set master IRQ flag
    if ((dma->channel_irq_enable & (1 << 2)) && dma->master_irq_enable) {
        dma->master_irq_flag = true;
        LOG_EVENT_DEBUG("[DMA] Master IRQ flag set: master_irq_flag=%d", dma->master_irq_flag);
    }
    // If master IRQ flag is set, set IRQ3 (DMA IRQ) in irq_status
    if (dma->master_irq_flag) {
        LOG_EVENT_DEBUG("[DMA] GPU DMA IRQ3 triggered (master IRQ flag set)");
        sys->irq_status |= (1u << 3); // IRQ3 is DMA
    }
    LOG_EVENT_DEBUG("[DMA] Handler exit: channel_irq_enable=0x%02x, master_irq_enable=%d, master_irq_flag=%d, irq_status=0x%04x", dma->channel_irq_enable, dma->master_irq_enable, dma->master_irq_flag, sys->irq_status);
}

static void evq_handle_dma_cdrom(struct Interconnect* sys) {
    LOG_EVENT_DEBUG("[DMA] CDROM DMA event handler called (stub)");
    // TODO: Complete the CDROM DMA transfer and set IRQ when ready
}

static void evq_handle_cdrom(struct Interconnect* sys) {
    LOG_CDROM_INFO("[CDROM] Event handler fired\n");
    if (sys->cdrom.pending_completion_handler) {
        void (*handler)(struct Cdrom*) = sys->cdrom.pending_completion_handler;
        sys->cdrom.pending_completion_handler = NULL;
        handler(&sys->cdrom);
    }
} 