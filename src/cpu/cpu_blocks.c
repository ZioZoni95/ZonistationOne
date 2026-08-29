/* SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 ZioZoni95
 *
 * Part of ZoniStation One, a PlayStation 1 emulator.
 * See LICENSE for the full licence text and THIRD-PARTY.md for the
 * components of this project that have other authors.
 */
#include "cpu_blocks.h"

#include <stdlib.h>
#include <string.h>
#include "interconnect.h"
#include "cpu_exec.h"
#include "log.h"

/* Direct-mapped, and deliberately so. A chained cache would keep a block that a
 * conflict displaced, at the cost of a pointer walk on the hottest lookup in the
 * emulator; rebuilding a block is a few dozen table lookups and happens once. */
#define REC_HASH_BITS 14
#define REC_HASH_SIZE (1u << REC_HASH_BITS)

static RecBlock* s_cache;

static inline uint32_t rec_slot(uint32_t paddr) {
    return (paddr >> 2) & (REC_HASH_SIZE - 1);   /* instructions are word-aligned */
}

bool cpu_blocks_init(void) {
    if (s_cache) return true;
    s_cache = (RecBlock*)calloc(REC_HASH_SIZE, sizeof(RecBlock));
    if (!s_cache) {
        LOG_CPU_ERROR("[CPU] block cache: cannot allocate %zu bytes",
                      (size_t)REC_HASH_SIZE * sizeof(RecBlock));
        return false;
    }
    LOG_CPU_INFO("[CPU] block cache: %u slots, %zu KB",
                 (unsigned)REC_HASH_SIZE,
                 ((size_t)REC_HASH_SIZE * sizeof(RecBlock)) / 1024);
    return true;
}

void cpu_blocks_shutdown(void) { free(s_cache); s_cache = NULL; }

void cpu_blocks_flush(void) {
    if (!s_cache) return;
    for (uint32_t i = 0; i < REC_HASH_SIZE; i++) s_cache[i].count = 0;
    cpu_exec_status_mut()->blocks_live = 0;
}

/* Does this instruction end a block?
 *
 * Every branch and jump, because after its delay slot the next address is not
 * this block's business. SYSCALL and BREAK too: they always raise, so anything
 * built past them would never run.
 *
 * Nothing else needs to. An instruction that *can* raise — a misaligned load, an
 * overflowing ADD — is handled at run time, where the runner abandons the block
 * the moment cpu->exception_pending is set. */
static bool ends_block(uint32_t instruction) {
    const uint32_t op = instruction >> 26;
    switch (op) {
        case 0x01:                          /* REGIMM: BLTZ/BGEZ/BLTZAL/BGEZAL */
        case 0x02: case 0x03:               /* J, JAL */
        case 0x04: case 0x05:               /* BEQ, BNE */
        case 0x06: case 0x07:               /* BLEZ, BGTZ */
            return true;
        case 0x00: {
            const uint32_t fn = instruction & 0x3F;
            return fn == 0x08 || fn == 0x09   /* JR, JALR */
                || fn == 0x0C || fn == 0x0D;  /* SYSCALL, BREAK */
        }
        default:
            return false;
    }
}

/* What the fetch will return for this address, without doing the fetch.
 *
 * The i-cache first, exactly as cpu_icache_fetch would read it, then RAM — which
 * is what a miss is about to fill the line with. So a block is built from the
 * instructions that will actually execute, including stale ones the guest wrote
 * without flushing, and building it costs no cycles, fills no line and trips no
 * watchpoint. Doing this through interconnect_load32 instead would charge a data
 * access for every instruction in the block and fire read watchpoints on
 * addresses the guest has not reached yet. */
static uint32_t peek_code(const Cpu* cpu, uint32_t paddr) {
    const uint32_t tag = paddr >> 12;
    const uint32_t li  = (paddr >> 4) & (ICACHE_NUM_LINES - 1);
    const uint32_t wi  = (paddr >> 2) & (ICACHE_LINE_WORDS - 1);
    const ICacheLine* l = &cpu->icache[li];
    if (l->tag == tag && l->valid[wi]) return l->data[wi];
    return ram_load32(cpu->inter->ram, paddr & (RAM_SIZE - 1));
}

bool cpu_blocks_reload_line(RecBlock* b, uint32_t line_index, const uint32_t* words) {
    const uint32_t op0   = b->line_op0[line_index];
    const uint32_t word0 = b->line_word0[line_index];
    const uint32_t last  = (line_index + 1u < b->line_count)
                         ? b->line_op0[line_index + 1u] : b->count;

    for (uint32_t i = op0, w = word0; i < last; i++, w++) {
        const uint32_t old = b->ops[i].instruction;
        const uint32_t nw  = words[w];
        if (nw == old) continue;

        /* The bytes behind this block changed under it — self-modifying code, or
         * an overlay loaded over the same address. Updating the handler is not
         * enough: if the new instruction branches where the old one did not (or
         * the other way round) the block's *shape* is wrong, and its remaining
         * ops belong to code that is no longer there. Say so and let the caller
         * drop it; the interpreter takes that instruction and the block is built
         * again from what is there now. */
        if (ends_block(nw) != ends_block(old)) return false;

        b->ops[i].instruction = nw;
        b->ops[i].fn          = cpu_decode_handler(nw);
    }
    return true;
}

RecBlock* cpu_blocks_lookup(Cpu* cpu, uint32_t vaddr, uint32_t paddr) {
    /* Two exclusions, both for correctness rather than for speed.
     *
     * KSEG1 is uncached: a fetch there always reaches memory, so a cached block
     * has nothing keeping it honest.
     *
     * Everything outside RAM, which in practice means the BIOS ROM. A ROM fetch
     * charges ~24 cycles a word (cpu_icache.c), and the interpreter charges them
     * interleaved with execution — line 1's charge lands as instruction 4 is
     * fetched. A block would have to reproduce that interleaving exactly or move
     * an event by a few cycles, and the BIOS is the boot path, not the hot one.
     * RAM fills charge nothing, which is what makes the lazy replay in the runner
     * free of ordering questions. */
    if ((vaddr >> 29) == 0x5u) return NULL;
    if (paddr >= 0x00800000u)  return NULL;

    RecBlock* b = &s_cache[rec_slot(paddr)];
    if (b->count != 0 && b->paddr == paddr) return b;

    const bool displaced = (b->count != 0);
    b->paddr      = paddr;
    b->count      = 0;
    b->line_count = 0;

    uint32_t addr      = paddr;
    uint32_t last_line = UINT32_MAX;
    bool     ending    = false;   /* branch is in; one delay slot to go */

    for (uint32_t i = 0; i < REC_BLOCK_MAX_OPS; i++) {
        /* Never run a block across a 4 KB page: consecutive physical addresses
         * stop meaning consecutive code at a region edge. */
        if (i > 0 && (addr & 0xFFFu) == 0) break;

        const uint32_t line = addr & ~((uint32_t)ICACHE_LINE_WORDS * 4u - 1u);
        if (line != last_line) {
            if (b->line_count >= REC_BLOCK_MAX_LINES) break;
            b->line_paddr[b->line_count] = line;
            b->line_word0[b->line_count] = (uint8_t)((addr >> 2) & (ICACHE_LINE_WORDS - 1));
            b->line_op0[b->line_count]   = (uint8_t)i;
            b->line_count++;
            last_line = line;
        }

        const uint32_t instr = peek_code(cpu, addr);
        b->ops[i].instruction = instr;
        b->ops[i].fn          = cpu_decode_handler(instr);
        b->count              = (uint16_t)(i + 1);

        if (ending) break;                    /* that was the delay slot */
        if (ends_block(instr)) ending = true;
        addr += 4;
    }

    if (b->count == 0) return NULL;

    CpuExecStatus* st = cpu_exec_status_mut();
    st->blocks_built++;
    if (displaced) st->blocks_invalidated++;
    else           st->blocks_live++;
    return b;
}
