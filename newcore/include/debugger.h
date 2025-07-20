#ifndef NEWCORE_DEBUGGER_H
#define NEWCORE_DEBUGGER_H

#include <stdint.h>
#include <stdbool.h>

#define NC_MAX_BREAKPOINTS 16

// Debugger state structure for newcore
typedef struct {
    uint32_t breakpoints[NC_MAX_BREAKPOINTS];
    uint32_t breakpoint_count;
    bool paused;
} NcDebugger;

// Initialize debugger
void nc_debugger_init(NcDebugger* dbg);

#endif // NEWCORE_DEBUGGER_H 