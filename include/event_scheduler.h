#ifndef EVENT_SCHEDULER_H
#define EVENT_SCHEDULER_H

#include <stdint.h>

// ===============================
// Event System for PS1 Emulator
// ===============================
// This header defines the event types and API for the central event/timing system.
// All hardware events (timers, VBlank, DMA, etc.) are scheduled and dispatched here.
//
// NOTE: Naming and structure are original and distinct from PCSX ReARMed.

// --- Event Types ---
typedef enum {
    EVQ_VBLANK = 0,    // Vertical blanking interval (VBlank)
    EVQ_TIMER0,        // Timer 0 event (usually for VBlank IRQ0)
    EVQ_TIMER1,        // Timer 1 event
    EVQ_TIMER2,        // Timer 2 event
    EVQ_DMA_GPU,       // DMA event: GPU
    EVQ_DMA_CDROM,     // DMA event: CDROM
    EVQ_DMA_SPU,       // DMA event: SPU (optional, for future)
    EVQ_DMA_OTC,       // DMA event: Ordering Table Clear (OTC)
    EVQ_SIO,           // Serial I/O event
    EVQ_CDROM,         // CDROM event (non-DMA)
    EVQ_GPU,           // GPU event (non-DMA)
    EVQ_MDEC,          // MDEC event
    EVQ_SPU,           // SPU event
    EVQ_EVENT_COUNT    // Total number of event types
} EventQueueType;

// Forward declaration of the central system struct (e.g., Interconnect)
struct Interconnect;

// --- Event System API ---

/**
 * @brief Schedule an event to occur after a given number of cycles from now.
 * @param sys Pointer to the central system/interconnect struct.
 * @param event The event type to schedule.
 * @param cycles_from_now Number of CPU cycles from now to schedule the event.
 */
void eventq_schedule(struct Interconnect* sys, EventQueueType event, uint32_t cycles_from_now);

/**
 * @brief Check and dispatch any events that are due at the current cycle.
 * @param sys Pointer to the central system/interconnect struct.
 */
void eventq_dispatch_due(struct Interconnect* sys);

/**
 * @brief Get the cycle of the next scheduled event (for main loop efficiency).
 * @param sys Pointer to the central system/interconnect struct.
 * @return The cycle count of the next scheduled event.
 */
uint32_t eventq_next_cycle(const struct Interconnect* sys);

#endif // EVENT_SCHEDULER_H 