#include "frame_events.h"

#include <string.h>

/* Two frames: one being filled by the emulation thread, one published for the
 * UI. Swapped at frame_events_end_frame(), which the UI thread cannot observe
 * mid-write because it only ever reads the published index. */
static FrameEventFrame s_frames[2];
static int             s_write_idx;
static int             s_read_idx = 1;

static const uint32_t* s_clock;
static bool            s_enabled;

static uint32_t now_cycle(void) { return s_clock ? *s_clock : 0; }

void frame_events_bind_clock(const uint32_t* cycle_counter) {
    s_clock = cycle_counter;
    memset(s_frames, 0, sizeof(s_frames));
    s_write_idx = 0;
    s_read_idx  = 1;
    s_frames[s_write_idx].start_cycle = now_cycle();
}

void frame_events_set_enabled(bool enabled) {
    if (enabled == s_enabled) return;
    s_enabled = enabled;
    if (enabled) {
        /* Start clean: a ring half-filled from whenever it was last on would
         * put stale events on the same axis as live ones. */
        FrameEventFrame* f = &s_frames[s_write_idx];
        f->count = 0;
        f->dropped = 0;
        memset(f->type_count, 0, sizeof(f->type_count));
        f->start_cycle = now_cycle();
    }
}

bool frame_events_enabled(void) { return s_enabled; }

void frame_events_record(FrameEventType type, uint32_t detail) {
    if (!s_enabled || (unsigned)type >= FEV_TYPE_COUNT) return;

    FrameEventFrame* f = &s_frames[s_write_idx];
    f->type_count[type]++;

    if (f->count >= FRAME_EVENT_CAPACITY) { f->dropped++; return; }

    FrameEvent* e = &f->events[f->count++];
    e->cycle  = now_cycle();
    e->detail = detail;
    e->type   = (uint8_t)type;
}

void frame_events_end_frame(void) {
    if (!s_enabled) return;

    FrameEventFrame* f = &s_frames[s_write_idx];
    f->end_cycle = now_cycle();

    s_read_idx  = s_write_idx;
    s_write_idx = 1 - s_write_idx;

    FrameEventFrame* n = &s_frames[s_write_idx];
    n->count   = 0;
    n->dropped = 0;
    memset(n->type_count, 0, sizeof(n->type_count));
    n->start_cycle = f->end_cycle;
    n->end_cycle   = f->end_cycle;
}

const FrameEventFrame* frame_events_last(void) { return &s_frames[s_read_idx]; }
