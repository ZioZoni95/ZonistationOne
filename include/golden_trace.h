/* SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 ZioZoni95
 *
 * Part of ZoniStation One, a PlayStation 1 emulator.
 * See LICENSE for the full licence text and THIRD-PARTY.md for the
 * components of this project that have other authors.
 */
#ifndef GOLDEN_TRACE_H
#define GOLDEN_TRACE_H

#include <stdint.h>
#include <stdbool.h>

/* The golden trace: a reproducible fingerprint of everything the CPU did.
 *
 * It exists to answer one question nothing else in this repository can — "is
 * this still the same machine?". Every accuracy claim in CLAUDE.md was
 * established by running a game and reading a log, and the LWL/LWR bug showed
 * what that costs: months live, most of a day to find, and a defect that a boot
 * and a CPI reading cannot see, because it was data-dependent
 * (docs/TESTING_PLAN_2026-08-20.md).
 *
 * Two hashes, because they fail differently:
 *
 *   path   folded over (current_pc, instruction) for every instruction executed.
 *          Diverges on the first instruction that goes somewhere else, which
 *          localises a control-flow change to one checkpoint interval.
 *   state  folded over the register file, HI/LO, the COP0 registers the machine
 *          actually uses and both load-delay slots, at each checkpoint. Catches
 *          a wrong *value* on an otherwise identical path — the LWL/LWR shape.
 *
 * Runtime-gated rather than compiled out, deliberately: the harness is worth
 * more when it exercises the binary that ships than a variant of it, and the
 * cost of being switched off is one load and one perfectly-predicted branch on
 * a loop that already does thirty-odd operations per instruction.
 *
 * A trace is only meaningful against a machine that is reproducible, which this
 * one is not by default — see ZS1_CD_SYNC in cdrom_disc.c. Capture with it set.
 */

struct Cpu;

/* Non-zero once a trace is open. Read once per instruction; the fold itself is
 * out of line so the fast path stays two instructions. */
extern bool zs1_trace_active;

void zs1_trace_init(void);   /* reads ZS1_TRACE / ZS1_TRACE_EVERY / ZS1_TRACE_STOP */
void zs1_trace_fold(const struct Cpu* cpu, uint32_t instruction);
/* True once ZS1_TRACE_STOP instructions have run. Inline and reading a flag,
 * because the frame loop asks once per instruction: as a cross-unit call it was
 * 1.55% of the emulation thread, for a function whose answer is almost always
 * the same "no". */
extern bool zs1_trace_stopped;
static inline bool zs1_trace_done(void) { return zs1_trace_stopped; }
void zs1_trace_finish(void);

static inline void zs1_trace_step(const struct Cpu* cpu, uint32_t instruction) {
    if (zs1_trace_active) zs1_trace_fold(cpu, instruction);
}

#endif /* GOLDEN_TRACE_H */
