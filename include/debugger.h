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

typedef struct {
    uint32_t breakpoints[MAX_BREAKPOINTS];
    bool     bp_enabled[MAX_BREAKPOINTS];
    uint32_t breakpoint_count;

    uint32_t read_watchpoints[MAX_WATCHPOINTS];
    uint32_t read_watchpoint_count;
    uint32_t write_watchpoints[MAX_WATCHPOINTS];
    uint32_t write_watchpoint_count;

    bool paused;
    bool step_skip_bp; // Skip BP check for one instruction when stepping off a breakpoint
} Debugger;

void debugger_init(Debugger* dbg);

bool debugger_add_breakpoint(Debugger* dbg, uint32_t addr);
bool debugger_remove_breakpoint(Debugger* dbg, uint32_t addr);

bool debugger_add_read_watchpoint(Debugger* dbg, uint32_t addr);
bool debugger_remove_read_watchpoint(Debugger* dbg, uint32_t addr);
bool debugger_add_write_watchpoint(Debugger* dbg, uint32_t addr);
bool debugger_remove_write_watchpoint(Debugger* dbg, uint32_t addr);

void debugger_check_breakpoint(Debugger* dbg, struct Cpu* cpu);
void debugger_check_read_watchpoint(Debugger* dbg, struct Cpu* cpu, uint32_t addr, uint32_t size);
void debugger_check_write_watchpoint(Debugger* dbg, struct Cpu* cpu, uint32_t addr, uint32_t size);
void debugger_handle_break(Debugger* dbg, struct Cpu* cpu, const char* reason);

#endif // DEBUGGER_H