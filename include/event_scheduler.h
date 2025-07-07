#ifndef EVENT_SCHEDULER_H
#define EVENT_SCHEDULER_H

#include <stdint.h>

// Event types for the emulator's event/IRQ system
typedef enum {
    EVENT_VBLANK = 0,    // Vertical blank interrupt
    EVENT_TIMER0,        // Timer 0 interrupt
    EVENT_TIMER1,        // Timer 1 interrupt
    EVENT_TIMER2,        // Timer 2 interrupt
    EVENT_DMA,           // DMA event (expand as needed)
    EVENT_SIO,           // Serial I/O event
    EVENT_CDROM,         // CDROM event
    EVENT_GPU,           // GPU event
    EVENT_MDEC,          // MDEC event
    EVENT_SPU,           // SPU event
    EVENT_COUNT          // Total number of event types
} SystemEventType;

// Forward declaration of the central system struct (e.g., Interconnect)
struct Interconnect;

// Schedule an event to occur after a given number of cycles
void event_scheduler_schedule(struct Interconnect* sys, SystemEventType event, uint32_t cycles_from_now);

// Check and dispatch any events that are due at the current cycle
void event_scheduler_dispatch_due(struct Interconnect* sys);

// Calculate the cycle of the next scheduled event (for main loop efficiency)
uint32_t event_scheduler_next_cycle(const struct Interconnect* sys);

#endif // EVENT_SCHEDULER_H 