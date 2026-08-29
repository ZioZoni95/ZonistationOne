/* SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 ZioZoni95
 *
 * Part of ZoniStation One, a PlayStation 1 emulator.
 * See LICENSE for the full licence text and THIRD-PARTY.md for the
 * components of this project that have other authors.
 */
#ifndef CPU_BLOCKS_H
#define CPU_BLOCKS_H

#include <stdint.h>
#include <stdbool.h>
#include "cpu.h"

/* The block cache — the recompiler's front end.
 *
 * A block is a run of instructions decoded once and kept: the handler is already
 * resolved (SPECIAL included, so the second dispatch through s_special_table is
 * gone) and the instruction word is beside it. Running a block costs one lookup
 * instead of one i-cache tag comparison per instruction, which a 70 s profile
 * put at 17.3% of the emulation thread — the second largest item in it.
 *
 * No code is emitted here. This exists first so that block discovery, the cache
 * and invalidation are all validated against the golden trace before anything
 * generates x86-64, because a defect in this half would otherwise be discovered
 * through the other half.
 *
 * ---------------------------------------------------------------------------
 * Why there is no invalidation machinery
 *
 * The obvious design keeps a dirty-page bitmap and drops blocks when the guest
 * writes into one. This does not, and the reason is worth stating because it
 * makes the whole thing exact rather than approximately right.
 *
 * The interpreter does not read instructions from memory: it reads them from the
 * i-cache, which on an R3000A does not snoop writes. Code written by the CPU or
 * by DMA is *not* seen until the line is evicted or the guest flushes it — that
 * is hardware behaviour, and it is what this emulator already does. So a block
 * is valid for exactly as long as the i-cache lines behind it are valid with the
 * same tag, and no longer.
 *
 * That is what a block re-entry checks. The lines a block spans are replayed on
 * every execution and the block is compared against what they now hold; anything
 * that changed is re-decoded. The i-cache is the invalidation mechanism, exactly
 * as on the real machine, and self-modifying code behaves identically to the
 * interpreter without a single extra test on the store path.
 *
 * The comparison happens on every entry and not only when a line had to be
 * filled, because the interesting case is the one where it did not: a block
 * built while its line was invalid reads memory, the memory changes, some other
 * fetch fills the line with the new bytes, and this block comes back to find its
 * line valid and its own copy stale. That cost a divergence at 450M instructions
 * before ZS1_BLOCKS_VERIFY pinned it. Comparing is what is cheap here — four
 * word comparisons per line, about one per instruction — while decoding is not,
 * so the compare is unconditional and the decode is not.
 *
 * Two consequences follow, and both are deliberate:
 *   - Lines are replayed *lazily*, immediately before the first instruction that
 *     lives in them, not all at block entry. An instruction can write the memory
 *     a later line of its own block reads, and filling early would let the block
 *     see the old bytes where the interpreter sees the new ones.
 *   - KSEG1 is uncached, so a fetch there always goes to memory and a block
 *     could never be reused. Those addresses fall back to the interpreter.
 */

#define REC_BLOCK_MAX_OPS   32
#define REC_BLOCK_MAX_LINES ((REC_BLOCK_MAX_OPS / ICACHE_LINE_WORDS) + 1)

typedef struct {
    cpu_handler_t fn;           /* resolved handler, SPECIAL already dispatched */
    uint32_t      instruction;
} RecOp;

typedef struct {
    uint32_t paddr;             /* physical address of the first instruction */
    uint16_t count;             /* instructions in the block; 0 means empty slot */
    uint16_t line_count;

    /* One entry per distinct i-cache line the block spans, in execution order. */
    uint32_t line_paddr[REC_BLOCK_MAX_LINES];  /* line-aligned physical address */
    uint8_t  line_word0[REC_BLOCK_MAX_LINES];  /* first word index used in it */
    uint8_t  line_op0[REC_BLOCK_MAX_LINES];    /* index of its first op */

    RecOp    ops[REC_BLOCK_MAX_OPS];
} RecBlock;

/* Allocates the cache. Returns false if it could not, in which case the caller
 * must stay on the interpreter and say so through cpu_exec_set_active(). */
bool cpu_blocks_init(void);
void cpu_blocks_shutdown(void);

/* The block starting at this physical address, built if it is not cached.
 * NULL when this address cannot be blocked (uncached, or nothing decodable). */
RecBlock* cpu_blocks_lookup(Cpu* cpu, uint32_t vaddr, uint32_t paddr);

/* Drops every block. For a reset, and for the guest flushing its i-cache. */
void cpu_blocks_flush(void);

/* Re-decode the ops backing one i-cache line after it was refilled. Returns
 * false when the new bytes would change the block's shape — a branch where there
 * was none, or the reverse — in which case the caller must drop the block and
 * let the interpreter take that instruction. Called from the block runner, which
 * owns the lazy replay. */
bool cpu_blocks_reload_line(RecBlock* b, uint32_t line_index, const uint32_t* words);

#endif /* CPU_BLOCKS_H */
