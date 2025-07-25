// timer_unit.h
// Migrated from timers.c: timers/counters logic (header)
// TODO: Move timers/counters declarations here.

#ifndef TIMER_UNIT_H
#define TIMER_UNIT_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// --- Timer Unit State ---
typedef struct {
    // TODO: Add timer/counter state, registers, etc.
} TimerUnit;

// --- Timer Unit API ---
void timer_unit_init(TimerUnit* timers);
void timer_unit_step(TimerUnit* timers);

#endif // TIMER_UNIT_H 