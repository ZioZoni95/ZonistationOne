#include "event_scheduler.h"
#include "interconnect.h"
#include "cpu.h"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "log.h"
#include "dma.h"           // For DMA structures and helpers
#include "gpu.h"           // For GPU DMA transfer
#include "timers.h"        // For timer event handler prototypes
#include "sio.h"           // For sio_execute_event

// ===============================
// Event Scheduler Implementation
// ===============================
// This file implements the central event/timing system for the emulator.
// All hardware events (timers, VBlank, DMA, etc.) are scheduled and dispatched here.
// Naming and structure are original and distinct from PCSX ReARMed.

// --- Event Handler Table ---
typedef void (*EventQueueHandler)(struct Interconnect*);

// Event handler stubs for all timer events
static void evq_handle_vblank(struct Interconnect* sys); // VBlank event
static void evq_handle_timer0(struct Interconnect* sys); // Timer0 event
static void evq_handle_timer1(struct Interconnect* sys); // Timer1 event
static void evq_handle_timer2(struct Interconnect* sys); // Timer2 event
static void evq_handle_dma_gpu(struct Interconnect* sys);   // GPU DMA event
static void evq_handle_dma_cdrom(struct Interconnect* sys); // CDROM DMA event
static void evq_handle_cdrom(struct Interconnect* sys); // CDROM event
static void evq_handle_sio(struct Interconnect* sys);   // SIO deferred transfer

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
    evq_handle_sio,      // EVQ_SIO
    evq_handle_cdrom,    // EVQ_CDROM (non-DMA)
    NULL,                // EVQ_GPU (non-DMA)
    NULL,                // EVQ_MDEC
    NULL                 // EVQ_SPU — handled by dedicated SPU thread
};

// --- Event Scheduling ---
/**
 * @brief Schedule an event to occur after a given number of cycles from now.
 */
void eventq_schedule(struct Interconnect* sys, EventQueueType event, uint32_t cycles_from_now) {
    sys->evq_pending |= (1u << event);
    sys->evq_target_cycle[event] = sys->cpu_cycle_counter + cycles_from_now;

    // Update the next event cycle if this event is sooner
    if (sys->evq_next_cycle > sys->evq_target_cycle[event] || sys->evq_next_cycle <= sys->cpu_cycle_counter) {
        sys->evq_next_cycle = sys->evq_target_cycle[event];
        // Immediately truncate CPU downcount so the CPU wakes up at the right time
        if (sys->cpu) {
            int32_t cycles_until = (int32_t)(sys->evq_next_cycle - sys->cpu_cycle_counter);
            if (cycles_until > 0 && cycles_until < sys->cpu->downcount)
                sys->cpu->downcount = cycles_until;
        }
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

uint32_t eventq_cycles_until_next(const struct Interconnect* sys) {
    const uint32_t now = sys->cpu_cycle_counter;
    const uint32_t next = sys->evq_next_cycle;

    if (next == UINT32_MAX) {
        // No pending scheduled events: execute at least one cycle.
        return 1;
    }

    if ((int32_t)(now - next) >= 0) {
        return 0;
    }

    return next - now;
}

// --- Example Event Handlers (Stubs) ---
// These should be implemented to set IRQs, reschedule themselves, etc.

#define VBLANK_CYCLES 564480 // NTSC: 33868800 / 60
#define TIMER0_CYCLES 1000   // Placeholder, tune as needed

static void evq_handle_vblank(struct Interconnect* sys) {
    static uint32_t vblank_count = 0;
    vblank_count++;

    gpu_crtc_tick(&sys->gpu, VBLANK_CYCLES);

    if (vblank_count <= 5 || vblank_count % 60 == 0)
        LOG_EVENT_DEBUG("[EVQ] VBlank #%u (cycle=%u)", vblank_count, sys->cpu_cycle_counter);

    eventq_schedule(sys, EVQ_VBLANK, VBLANK_CYCLES);

    if (!(sys->evq_pending & (1u << EVQ_VBLANK))) {
        LOG_EVENT_ERROR("[EVQ] CRITICAL: VBlank not rescheduled! pending=0x%X", sys->evq_pending);
    }
    
    // Trigger VBlank interrupt (IRQ0 per PSX-SPX)
    interconnect_request_irq(sys, 0, "VBlank");

    // NOTE: renderer_blit_vram disabled — FBO already contains correctly rendered content
    // from OpenGL primitive draws (polys, rects, lines). The blit read from vram_texture
    // (CPU-side R16UI buffer) had a UV wrap bug: fragment shader masks v with &0xFF, so
    // for vram_y=240 the v coordinate wraps at 256 → draws VRAM y=0 data over frame 2.
    // draw_ps1_display() now crops the FBO to crtc.display_vram_y/display_height directly.
}

static void evq_handle_timer0(struct Interconnect* sys) {
    static int timer0_dispatch_count = 0;
    if (timer0_dispatch_count++ < 3)
        LOG_EVENT_DEBUG("[EVQ] Timer0 fired (#%d)", timer0_dispatch_count);
    timer0_event_handler(sys);
}
static void evq_handle_timer1(struct Interconnect* sys) { timer1_event_handler(sys); }
static void evq_handle_timer2(struct Interconnect* sys) { timer2_event_handler(sys); }

static void evq_handle_dma_gpu(struct Interconnect* sys) {
    /* Resume GPU DMA slice — sets up next slice or signals completion */
    dma_gpu_resume(sys);
}

static void evq_handle_dma_cdrom(struct Interconnect* sys) {
    /* CDROM DMA is performed synchronously in interconnect_perform_dma()
       when channel 3 CHCR bit 24 is written. This handler is unused. */
    (void)sys;
}

static void evq_handle_cdrom(struct Interconnect* sys) {
    // CDROM events are now handled via interconnect_check_cdrom_events()
    // This handler is kept for legacy event system compatibility
    (void)sys;
}

static void evq_handle_sio(struct Interconnect* sys) {
    LOG_EVENT_DEBUG("[EVQ] Firing SIO deferred byte transfer");
    sio_execute_event(&sys->sio);
}