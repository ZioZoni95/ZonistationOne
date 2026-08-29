/* SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 ZioZoni95
 *
 * Part of ZoniStation One, a PlayStation 1 emulator.
 * See LICENSE for the full licence text and THIRD-PARTY.md for the
 * components of this project that have other authors.
 */
#ifndef CPU_EXEC_H
#define CPU_EXEC_H

#include <stdint.h>
#include <stdbool.h>

/* Which engine is running the guest's code.
 *
 * There is more than one now, and which one is live changes what a measurement
 * means and which failures are plausible — so the interface says it, rather than
 * leaving it to be inferred from the log. That is the same rule the Host HW
 * panel already follows for the GPU: "always check that line before treating a
 * rendering difference as an emulator bug" (CLAUDE.md).
 *
 * `requested` is what was asked for and `active` is what is running. They differ
 * when an engine could not start — a recompiler on an unsupported host, or one
 * that hit an instruction it cannot emit — and `fallback_reason` says why. The
 * interpreter is always available, so `active` is always something.
 */
typedef enum {
    CPU_EXEC_INTERPRETER = 0,  /* one instruction at a time — the reference */
    CPU_EXEC_BLOCKS,           /* cached decoded blocks, still interpreted */
    CPU_EXEC_RECOMPILER,       /* native x86-64 emitted per block */
    CPU_EXEC_MODE_COUNT
} CpuExecMode;

typedef struct {
    CpuExecMode requested;
    CpuExecMode active;
    const char* fallback_reason;   /* NULL when active == requested */

    /* Filled by the block engines; zero under the interpreter, which is the
     * honest answer rather than a hidden one. Counted per block, never per
     * instruction — this must not appear on the hot path. */
    uint64_t instr_from_cache;     /* instructions run out of a cached block */
    uint32_t blocks_live;          /* blocks currently in the cache */
    uint32_t blocks_built;         /* built this session */
    uint32_t blocks_invalidated;   /* dropped by a write into their page */
    uint32_t code_bytes;           /* native code emitted, recompiler only */
} CpuExecStatus;

/* Reads ZS1_CPU=interpreter|blocks|jit. Unknown values warn and fall back to the
 * interpreter rather than guessing. */
void cpu_exec_init(void);

const CpuExecStatus* cpu_exec_status(void);

/* "Interpreter", "Block cache", "Recompiler" — for the interface. */
const char* cpu_exec_mode_name(CpuExecMode m);

/* Short form for the machine bar's chip: "INTERP", "BLOCKS", "JIT". */
const char* cpu_exec_mode_short(CpuExecMode m);

/* Record a fallback, with the reason the interface will show. */
void cpu_exec_set_active(CpuExecMode active, const char* reason);

CpuExecStatus* cpu_exec_status_mut(void);

#endif /* CPU_EXEC_H */
