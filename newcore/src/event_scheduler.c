#include "../include/event_scheduler.h"
#include "../include/log.h"
#include <string.h>

void nc_eventq_init(NcEventQueue* q) {
    memset(q, 0, sizeof(NcEventQueue));
    NC_LOGI("Event queue initialized");
}

void nc_eventq_schedule(NcEventQueue* q, NcEventType type, uint32_t cycle, void (*handler)(void*), void* ctx) {
    if (q->count < NC_EVENT_QUEUE_SIZE) {
        NcEvent* e = &q->events[q->count++];
        e->type = type;
        e->cycle = cycle;
        e->handler = handler;
        e->ctx = ctx;
        NC_LOGI("Event scheduled: type=%d cycle=%u", type, cycle);
    } else {
        NC_LOGW("Event queue full, cannot schedule");
    }
}

void nc_eventq_dispatch_due(NcEventQueue* q) {
    for (int i = 0; i < q->count; ++i) {
        if (q->events[i].cycle <= q->current_cycle) {
            NC_LOGI("Dispatching event: type=%d", q->events[i].type);
            if (q->events[i].handler) q->events[i].handler(q->events[i].ctx);
            // Remove event from queue
            memmove(&q->events[i], &q->events[i+1], (q->count-i-1)*sizeof(NcEvent));
            --q->count;
            --i;
        }
    }
} 