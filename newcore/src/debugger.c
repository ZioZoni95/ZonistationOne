#include "../include/debugger.h"
#include "../include/log.h"

// Initialize debugger state
void nc_debugger_init(NcDebugger* dbg) {
    dbg->breakpoint_count = 0;
    dbg->paused = false;
    NC_LOGI("Debugger initialized (default state)");
} 