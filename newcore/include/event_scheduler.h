#ifndef NC_EVENT_SCHEDULER_H
#define NC_EVENT_SCHEDULER_H

#define NC_EVENT_VBLANK 1
#define NC_EVENT_DMA 2

typedef struct NcEventQueue {
    int current_cycle;
} NcEventQueue;

void nc_eventq_init(NcEventQueue* q);
void nc_eventq_schedule(NcEventQueue* q, int event, int when, void (*handler)(void*), void* ctx);
void nc_eventq_dispatch_due(NcEventQueue* q);

#endif // NC_EVENT_SCHEDULER_H 