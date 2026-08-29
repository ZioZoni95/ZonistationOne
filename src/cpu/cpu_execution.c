/* SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 ZioZoni95
 *
 * Part of ZoniStation One, a PlayStation 1 emulator.
 * See LICENSE for the full licence text and THIRD-PARTY.md for the
 * components of this project that have other authors.
 */
#include "cpu.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "interconnect.h"
#include "event_scheduler.h"
#include "gte.h"
#include "debugger.h"
#include "golden_trace.h"
#include "cpu_blocks.h"
#include "cpu_exec.h"
#include "log.h"

// ============================================================================
// CPU Execution Loop
// ============================================================================

// Check for pending hardware interrupt - called once per instruction
static inline bool CheckPendingInterrupt(Cpu* cpu, const uint32_t* known_instr) {
    // Per PSX-SPX: interrupt pending if (I_STAT & I_MASK) != 0 AND SR.IE == 1
    uint16_t i_stat = cpu->inter->irq_status;
    uint16_t i_mask = cpu->inter->irq_mask;
    bool irq_pending = (i_stat & i_mask) != 0;
    
    // Update COP0 Cause bit 10 (IP2) based on current state - NOT a latch!
    if (irq_pending) {
        cpu->cause |= (1u << 10);
    } else {
        cpu->cause &= ~(1u << 10);
    }
    
    // Check: SR.IEc && ((cause & sr) & 0xFF00) != 0
    bool sr_iec = (cpu->sr & 1) != 0;
    uint32_t sr_cause_masked = (cpu->sr & cpu->cause) & 0xFF00;
    bool has_interrupt = sr_iec && (sr_cause_masked != 0);
    
    if (has_interrupt) {
        /* PSX-SPX: do not take IRQ if the next instruction is a GTE opcode (COP2 data op).
           Defer until the GTE instruction completes to avoid EPC pointing into a GTE op. */
        /* The block runner already holds this word, so it hands it over rather
         * than paying a second lookup. Identical either way: the line is valid
         * by the time the runner gets here, so the fetch would hit. */
        uint32_t next_instr = known_instr ? *known_instr
                                          : cpu_icache_fetch(cpu, cpu->current_pc, false);
        if ((next_instr & 0xFE000000) == 0x4A000000) return false;
        cpu_exception(cpu, EXCEPTION_INTERRUPT);
        return true;
    }
    return false;
}

/* Advance the load-delay pipeline by one instruction.
 *
 * Called once the instruction has run, which is the whole point: "the target
 * register isn't updated until the next opcode has completed"
 * (psx-spx-docs/docs/cpuspecifications.md:172-174). The load issued by the
 * previous instruction lands here, and the one this instruction issued (if any)
 * takes its place, to land after the next one.
 *
 * Writing cpu->regs directly rather than through cpu_set_reg: that function
 * cancels a delayed load aimed at the register it writes, which is right for an
 * instruction's own write and wrong for the load's own retirement. A load
 * targeting R0 is a discard, and delay_load_reg is REG_ZERO when nothing is in
 * flight, so the same test covers both. */
static inline void cpu_retire_load_delay(Cpu* cpu) {
    if (cpu->delay_load_reg != REG_ZERO) {
        cpu->regs[cpu->delay_load_reg] = cpu->delay_load_value;
    }
    cpu->delay_load_reg   = cpu->load_reg_idx;
    cpu->delay_load_value = cpu->load_value;
    cpu->load_reg_idx     = REG_ZERO;
}

/* Land an in-flight load on the way into an exception handler.
 *
 * "unless an IRQ occurs between the load and next opcode, in that case the load
 * would complete during IRQ handling, and so, the next opcode would receive the
 * NEW value" (:175-177). The handler is that next opcode, so the delay is spent
 * by the time it runs. Both slots are drained: the instruction that was about to
 * execute never does, so nothing is left to retire it later. */
void cpu_flush_load_delay(Cpu* cpu) {
    if (cpu->delay_load_reg != REG_ZERO) {
        cpu->regs[cpu->delay_load_reg] = cpu->delay_load_value;
        cpu->delay_load_reg = REG_ZERO;
    }
    if (cpu->load_reg_idx != REG_ZERO) {
        cpu->regs[cpu->load_reg_idx] = cpu->load_value;
        cpu->load_reg_idx = REG_ZERO;
    }
}

/* One instruction, from the branch-state update to the event dispatch.
 *
 * Shared by the interpreter and the block runner, and shared rather than copied
 * on purpose: these two paths have to stay bit-identical, and the way that fails
 * is somebody fixing one of two near-identical copies. The only thing the block
 * runner supplies that the interpreter does not is `fn`, the handler already
 * resolved at build time — passing NULL takes the two-level table walk instead.
 *
 * Returns false when straight-line execution has to stop: an exception, a
 * debugger pause, or a BIOS call answered by HLE, which jumps to $ra. */
static inline bool cpu_step_body(Cpu* cpu, uint32_t instruction, cpu_handler_t fn) {
    /* The golden trace folds (current_pc, instruction) here, before anything
     * can change either — and before the breakpoint check below, which can
     * return early and would otherwise drop an instruction from the fold on a
     * paused run only. */
    zs1_trace_step(cpu, instruction);

    // Record into execution trace ring buffer (frozen after first crash dump)
    if (!cpu->exec_trace_frozen) {
        cpu->exec_trace_pc[cpu->exec_trace_head]    = cpu->current_pc;
        cpu->exec_trace_instr[cpu->exec_trace_head] = instruction;
        cpu->exec_trace_head = (cpu->exec_trace_head + 1) & (EXEC_TRACE_SIZE - 1);
        if (cpu->exec_trace_count < EXEC_TRACE_SIZE) cpu->exec_trace_count++;
    }

    // --- Update Branch State ---
    cpu->branch_taken = false;
    cpu->pc = cpu->next_pc;
    cpu->next_pc = cpu->pc + 4;

    // --- Breakpoint check (before executing the instruction, so cpu->regs
    // reflects fully-settled state — a debugger callback that runs any earlier
    // shows register values stale by one instruction for whatever the
    // immediately preceding instruction just wrote). ---
    if (!cpu->inter->debugger.step_skip_bp) {
        debugger_check_breakpoint(&cpu->inter->debugger, cpu);
        if (cpu->inter->debugger.paused) return false;
    } else {
        cpu->inter->debugger.step_skip_bp = false;
    }

    // --- BIOS syscall side-channel capture (A0h/B0h/C0h) ---
    // Must fire here, once control actually reaches the vector address, NOT
    // inside op_jr's own handler: the real calling convention sets $t1 (the
    // function-select register) in the JR's delay-slot instruction (e.g.
    // "jr $10 ; addiu $9,$0,0xA1", confirmed via disassembly trace), so $t1
    // is only valid after that delay-slot instruction has committed.
    if (cpu->current_pc == 0x000000A0 || cpu->current_pc == 0x000000B0 || cpu->current_pc == 0x000000C0) {
        bool hle = false;
        if (cpu->current_pc == 0x000000A0)
            hle = handle_a0_syscall(cpu);
        else if (cpu->current_pc == 0x000000B0)
            handle_b0_syscall(cpu);
        else
            handle_c0_syscall(cpu);
        /* LLE: real BIOS code executes normally unless HLE'd. */
        if (hle) {
            /* This stands in for a whole BIOS call, so it counts as a completed
             * opcode for the load pipeline too — skipping the rotation would
             * hold an in-flight load one instruction longer than hardware. */
            cpu_retire_load_delay(cpu);
            cpu->pc = cpu->regs[31];  // $ra: return to caller, as if the call fully executed
            cpu->next_pc = cpu->pc + 4;
            cpu->branch_taken = true;
            return false;
        }
    }

    // --- Decode and Execute ---
    if (fn) fn(cpu, instruction);
    else    decode_and_execute(cpu, instruction);
    if (cpu->exception_pending) {
        /* cpu_exception() already drained the pipeline: the load completes
         * during handling (cpuspecifications.md:175-177), and this instruction
         * never completed, so there is nothing to rotate. */
        return false;
    }

    // --- Finalize ---
    /* The instruction has completed, so the previous one's load lands now and
     * this one's takes its place. Anything this instruction wrote to the same
     * register already cancelled the pending load inside cpu_set_reg. */
    cpu_retire_load_delay(cpu);
    cpu->regs[REG_ZERO] = 0;   /* cheap belt-and-braces; cpu_set_reg drops R0 writes */

    // --- Advance Cycle Counters ---
    // Base cost is one cycle, plus whatever this instruction's data access(es)
    // owed. The bus accumulated that during execute; charging it here rather
    // than inside the access keeps a single instruction's cost atomic, so an
    // event scheduled mid-instruction cannot fire between an instruction's
    // memory stall and its retirement.
    uint32_t stall = cpu->inter->cpu_mem_stall_cycles;
    cpu->inter->cpu_mem_stall_cycles = 0;
    cpu->inter->cpu_cycle_counter += 1u + stall;
    cpu->inter->instructions_retired++;
    cpu->downcount -= (int32_t)(1u + stall);

    // --- Dispatch Events (event-scheduler downcount) ---
    if (cpu->downcount <= 0) {
        eventq_dispatch_due(cpu->inter);
        uint32_t next = cpu->inter->evq_next_cycle;
        uint32_t now  = cpu->inter->cpu_cycle_counter;
        cpu->downcount = (next != UINT32_MAX && (int32_t)(next - now) > 0)
                       ? (int32_t)(next - now) : 1;
    }
    return true;
}

// Main execution cycle - called for each instruction
void cpu_run_next_instruction(Cpu* cpu) {
    // exception_pending is per-instruction state; clear it before running this step.
    cpu->exception_pending = false;

    // Establish current-instruction context before any potential interrupt exception.
    // This matches R3000A behavior where IRQ is taken between instructions, and BD/EPC
    // are derived from the instruction about to execute.
    cpu->current_pc = cpu->pc;
    cpu->in_delay_slot = cpu->branch_taken;

    if (CheckPendingInterrupt(cpu, NULL)) return;   // exception raised, PC already updated

    if (cpu->current_pc % 4 != 0) {
        cpu_exception(cpu, EXCEPTION_LOAD_ADDRESS_ERROR);
        return;
    }

    uint32_t instruction = cpu_icache_fetch(cpu, cpu->current_pc, true);
    (void)cpu_step_body(cpu, instruction, NULL);
}

/* Run one cached block, or one instruction when there is no block to run.
 *
 * The i-cache lines behind the block are replayed here, each immediately before
 * the first instruction that lives in it — not all at entry. An instruction can
 * write the memory a later line of its own block reads, and filling early would
 * show the block the old bytes where the interpreter sees the new ones. A line
 * that had to be filled hands its words back to the cache, which re-decodes the
 * ops behind it; if that would change the block's shape the block is dropped and
 * the interpreter takes the instruction. See cpu_blocks.h for why this is the
 * whole of the invalidation story. */
static bool s_blocks_verify;

static void cpu_run_block(Cpu* cpu) {
    const uint32_t vaddr = cpu->pc;
    if (vaddr & 3u) { cpu_run_next_instruction(cpu); return; }

    RecBlock* b = cpu_blocks_lookup(cpu, vaddr, mask_region(vaddr));
    if (!b) { cpu_run_next_instruction(cpu); return; }

    uint32_t words[ICACHE_LINE_WORDS];
    uint32_t expect = vaddr;
    uint32_t li  = 0;
    uint32_t ran = 0;

    for (uint32_t i = 0; i < b->count; i++) {
        if (li < b->line_count && b->line_op0[li] == i) {
            /* Revalidate on every entry, not only when the touch had to fill.
             *
             * "The line filled, so re-read it" is not enough, and the case it
             * misses is the one that actually happens: a block is built while
             * its line is invalid, so it reads memory; the memory then changes;
             * some *other* fetch fills the line with the new bytes; and when
             * this block comes back the line is valid, the touch reports no
             * fill, and the block keeps serving what memory used to hold. Caught
             * at pc=0x00000CF0 by ZS1_BLOCKS_VERIFY, where the block still had a
             * LUI that had since become a NOP.
             *
             * So the words come back either way and the block is checked against
             * them. It is cheap in the case that matters: reload_line compares
             * first and only re-decodes what actually changed, so a block whose
             * bytes are stable pays four word comparisons per cache line — about
             * one per instruction — and never touches the decode tables. */
            cpu_icache_touch_line(cpu, b->line_paddr[li], b->line_word0[li], words, true);
            if (!cpu_blocks_reload_line(b, li, words)) {
                b->count = 0;                     /* the code under it changed shape */
                cpu_exec_status_mut()->blocks_invalidated++;
                break;
            }
            li++;
        }

        cpu->exception_pending = false;
        cpu->current_pc    = cpu->pc;
        cpu->in_delay_slot = cpu->branch_taken;

        /* Straight-line only. Anything that moved the PC elsewhere ends the
         * block here and the next lookup starts from wherever it went. */
        if (cpu->current_pc != expect) break;

        /* ZS1_BLOCKS_VERIFY=1: does this block still hold what a fetch would
         * return? That is the one assumption the whole design rests on — a
         * block is valid exactly as long as its i-cache lines are — and it is
         * cheaper to test it directly than to bisect a golden trace. The fetch
         * hits, because the line was touched above, so this changes nothing
         * beyond the time it takes. */
        if (s_blocks_verify) {
            const uint32_t want = cpu_icache_fetch(cpu, cpu->current_pc, false);
            if (want != b->ops[i].instruction || cpu_decode_handler(want) != b->ops[i].fn) {
                LOG_CPU_ERROR("[CPU] block mismatch at pc=0x%08X op %u/%u: "
                              "block has 0x%08X, fetch says 0x%08X (block paddr 0x%08X)",
                              cpu->current_pc, i, b->count,
                              b->ops[i].instruction, want, b->paddr);
                s_blocks_verify = false;   /* one report, not a flood */
            }
        }

        if (CheckPendingInterrupt(cpu, &b->ops[i].instruction)) break;
        if (!cpu_step_body(cpu, b->ops[i].instruction, b->ops[i].fn)) { ran++; break; }

        ran++;
        expect += 4;

        /* The frame ends where VBlank says it ends, not at the end of whatever
         * block happened to contain it. system_run_frame() tests this between
         * slices, so without the test here a block would carry up to 31
         * instructions past the boundary — the machine's state stays consistent
         * either way, but the host work main() does between frames (submitting
         * the field, polling input, pacing the audio ring) would land at a
         * different instruction than the interpreter puts it at, and the two
         * engines have to be indistinguishable. */
        if (cpu->inter->frame_complete) break;
    }

    cpu_exec_status_mut()->instr_from_cache += ran;
}

/* What the frame loop runs: a block under the block engine, one instruction
 * under the interpreter. The debugger's single-step stays on
 * cpu_run_next_instruction, which is the point of it. */
void cpu_run_slice(Cpu* cpu) {
    static int verify_cached = -1;
    if (verify_cached < 0) {
        const char* v = getenv("ZS1_BLOCKS_VERIFY");
        verify_cached = (v && *v && *v != '0');
        s_blocks_verify = verify_cached != 0;
        if (s_blocks_verify)
            LOG_CPU_INFO("[CPU] ZS1_BLOCKS_VERIFY — every cached instruction is "
                         "compared against a fetch; slow, and for diagnosis only");
    }
    if (cpu_exec_status()->active == CPU_EXEC_INTERPRETER) {
        cpu_run_next_instruction(cpu);
        return;
    }
    cpu_run_block(cpu);
}

// Safe memory peek for trace output — no bus side effects, no exceptions.
static uint32_t trace_peek32(const Interconnect* inter, uint32_t vaddr) {
    uint32_t phys = vaddr & 0x1FFFFFFF; // strip KUSEG/KSEG0/KSEG1 prefix
    if (phys < RAM_SIZE - 3)
        return (uint32_t)inter->ram->data[phys]       |
               ((uint32_t)inter->ram->data[phys+1]<<8) |
               ((uint32_t)inter->ram->data[phys+2]<<16)|
               ((uint32_t)inter->ram->data[phys+3]<<24);
    if (phys >= 0x1FC00000 && phys <= 0x1FC7FFFC) {
        uint32_t off = phys - 0x1FC00000;
        return (uint32_t)inter->bios->data[off]       |
               ((uint32_t)inter->bios->data[off+1]<<8) |
               ((uint32_t)inter->bios->data[off+2]<<16)|
               ((uint32_t)inter->bios->data[off+3]<<24);
    }
    if (phys >= 0x1F800000 && phys <= 0x1F8003FC) {
        uint32_t off = phys - 0x1F800000;
        return (uint32_t)inter->scratchpad[off]       |
               ((uint32_t)inter->scratchpad[off+1]<<8) |
               ((uint32_t)inter->scratchpad[off+2]<<16)|
               ((uint32_t)inter->scratchpad[off+3]<<24);
    }
    return 0xDEADBEEF;
}

static bool trace_is_code_addr(uint32_t v) {
    if ((v & 3) != 0) return false; // must be aligned
    uint32_t phys = v & 0x1FFFFFFF;
    // RAM: 0x80–0x1FFFFF, BIOS ROM: 0x1FC00000–0x1FC7FFFF
    return (phys >= 0x80 && phys < 0x200000) ||
           (phys >= 0x1FC00000 && phys < 0x1FC80000);
}

void cpu_dump_exec_trace(const Cpu* cpu, const char* path) {
    FILE* f = fopen(path, "w");
    if (!f) return;
    const Interconnect* inter = cpu->inter;

    // --- 1. Register snapshot ---
    fprintf(f, "=== CPU Register Snapshot ===\n");
    fprintf(f, "PC=%08X  nPC=%08X  curPC=%08X\n",
            cpu->pc, cpu->next_pc, cpu->current_pc);
    static const char* const rn[] = {
        "$0","at","v0","v1","a0","a1","a2","a3",
        "t0","t1","t2","t3","t4","t5","t6","t7",
        "s0","s1","s2","s3","s4","s5","s6","s7",
        "t8","t9","k0","k1","gp","sp","fp","ra"
    };
    for (int i = 0; i < 32; i++) {
        fprintf(f, " %s=%08X", rn[i], cpu->regs[i]);
        if ((i & 3) == 3) fputc('\n', f);
    }
    fprintf(f, " HI=%08X  LO=%08X\n", cpu->hi, cpu->lo);
    fprintf(f, " SR=%08X  Cause=%08X  EPC=%08X\n\n", cpu->sr, cpu->cause, cpu->epc);

    // --- 2. $ra context (call site) ---
    uint32_t ra = cpu->regs[31];
    fprintf(f, "=== $ra = 0x%08X (return site) ===\n", ra);
    if (inter && trace_is_code_addr(ra)) {
        for (int d = -3; d <= 2; d++) {
            uint32_t a = (uint32_t)((int32_t)ra + d * 4);
            uint32_t w = trace_peek32(inter, a);
            const char* dis = disassemble_mips(w, a);
            fprintf(f, " %s%08X  %08X  %s\n",
                    (d == -1) ? "CALL> " : "      ", a, w, dis ? dis : "?");
        }
    } else {
        fprintf(f, " (invalid or unavailable)\n");
    }
    fputc('\n', f);

    // --- 3. Stack walk — find saved return addresses ---
    uint32_t sp = cpu->regs[29];
    fprintf(f, "=== Stack Walk ($sp=0x%08X, scanning 64 words upward) ===\n", sp);
    int found = 0;
    if (inter) {
        for (int i = 0; i < 64; i++) {
            uint32_t addr = sp + (uint32_t)(i * 4);
            if ((addr & 3) != 0) continue;
            uint32_t val = trace_peek32(inter, addr);
            if (val == 0xDEADBEEF || !trace_is_code_addr(val)) continue;
            // Bonus: check if instruction at val-8 looks like JAL/JALR
            uint32_t caller_instr = trace_peek32(inter, val - 8);
            uint32_t op = caller_instr >> 26;
            uint32_t fn = caller_instr & 0x3F;
            bool is_call = (op == 3) || (op == 0 && fn == 9); // JAL or JALR
            const char* dis = disassemble_mips(caller_instr, val - 8);
            fprintf(f, " SP+%03X [%08X] => %08X%s  %08X  %s\n",
                    i * 4, addr, val, is_call ? " (JAL)" : "      ",
                    caller_instr, dis ? dis : "?");
            found++;
        }
    }
    if (found == 0) fprintf(f, " (no code pointers found)\n");
    fputc('\n', f);

    // --- 4. Execution trace ---
    uint32_t count = cpu->exec_trace_count;
    uint32_t head  = cpu->exec_trace_head;
    uint32_t start = (head - count) & (EXEC_TRACE_SIZE - 1);
    fprintf(f, "=== Execution Trace (last %u instructions) ===\n", count);
    for (uint32_t i = 0; i < count; i++) {
        uint32_t idx = (start + i) & (EXEC_TRACE_SIZE - 1);
        uint32_t pc  = cpu->exec_trace_pc[idx];
        uint32_t ins = cpu->exec_trace_instr[idx];
        const char* dis = disassemble_mips(ins, pc);
        fprintf(f, "%s[%4u] %08X  %08X  %s\n",
                (i == count - 1) ? ">>> " : "    ",
                i, pc, ins, dis ? dis : "?");
    }
    fclose(f);
}
