/* SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 ZioZoni95
 *
 * Part of ZoniStation One, a PlayStation 1 emulator.
 * See LICENSE for the full licence text and THIRD-PARTY.md for the
 * components of this project that have other authors.
 */
#ifndef FRAME_EVENTS_H
#define FRAME_EVENTS_H

#include <stdbool.h>
#include <stdint.h>

/* Per-frame event ring with cycle timestamps (debug UI Phase 4).
 *
 * The renderer already records the *order* of VRAM uploads and draw batches,
 * because replaying them out of order corrupts a texture page re-uploaded
 * mid-frame. What it does not record is *when* — and "thirteen of twenty
 * columns never arrived" is a question about time against the frame budget,
 * not about order.
 *
 * Recording is off unless the Frame view is visible: the standing constraint on
 * this interface is that the panels cost, not the core. With it off,
 * frame_events_record() is a load, a test and a return.
 *
 * Single-producer. Everything here is called from the emulation thread, except
 * frame_events_last() which the UI thread reads between frames. */

typedef enum {
    FEV_VRAM_UPLOAD = 0,   /* CPU/DMA -> VRAM transfer completed; detail = pixels */
    FEV_VRAM_COPY,         /* VRAM -> VRAM copy;                  detail = pixels */
    FEV_DRAW_BATCH,        /* a draw batch was recorded;          detail = vertices */
    FEV_XA_SECTOR,         /* an XA sector was decoded;           detail = samples */
    FEV_DMA_GPU,           /* DMA channel 2 block completed;      detail = words */
    FEV_FLIP,              /* the display region changed (page flip) */
    FEV_TYPE_COUNT
} FrameEventType;

typedef struct {
    uint32_t cycle;        /* inter->cpu_cycle_counter when recorded */
    uint32_t detail;
    uint8_t  type;
} FrameEvent;

#define FRAME_EVENT_CAPACITY 4096

typedef struct {
    FrameEvent events[FRAME_EVENT_CAPACITY];
    uint32_t   count;                     /* entries actually stored */
    uint32_t   dropped;                   /* events past capacity */
    uint32_t   type_count[FEV_TYPE_COUNT];/* totals, including dropped ones */
    uint32_t   start_cycle;
    uint32_t   end_cycle;
} FrameEventFrame;

/* Bind the monotonic cycle counter the timestamps are read from. Called once,
 * after the Interconnect is constructed. */
void frame_events_bind_clock(const uint32_t* cycle_counter);

void frame_events_set_enabled(bool enabled);
bool frame_events_enabled(void);

/* Hot path — callers do not need to guard, but may. */
void frame_events_record(FrameEventType type, uint32_t detail);

/* Close the frame under construction and publish it. */
void frame_events_end_frame(void);

/* The most recently published frame. Never NULL. */
const FrameEventFrame* frame_events_last(void);

#endif /* FRAME_EVENTS_H */
