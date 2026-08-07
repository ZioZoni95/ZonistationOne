/* SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 ZioZoni95
 *
 * Part of ZoniStation One, a PlayStation 1 emulator.
 * See LICENSE for the full licence text and THIRD-PARTY.md for the
 * components of this project that have other authors.
 */
#ifndef SPU_STRETCH_H
#define SPU_STRETCH_H

#include <stdint.h>
#include <stdbool.h>

/* Time-stretch on the consumer side of the SPU ring.
 *
 * The producer generates samples on the emulated clock; the audio device drains
 * them on the host clock. The two are never exactly equal, so the ring level
 * wanders. Left alone, the wander ends in one of the two audible failures: the
 * callback finds the ring empty and writes silence (a click), or the ring is
 * full and generated samples are discarded (a skip). Both are heard as a cut.
 *
 * The fix is to let the *consumption rate* absorb the difference. Reading the
 * ring at tempo T while still emitting 44100 output frames per second changes
 * how fast the queue drains without changing playback pitch, so a queue that is
 * filling up is drained faster and one that is running dry is drained slower.
 * The stretch itself is WSOLA (waveform-similarity overlap-add): each output
 * block is taken from the position inside a search window whose waveform best
 * matches the tail of the previous block, then cross-faded into it, so the
 * splice lands on a matching phase instead of a discontinuity.
 *
 * At tempo exactly 1.0 the search is skipped and offset 0 is used. That case is
 * bit-exact passthrough — the cross-fade region is then the previous block's own
 * tail overlapped with itself — so nothing is paid, and nothing is coloured,
 * while the queue sits inside the dead band.
 *
 * Window sizes are the usual ones for speech and music at 44.1 kHz.
 */

#define SPU_STRETCH_RATE        44100
#define SPU_STRETCH_SEQ_MS         30   /* block taken from the input */
#define SPU_STRETCH_SEEK_MS        20   /* how far the match is searched */
#define SPU_STRETCH_OVERLAP_MS     10   /* cross-fade length */

#define SPU_STRETCH_SEQ      ((SPU_STRETCH_RATE * SPU_STRETCH_SEQ_MS)     / 1000) /* 1323 */
#define SPU_STRETCH_SEEK     ((SPU_STRETCH_RATE * SPU_STRETCH_SEEK_MS)    / 1000) /*  882 */
#define SPU_STRETCH_OVERLAP  ((SPU_STRETCH_RATE * SPU_STRETCH_OVERLAP_MS) / 1000) /*  441 */

/* A block cannot be produced until one block plus its whole search window is in
 * hand, so SEQ+SEEK frames are working set, not cushion — the controller has to
 * hold that much on top of whatever queue it is aiming for. The rest of the
 * capacity is that queue: the ring gets drained into here every callback, so
 * this has to be able to hold all of it. */
#define SPU_STRETCH_WORKING  (SPU_STRETCH_SEQ + SPU_STRETCH_SEEK)
#define SPU_STRETCH_IN_CAP   (SPU_STRETCH_WORKING + 4096)
/* Output must hold one produced block plus what a short callback left behind. */
#define SPU_STRETCH_OUT_CAP  (2 * (SPU_STRETCH_SEQ - SPU_STRETCH_OVERLAP))

/* Tempo limits. Beyond these the artefacts stop being worth the correction, and
 * a queue that far out is a real stall rather than clock wander. */
#define SPU_STRETCH_TEMPO_MIN  0.80
#define SPU_STRETCH_TEMPO_MAX  1.25

typedef struct {
    int16_t in[SPU_STRETCH_IN_CAP * 2];       /* interleaved L,R */
    int     in_len;                            /* frames */
    int16_t out[SPU_STRETCH_OUT_CAP * 2];
    int     out_len;                           /* frames */
    int16_t mid[SPU_STRETCH_OVERLAP * 2];      /* tail of the previous block */
    double  tempo;                             /* input frames per output frame */
    double  skip_frac;                         /* fractional part of the skip carried over */
    bool    primed;                            /* mid holds audio, not startup silence */
    /* Counters — the point of the whole mechanism is that these stay flat. */
    uint32_t blocks_stretched;                 /* blocks emitted at tempo != 1 */
    uint32_t activations;                      /* dead band exits */
} SpuStretch;

void spu_stretch_reset(SpuStretch *st);

/* Tempo is applied from the next block; a block already in the output FIFO is
 * not revisited. Values outside the limits are clamped. */
void spu_stretch_set_tempo(SpuStretch *st, double tempo);

/* Frames the input FIFO can still take. */
int  spu_stretch_input_room(const SpuStretch *st);

/* Append input frames. Returns how many were taken. */
int  spu_stretch_push(SpuStretch *st, const int16_t *frames, int count);

/* Emit up to `count` output frames. Returns how many were produced; short
 * returns mean the input FIFO could not fill another block. */
int  spu_stretch_pull(SpuStretch *st, int16_t *frames, int count);

/* Frames held inside the stretcher, input and output side together. Queue depth
 * for the tempo controller has to count these or it will chase its own tail. */
int  spu_stretch_queued(const SpuStretch *st);

#endif /* SPU_STRETCH_H */
