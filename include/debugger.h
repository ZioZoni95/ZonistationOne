/* SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 ZioZoni95
 *
 * Part of ZoniStation One, a PlayStation 1 emulator.
 * See LICENSE for the full licence text and THIRD-PARTY.md for the
 * components of this project that have other authors.
 */
#ifndef DEBUGGER_H
#define DEBUGGER_H

#include <stdint.h>
#include <stdbool.h>

// Forward-declare Cpu to avoid circular include (cpu.h → interconnect.h → debugger.h → cpu.h)
struct Cpu;

#define MAX_BREAKPOINTS 16
#define MAX_WATCHPOINTS 16

/* Membership filter over watched word addresses.
 *
 * The checks below sit on the two hottest paths in the emulator — one per
 * instruction for breakpoints, one per load and per store for watchpoints — and
 * each was a linear scan of its list. A perf profile of a 30 s run put the three
 * of them at 2.92% of all samples while finding nothing, because no breakpoint
 * and no watchpoint was set; and the moment one *is* set, every memory access in
 * the machine pays a walk over the list.
 *
 * 1024 bits over the watched word addresses turns the common answer — "no" —
 * into one load and one bit test, with no false negatives, so a miss is decided
 * exactly. A hash collision can only produce a false *positive*, which costs the
 * old linear scan on that access alone. 128 bytes, two cache lines, rebuilt
 * whenever the list changes, which is a human-scale event. */
#define DBG_FILTER_BITS  10
#define DBG_FILTER_WORDS (1u << (DBG_FILTER_BITS - 5))   /* 32 words = 1024 bits */

typedef struct {
    uint32_t breakpoints[MAX_BREAKPOINTS];
    bool     bp_enabled[MAX_BREAKPOINTS];
    uint32_t breakpoint_count;

    uint32_t read_watchpoints[MAX_WATCHPOINTS];
    uint32_t read_watchpoint_count;
    uint32_t write_watchpoints[MAX_WATCHPOINTS];
    uint32_t write_watchpoint_count;

    uint32_t bp_filter[DBG_FILTER_WORDS];
    uint32_t rw_filter[DBG_FILTER_WORDS];
    uint32_t ww_filter[DBG_FILTER_WORDS];

    bool paused;
    bool step_skip_bp; // Skip BP check for one instruction when stepping off a breakpoint
} Debugger;

/* Knuth multiplicative hash of the word address, folded to DBG_FILTER_BITS. */
static inline uint32_t dbg_hash(uint32_t addr) {
    return ((addr >> 2) * 2654435761u) >> (32 - DBG_FILTER_BITS);
}
static inline bool dbg_filter_test(const uint32_t* filter, uint32_t addr) {
    uint32_t h = dbg_hash(addr);
    return (filter[h >> 5] & (1u << (h & 31u))) != 0;
}

void debugger_init(Debugger* dbg);

bool debugger_add_breakpoint(Debugger* dbg, uint32_t addr);
bool debugger_remove_breakpoint(Debugger* dbg, uint32_t addr);

bool debugger_add_read_watchpoint(Debugger* dbg, uint32_t addr);
bool debugger_remove_read_watchpoint(Debugger* dbg, uint32_t addr);
bool debugger_add_write_watchpoint(Debugger* dbg, uint32_t addr);
bool debugger_remove_write_watchpoint(Debugger* dbg, uint32_t addr);

/* The three hot checks: one per instruction, one per load, one per store.
 *
 * The membership filter below them turned the common answer into a load and a
 * bit test, and that was worth 2.92% of samples when it landed. It is not
 * enough. A 70 s profile now puts the three at 4.19%, 1.85% and 0.60% — 6.6% of
 * every cycle in the process — and they are still finding nothing, because the
 * usual state of a run is that no breakpoint and no watchpoint exists at all.
 * What is left to pay for is the cross-unit call and the filter's own cache
 * line, which is cold precisely because nothing ever hits it.
 *
 * So the exact "nobody armed anything" answer moves inline and reads a counter
 * that is already in cache — the Debugger sits inside the Interconnect, which
 * the loop touches every instruction anyway. The filter and the list walk stay
 * out of line, where they belong: they only run once something is armed. */
void debugger_breakpoint_slow(Debugger* dbg, struct Cpu* cpu);
void debugger_read_watchpoint_slow(Debugger* dbg, struct Cpu* cpu, uint32_t addr, uint32_t size);
void debugger_write_watchpoint_slow(Debugger* dbg, struct Cpu* cpu, uint32_t addr, uint32_t size);

static inline void debugger_check_breakpoint(Debugger* dbg, struct Cpu* cpu) {
    if (dbg->breakpoint_count != 0) debugger_breakpoint_slow(dbg, cpu);
}
static inline void debugger_check_read_watchpoint(Debugger* dbg, struct Cpu* cpu,
                                                  uint32_t addr, uint32_t size) {
    if (dbg->read_watchpoint_count != 0) debugger_read_watchpoint_slow(dbg, cpu, addr, size);
}
static inline void debugger_check_write_watchpoint(Debugger* dbg, struct Cpu* cpu,
                                                   uint32_t addr, uint32_t size) {
    if (dbg->write_watchpoint_count != 0) debugger_write_watchpoint_slow(dbg, cpu, addr, size);
}
void debugger_handle_break(Debugger* dbg, struct Cpu* cpu, const char* reason);

#endif // DEBUGGER_H