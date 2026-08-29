/* SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 ZioZoni95
 *
 * Part of ZoniStation One, a PlayStation 1 emulator.
 * See LICENSE for the full licence text and THIRD-PARTY.md for the
 * components of this project that have other authors.
 */
#ifndef CPU_REC_H
#define CPU_REC_H

#include <stdint.h>
#include <stdbool.h>
#include "cpu_blocks.h"

/* The recompiler: native x86-64 for a block the block cache already found.
 *
 * It sits on top of cpu_blocks.c rather than beside it. Block discovery, the
 * cache and the i-cache revalidation are that file's, are already verified
 * bit-identical to the interpreter, and are not re-implemented here — this only
 * turns a RecBlock's ops into code.
 *
 * Hand-written encodings rather than an assembler library: PCSX-Redux uses
 * xbyak, which is C++ and a large dependency for a project whose rule is C99
 * with C++ confined to debug_ui.cpp. The instruction subset a MIPS interpreter
 * body needs is small enough to encode directly, and doing so keeps the emitted
 * code obvious at the point where correctness matters most.
 *
 * What is folded at compile time, and is the whole reason this is faster than
 * the block cache:
 *   - current_pc, pc and next_pc are constants inside a block, so their updates
 *     become immediate stores instead of loads and adds.
 *   - the A0h/B0h/C0h vector test is decided per instruction at compile time.
 *     Three comparisons per instruction disappear from every block that does not
 *     contain a vector, which is all but three of them.
 *   - the one-cycle-per-instruction part of the cycle cost is a constant, summed
 *     once at the end of the block. The data-dependent part — the RAM load stall
 *     the bus accumulates — still lands per instruction, because it has to.
 *
 * What is not folded, because it changes the machine:
 *   - the load-delay rotation, which is state the next instruction reads;
 *   - the interrupt check, which has to happen between instructions;
 *   - anything an exception can interrupt. A block abandons at the instruction
 *     that raised, and the interpreter is what runs next.
 */

/* Allocates the code cache. False if the mapping failed, in which case the
 * caller stays on the block cache and says so through cpu_exec_set_active(). */
bool cpu_rec_init(void);
void cpu_rec_shutdown(void);

/* Drop every compiled block. For a reset, and whenever a RecBlock's shape
 * changes under it. */
void cpu_rec_flush(void);

/* Compiled entry point for this block, compiling it on first use. NULL when the
 * block holds something the emitter does not cover yet, which is not an error:
 * the caller runs it through the block cache instead. */
typedef uint32_t (*RecEntry)(struct Cpu* cpu);
RecEntry cpu_rec_entry(const RecBlock* b, uint32_t vaddr);

/* Bytes of native code emitted so far, for the Host HW panel. */
uint32_t cpu_rec_code_bytes(void);

#endif /* CPU_REC_H */
