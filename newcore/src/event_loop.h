// event_loop.h
// Migrated from event_scheduler.c: event/timer scheduling logic (header)
// TODO: Move event/timer scheduling declarations here.

#ifndef EVENT_LOOP_H
#define EVENT_LOOP_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// --- Event Loop State ---
typedef struct {
    // TODO: Add event queue, timer state, etc.
} EventLoop;

// --- Event Loop API ---
void event_loop_init(EventLoop* loop);
void event_loop_schedule(EventLoop* loop, uint32_t cycles, void (*handler)(void*), void* ctx);
void event_loop_dispatch(EventLoop* loop, uint32_t current_cycle);

#endif // EVENT_LOOP_H 