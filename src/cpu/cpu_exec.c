/* SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 ZioZoni95
 *
 * Part of ZoniStation One, a PlayStation 1 emulator.
 * See LICENSE for the full licence text and THIRD-PARTY.md for the
 * components of this project that have other authors.
 */
#include "cpu_exec.h"

#include <stdlib.h>
#include <string.h>
#include "cpu_blocks.h"
#include "log.h"

static CpuExecStatus s_status = {
    .requested       = CPU_EXEC_INTERPRETER,
    .active          = CPU_EXEC_INTERPRETER,
    .fallback_reason = NULL,
};

static const char* const s_names[CPU_EXEC_MODE_COUNT] = {
    "Interpreter", "Block cache", "Recompiler"
};
static const char* const s_short[CPU_EXEC_MODE_COUNT] = {
    "INTERP", "BLOCKS", "JIT"
};

const char* cpu_exec_mode_name(CpuExecMode m) {
    return (m >= 0 && m < CPU_EXEC_MODE_COUNT) ? s_names[m] : "?";
}
const char* cpu_exec_mode_short(CpuExecMode m) {
    return (m >= 0 && m < CPU_EXEC_MODE_COUNT) ? s_short[m] : "?";
}

const CpuExecStatus* cpu_exec_status(void)     { return &s_status; }
CpuExecStatus*       cpu_exec_status_mut(void) { return &s_status; }

void cpu_exec_set_active(CpuExecMode active, const char* reason) {
    s_status.active = active;
    s_status.fallback_reason = (active == s_status.requested) ? NULL : reason;
    if (s_status.fallback_reason)
        LOG_CPU_WARN("[CPU] %s requested but running %s — %s",
                     cpu_exec_mode_name(s_status.requested),
                     cpu_exec_mode_name(active), reason);
}

void cpu_exec_init(void) {
    const char* s = getenv("ZS1_CPU");
    CpuExecMode want = CPU_EXEC_INTERPRETER;

    if (s && *s) {
        if      (strcmp(s, "interpreter") == 0) want = CPU_EXEC_INTERPRETER;
        else if (strcmp(s, "blocks")      == 0) want = CPU_EXEC_BLOCKS;
        else if (strcmp(s, "jit")         == 0) want = CPU_EXEC_RECOMPILER;
        else {
            LOG_CPU_WARN("[CPU] ZS1_CPU=\"%s\" not recognised "
                         "(want \"interpreter\", \"blocks\" or \"jit\") — using the interpreter", s);
            want = CPU_EXEC_INTERPRETER;
        }
    }
    s_status.requested = want;

    /* The recompiler is not built yet, so asking for it gets the block cache it
     * would sit on top of — which is the honest answer, and the interface says
     * so rather than quietly pretending it emitted anything. */
    switch (want) {
        case CPU_EXEC_INTERPRETER:
            cpu_exec_set_active(CPU_EXEC_INTERPRETER, NULL);
            break;
        case CPU_EXEC_BLOCKS:
            if (cpu_blocks_init()) cpu_exec_set_active(CPU_EXEC_BLOCKS, NULL);
            else                   cpu_exec_set_active(CPU_EXEC_INTERPRETER, "block cache would not allocate");
            break;
        case CPU_EXEC_RECOMPILER:
            if (cpu_blocks_init()) cpu_exec_set_active(CPU_EXEC_BLOCKS, "no code emitter yet — running its block cache");
            else                   cpu_exec_set_active(CPU_EXEC_INTERPRETER, "block cache would not allocate");
            break;
        default:
            cpu_exec_set_active(CPU_EXEC_INTERPRETER, NULL);
            break;
    }

    LOG_CPU_INFO("[CPU] execution engine: %s", cpu_exec_mode_name(s_status.active));
}
