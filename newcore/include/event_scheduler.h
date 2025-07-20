#ifndef NEWCORE_EVENT_SCHEDULER_H
#define NEWCORE_EVENT_SCHEDULER_H
#include <stdint.h>
#define NC_EVENT_QUEUE_SIZE 16

typedef enum {
    NC_EVENT_TIMER0,
    NC_EVENT_TIMER1,
    NC_EVENT_TIMER2,
    NC_EVENT_VBLANK,
    NC_EVENT_DMA,
    NC_EVENT_CDROM,
    NC_EVENT_SPU,
    NC_EVENT_OTHER
} NcEventType;

typedef struct {
    NcEventType type;
    uint32_t cycle;
    void (*handler)(void* ctx);
    void* ctx;
} NcEvent;

typedef struct {
    NcEvent events[NC_EVENT_QUEUE_SIZE];
    int count;
    uint32_t current_cycle;
} NcEventQueue;

void nc_eventq_init(NcEventQueue* q);
void nc_eventq_schedule(NcEventQueue* q, NcEventType type, uint32_t cycle, void (*handler)(void*), void* ctx);
void nc_eventq_dispatch_due(NcEventQueue* q);

#endif // NEWCORE_EVENT_SCHEDULER_H 