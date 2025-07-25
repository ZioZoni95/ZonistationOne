#include "../include/event_scheduler.h"
#include <stdio.h>

void nc_eventq_init(NcEventQueue* q) {
    // Stub: do nothing
    (void)q;
}

void nc_eventq_schedule(NcEventQueue* q, int event, int when, void (*handler)(void*), void* ctx) {
    // Stub: do nothing
    (void)q; (void)event; (void)when; (void)handler; (void)ctx;
}

void nc_eventq_dispatch_due(NcEventQueue* q) {
    // Stub: do nothing
    (void)q;
} 