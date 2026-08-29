/* SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 ZioZoni95
 *
 * Part of ZoniStation One, a PlayStation 1 emulator.
 * See LICENSE for the full licence text and THIRD-PARTY.md for the
 * components of this project that have other authors.
 */
#include "golden_trace.h"

#include <stdio.h>
#include <stdlib.h>
#include "cpu.h"
#include "interconnect.h"
#include "log.h"

/* FNV-1a, 64-bit. Chosen for being three lines and order-sensitive; this is a
 * change detector, not a security primitive. */
#define FNV_OFFSET 1469598103934665603ULL
#define FNV_PRIME  1099511628211ULL

static inline uint64_t fnv(uint64_t h, uint32_t v) {
    for (int i = 0; i < 4; i++) {
        h ^= (uint8_t)(v >> (i * 8));
        h *= FNV_PRIME;
    }
    return h;
}

bool zs1_trace_active;
bool zs1_trace_stopped;

static FILE*    s_out;
static uint64_t s_path = FNV_OFFSET;
static uint64_t s_count;
static uint64_t s_every = 1u << 20;
static uint64_t s_stop  = 64ull << 20;
#define s_done zs1_trace_stopped

static uint64_t env_u64(const char* name, uint64_t dflt) {
    const char* s = getenv(name);
    if (!s || !*s) return dflt;
    char* end = NULL;
    unsigned long long v = strtoull(s, &end, 0);
    return (end && end != s && v > 0) ? (uint64_t)v : dflt;
}

void zs1_trace_init(void) {
    const char* path = getenv("ZS1_TRACE");
    if (!path || !*path) return;

    s_every = env_u64("ZS1_TRACE_EVERY", s_every);
    s_stop  = env_u64("ZS1_TRACE_STOP",  s_stop);

    s_out = fopen(path, "w");
    if (!s_out) {
        LOG_SYSTEM_ERROR("[TRACE] cannot open %s", path);
        return;
    }
    /* The header is part of the diff on purpose: a trace taken with different
     * parameters is not comparable, and this makes that a one-line mismatch
     * instead of a silent one. */
    fprintf(s_out, "# zs1 golden trace v1 every=%llu stop=%llu\n",
            (unsigned long long)s_every, (unsigned long long)s_stop);
    fprintf(s_out, "# instr cycle pc path_hash state_hash\n");
    zs1_trace_active = true;
    LOG_SYSTEM_INFO("[TRACE] golden trace -> %s (every %llu, stop %llu)",
                    path, (unsigned long long)s_every, (unsigned long long)s_stop);
}

/* The COP0 registers folded in are the ones the machine actually uses: SR,
 * Cause and EPC drive the exception path, and the four breakpoint registers are
 * general-purpose storage to at least one game (Dino Crisis keeps its LibCrypt
 * table pointer in BDAM — see the note on Cpu). Leaving those out would let a
 * change to COP0 handling pass. */
static uint64_t state_hash(const Cpu* cpu) {
    uint64_t h = FNV_OFFSET;
    for (int i = 0; i < 32; i++) h = fnv(h, cpu->regs[i]);
    h = fnv(h, cpu->hi);
    h = fnv(h, cpu->lo);
    h = fnv(h, cpu->pc);
    h = fnv(h, cpu->next_pc);
    h = fnv(h, cpu->sr);
    h = fnv(h, cpu->cause);
    h = fnv(h, cpu->epc);
    h = fnv(h, cpu->cop0_bpc);
    h = fnv(h, cpu->cop0_bda);
    h = fnv(h, cpu->cop0_bdam);
    h = fnv(h, cpu->cop0_bpcm);
    /* The load-delay slots are state, and the one bug this harness was written
     * after lived entirely in them. */
    h = fnv(h, cpu->load_reg_idx);
    h = fnv(h, cpu->load_value);
    h = fnv(h, cpu->delay_load_reg);
    h = fnv(h, cpu->delay_load_value);
    return h;
}

void zs1_trace_fold(const Cpu* cpu, uint32_t instruction) {
    if (s_done) return;

    s_path = fnv(s_path, cpu->current_pc);
    s_path = fnv(s_path, instruction);
    s_count++;

    if (s_count % s_every == 0 || s_count == s_stop) {
        fprintf(s_out, "%llu %u %08X %016llX %016llX\n",
                (unsigned long long)s_count,
                cpu->inter->cpu_cycle_counter,
                cpu->current_pc,
                (unsigned long long)s_path,
                (unsigned long long)state_hash(cpu));
    }
    if (s_count >= s_stop) { s_done = true; zs1_trace_active = false; }
}

void zs1_trace_finish(void) {
    zs1_trace_active = false;
    if (!s_out) return;
    fprintf(s_out, "# end instr=%llu\n", (unsigned long long)s_count);
    fclose(s_out);
    s_out = NULL;
}

