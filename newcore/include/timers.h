#ifndef NEWCORE_TIMERS_H
#define NEWCORE_TIMERS_H

#include <stdint.h>
#include <stdbool.h>

struct NcInterconnect;

// Timer structure for newcore
typedef struct {
    uint16_t counter;
    uint16_t mode;
    uint16_t target;
    // TODO: Add more timer state fields as needed
} NcTimer;

// Timers controller for newcore
typedef struct {
    NcTimer timers[3];
    struct NcInterconnect* inter;
} NcTimers;

// Initialize timers
void nc_timers_init(NcTimers* timers, struct NcInterconnect* inter);

#endif // NEWCORE_TIMERS_H 