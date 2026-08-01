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
    EVQ_DMA_CDROM,     // DMA event: CDROM (channel 3 completion; real transfer is synchronous, handler is a documented no-op)
    EVQ_SIO,           // Serial I/O event
    EVQ_CDROM_COMMAND,         // CDROM: command dispatch / first response (INT3)
    EVQ_CDROM_DRIVE,           // CDROM: drive mechanism tick (seek/read/play)
    EVQ_CDROM_SECOND_RESPONSE, // CDROM: delayed second response (INT2/INT5)
    EVQ_MDEC,          // MDEC event
    EVQ_SPU,           // SPU event — handled by dedicated SPU thread, handler intentionally NULL
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

/* Rebuild the next-event anchor from the pending set. Needed after a
 * savestate load, where evq_next_cycle arrives as stored data. */
void eventq_recompute_next(struct Interconnect* sys);

/**
 * @brief Returns CPU cycles until the next scheduled event.
 * Returns 0 when an event is already due, 1 when no events are pending.
 * @param sys Pointer to the central system/interconnect struct.
 * @return Cycles until next event dispatch point.
 */
uint32_t eventq_cycles_until_next(const struct Interconnect* sys);

#endif // EVENT_SCHEDULER_H 