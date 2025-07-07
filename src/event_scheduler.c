#include "event_scheduler.h"
#include "interconnect.h"
#include <stddef.h>
#include <stdint.h>
#include "log.h"

#ifndef LOG_EVENT_DEBUG
#define LOG_EVENT_DEBUG(...) log_component("event_scheduler", LOG_LEVEL_DEBUG, __VA_ARGS__)
#endif

// --- Event Handler Table ---
// Forward declarations for event handler functions (to be implemented elsewhere)
typedef void (*EventHandler)(struct Interconnect*);

// Example: You will need to implement these handlers for your system
static void handle_vblank(struct Interconnect* sys); // TODO: Implement
static void handle_timer0(struct Interconnect* sys); // TODO: Implement
// ... add more as needed

// Table of event handlers, indexed by SystemEventType
static EventHandler evtq_handlers[EVENT_COUNT] = {
    handle_vblank,   // EVENT_VBLANK
    handle_timer0,   // EVENT_TIMER0
    NULL,            // EVENT_TIMER1 (add handler as needed)
    NULL,            // EVENT_TIMER2 (add handler as needed)
    NULL,            // EVENT_DMA
    NULL,            // EVENT_SIO
    NULL,            // EVENT_CDROM
    NULL,            // EVENT_GPU
    NULL,            // EVENT_MDEC
    NULL             // EVENT_SPU
};

// Schedule an event to occur after a given number of cycles
void event_scheduler_schedule(struct Interconnect* sys, SystemEventType event, uint32_t cycles_from_now) {
    // Set the event as pending
    sys->evtq_pending |= (1u << event);
    // Set the target cycle for this event
    sys->evtq_target_cycle[event] = sys->cpu_cycle_counter + cycles_from_now;
    // Update the next event cycle if this event is sooner
    if (sys->evtq_next_cycle > sys->evtq_target_cycle[event] || sys->evtq_next_cycle <= sys->cpu_cycle_counter) {
        sys->evtq_next_cycle = sys->evtq_target_cycle[event];
    }
}

// Check and dispatch any events that are due at the current cycle
void event_scheduler_dispatch_due(struct Interconnect* sys) {
    uint32_t now = sys->cpu_cycle_counter;
    uint32_t pending = sys->evtq_pending;
    // For each event type, check if it's due
    for (SystemEventType event = 0; event < EVENT_COUNT; ++event) {
        if ((pending & (1u << event)) && (int32_t)(now - sys->evtq_target_cycle[event]) >= 0) {
            // Clear the pending bit
            sys->evtq_pending &= ~(1u << event);
            // Call the event handler if registered
            if (evtq_handlers[event]) {
                evtq_handlers[event](sys);
            }
        }
    }
    // Recalculate the next event cycle
    uint32_t soonest = UINT32_MAX;
    for (SystemEventType event = 0; event < EVENT_COUNT; ++event) {
        if (sys->evtq_pending & (1u << event)) {
            uint32_t target = sys->evtq_target_cycle[event];
            if (target > now && target < soonest) {
                soonest = target;
            }
        }
    }
    sys->evtq_next_cycle = soonest;
}

// Calculate the cycle of the next scheduled event (for main loop efficiency)
uint32_t event_scheduler_next_cycle(const struct Interconnect* sys) {
    return sys->evtq_next_cycle;
}

// --- Example Event Handlers (Stubs) ---
// These should be implemented to set IRQs, reschedule themselves, etc.

// VBlank timing: 33868800 / 60 = 564480 cycles per NTSC frame
#define VBLANK_CYCLES 564480
// Timer0: For now, reschedule at a typical interval (can be tuned)
#define TIMER0_CYCLES 1000

static void handle_vblank(struct Interconnect* sys) {
    LOG_EVENT_DEBUG("[VBlank] Handler called. Setting IRQ0 (VBlank) in irq_status.");
    sys->irq_status |= (1u << 0); // Set VBlank bit in I_STAT
    LOG_EVENT_DEBUG("[VBlank] irq_status after set: 0x%04x", sys->irq_status);
    event_scheduler_schedule(sys, EVENT_VBLANK, VBLANK_CYCLES);
    LOG_EVENT_DEBUG("[VBlank] Next VBlank scheduled at cycle: %u", sys->evtq_target_cycle[EVENT_VBLANK]);
}

static void handle_timer0(struct Interconnect* sys) {
    // Set the Timer0 IRQ flag (IRQ_TIMER0 is usually 4)
    sys->irq_status |= (1u << 4); // Set Timer0 bit in I_STAT
    // Reschedule the next Timer0 event
    event_scheduler_schedule(sys, EVENT_TIMER0, TIMER0_CYCLES);
    // (Optional) Add debug log here
} 