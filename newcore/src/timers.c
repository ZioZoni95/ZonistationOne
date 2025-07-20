#include "../include/timers.h"
#include "../include/log.h"
#include <string.h>

// Initialize timers state
void nc_timers_init(NcTimers* timers, struct NcInterconnect* inter) {
    timers->inter = inter;
    for (int i = 0; i < 3; ++i) {
        timers->timers[i].counter = 0;
        timers->timers[i].mode = 0;
        timers->timers[i].target = 0;
    }
    NC_LOGI("Timers initialized (3 timers, default state)");
} 