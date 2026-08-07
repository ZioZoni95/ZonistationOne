/* SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 ZioZoni95
 *
 * Part of ZoniStation One, a PlayStation 1 emulator.
 * See LICENSE for the full licence text and THIRD-PARTY.md for the
 * components of this project that have other authors.
 *
 * WSOLA time-stretch. The algorithm is the published one (Verhelst & Roelands,
 * "An overlap-add technique based on waveform similarity", ICASSP 1993): pick
 * the position in a search window whose waveform best matches the tail of the
 * block already emitted, cross-fade the two, copy the rest of the block through,
 * then advance the read position by tempo x the block's output length. No code
 * from any other implementation is used here.
 */

#include "spu_stretch.h"

#include <math.h>
#include <string.h>

/* Correlation is computed on the L+R sum and on every other frame. Both halve
 * the cost of the search; neither moves the chosen offset by enough to hear,
 * because what is being located is a pitch period, not a sample. */
#define CORR_STEP 2

void spu_stretch_reset(SpuStretch *st)
{
    memset(st, 0, sizeof(*st));
    st->tempo  = 1.0;
    st->primed = false;
}

void spu_stretch_set_tempo(SpuStretch *st, double tempo)
{
    if (tempo < SPU_STRETCH_TEMPO_MIN) tempo = SPU_STRETCH_TEMPO_MIN;
    if (tempo > SPU_STRETCH_TEMPO_MAX) tempo = SPU_STRETCH_TEMPO_MAX;
    st->tempo = tempo;
}

int spu_stretch_input_room(const SpuStretch *st)
{
    return SPU_STRETCH_IN_CAP - st->in_len;
}

int spu_stretch_push(SpuStretch *st, const int16_t *frames, int count)
{
    int room = spu_stretch_input_room(st);
    if (count > room) count = room;
    if (count <= 0) return 0;
    memcpy(&st->in[st->in_len * 2], frames, (size_t)count * 2 * sizeof(int16_t));
    st->in_len += count;
    return count;
}

int spu_stretch_queued(const SpuStretch *st)
{
    return st->in_len + st->out_len;
}

/* Normalised cross-correlation of the previous block's tail against the input
 * at `offset`. Normalising by the candidate's own energy is what stops the
 * search from simply picking the loudest position in the window. */
static double corr_at(const SpuStretch *st, int offset)
{
    double num = 0.0, den = 0.0;
    const int16_t *a = st->mid;
    const int16_t *b = &st->in[offset * 2];
    for (int i = 0; i < SPU_STRETCH_OVERLAP; i += CORR_STEP) {
        double am = (double)a[i * 2] + (double)a[i * 2 + 1];
        double bm = (double)b[i * 2] + (double)b[i * 2 + 1];
        num += am * bm;
        den += bm * bm;
    }
    return num / sqrt(den + 1.0);
}

static int seek_best_offset(const SpuStretch *st)
{
    int    best   = 0;
    double best_c = -1e30;
    for (int off = 0; off <= SPU_STRETCH_SEEK; off++) {
        double c = corr_at(st, off);
        if (c > best_c) { best_c = c; best = off; }
    }
    return best;
}

/* One block: cross-fade OVERLAP frames, copy SEQ-2*OVERLAP straight, keep the
 * last OVERLAP as the tail the next block will be matched against. */
static void process_block(SpuStretch *st)
{
    const int copy_len = SPU_STRETCH_SEQ - 2 * SPU_STRETCH_OVERLAP;
    const int out_len  = SPU_STRETCH_SEQ - SPU_STRETCH_OVERLAP;

    /* Startup: with mid still zero the first cross-fade would fade in from
     * silence over 10 ms. Seeding it from the head of the input makes the first
     * block open at full level like every later one. */
    if (!st->primed) {
        memcpy(st->mid, st->in, (size_t)SPU_STRETCH_OVERLAP * 2 * sizeof(int16_t));
        st->primed = true;
    }

    /* At tempo 1.0 offset 0 makes the cross-fade region the previous block's own
     * tail overlapped with itself, which reconstructs the input exactly. */
    const int off = (st->tempo == 1.0) ? 0 : seek_best_offset(st);

    int16_t *o = &st->out[st->out_len * 2];
    const int16_t *b = &st->in[off * 2];

    for (int i = 0; i < SPU_STRETCH_OVERLAP; i++) {
        /* Linear cross-fade. The offset search has already put the two in
         * phase, so a longer-tailed window buys nothing here. */
        int32_t w  = i;
        int32_t iw = SPU_STRETCH_OVERLAP - i;
        o[i * 2]     = (int16_t)(((int32_t)st->mid[i * 2]     * iw +
                                  (int32_t)b[i * 2]           * w) / SPU_STRETCH_OVERLAP);
        o[i * 2 + 1] = (int16_t)(((int32_t)st->mid[i * 2 + 1] * iw +
                                  (int32_t)b[i * 2 + 1]       * w) / SPU_STRETCH_OVERLAP);
    }
    memcpy(&o[SPU_STRETCH_OVERLAP * 2], &b[SPU_STRETCH_OVERLAP * 2],
           (size_t)copy_len * 2 * sizeof(int16_t));

    memcpy(st->mid, &b[(SPU_STRETCH_SEQ - SPU_STRETCH_OVERLAP) * 2],
           (size_t)SPU_STRETCH_OVERLAP * 2 * sizeof(int16_t));

    st->out_len += out_len;
    if (st->tempo != 1.0) st->blocks_stretched++;

    /* Advance the input by tempo x the output length, carrying the fraction so
     * the ratio is exact over time rather than per block. */
    double skip_f = st->tempo * (double)out_len + st->skip_frac;
    int    skip   = (int)skip_f;
    st->skip_frac = skip_f - (double)skip;
    if (skip > st->in_len) skip = st->in_len;

    st->in_len -= skip;
    if (st->in_len > 0)
        memmove(st->in, &st->in[skip * 2], (size_t)st->in_len * 2 * sizeof(int16_t));
}

int spu_stretch_pull(SpuStretch *st, int16_t *frames, int count)
{
    const int need_in  = SPU_STRETCH_SEQ + SPU_STRETCH_SEEK;
    const int block_out = SPU_STRETCH_SEQ - SPU_STRETCH_OVERLAP;

    while (st->out_len < count) {
        if (st->in_len < need_in) break;
        if (st->out_len + block_out > SPU_STRETCH_OUT_CAP) break;
        process_block(st);
    }

    int n = (count < st->out_len) ? count : st->out_len;
    if (n <= 0) return 0;

    memcpy(frames, st->out, (size_t)n * 2 * sizeof(int16_t));
    st->out_len -= n;
    if (st->out_len > 0)
        memmove(st->out, &st->out[n * 2], (size_t)st->out_len * 2 * sizeof(int16_t));
    return n;
}
