/* SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 ZioZoni95
 *
 * Part of ZoniStation One, a PlayStation 1 emulator.
 * See LICENSE for the full licence text and THIRD-PARTY.md for the
 * components of this project that have other authors.
 */
#include "cpu_rec.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#include "cpu.h"
#include "interconnect.h"
#include "event_scheduler.h"
#include "debugger.h"
#include "golden_trace.h"
#include "cpu_exec.h"
#include "log.h"

/* ---------------------------------------------------------------------------
 * Register use in emitted code
 *
 *   rbx   Cpu*            callee-saved, so it survives every call out
 *   r12d  instructions retired so far in this block — the return value
 *   rax   scratch, and the target of an indirect call
 *
 * SysV is the ABI throughout: a handler takes (Cpu*, uint32_t) in rdi/esi, and
 * rax/rcx/rdx/rsi/rdi/r8-r11 are caller-saved, which is why nothing lives there
 * across a call.
 * ------------------------------------------------------------------------- */

#define REC_CODE_SIZE (16u * 1024u * 1024u)
#define REC_MAX_BLOCK_BYTES 8192u

static uint8_t* s_code;
static uint32_t s_code_used;

/* Compiled entry points, in the same direct-mapped shape as the block cache and
 * indexed the same way, so a block and its code are found by one hash each. */
#define REC_MAP_BITS 14
#define REC_MAP_SIZE (1u << REC_MAP_BITS)
/* The virtual address is part of the key, not just the physical one.
 *
 * The block cache is indexed physically, and rightly — the same code reached
 * through KUSEG and through KSEG0 is the same code. Emitted code is not: it
 * bakes current_pc in as an immediate and decides the A0h/B0h/C0h vector test at
 * compile time, both of which are virtual. The BIOS kernel reaches the same
 * routines both ways, so without this a block compiled for one mapping would run
 * with the other's current_pc. */
typedef struct {
    uint32_t paddr, vaddr;
    RecEntry fn;
    uint16_t count, version;
} RecMapSlot;
static RecMapSlot* s_map;

uint32_t cpu_rec_code_bytes(void) { return s_code_used; }

/* --- emitter ------------------------------------------------------------- */

typedef struct {
    uint8_t* p;         /* write cursor */
    uint8_t* end;
    bool     overflow;
} Emit;

static inline void e8(Emit* e, uint8_t v) {
    if (e->p >= e->end) { e->overflow = true; return; }
    *e->p++ = v;
}
static inline void e32(Emit* e, uint32_t v) {
    e8(e, (uint8_t)v); e8(e, (uint8_t)(v >> 8));
    e8(e, (uint8_t)(v >> 16)); e8(e, (uint8_t)(v >> 24));
}
static inline void e64(Emit* e, uint64_t v) { e32(e, (uint32_t)v); e32(e, (uint32_t)(v >> 32)); }

/* ModRM with rbx as base. disp is always emitted as disp32: Cpu is large enough
 * that most fields are past 127 anyway, and one form is one thing to get wrong. */
static void modrm_bx(Emit* e, uint8_t reg, uint32_t disp) {
    e8(e, (uint8_t)(0x80 | ((reg & 7) << 3) | 3));   /* mod=10, r/m=011 (rbx) */
    e32(e, disp);
}

/* mov rax, imm64 */
static void emit_mov_rax_imm64(Emit* e, uint64_t v) { e8(e, 0x48); e8(e, 0xB8); e64(e, v); }
/* mov rdi, rbx */
static void emit_mov_rdi_rbx(Emit* e) { e8(e, 0x48); e8(e, 0x89); e8(e, 0xDF); }
/* mov esi, imm32 */
static void emit_mov_esi_imm32(Emit* e, uint32_t v) { e8(e, 0xBE); e32(e, v); }
/* call rax */
static void emit_call_rax(Emit* e) { e8(e, 0xFF); e8(e, 0xD0); }
/* mov rsi, imm64 */
static void emit_mov_rsi_imm64(Emit* e, uint64_t v) { e8(e, 0x48); e8(e, 0xBE); e64(e, v); }
/* mov edx, imm32 */
static void emit_mov_edx_imm32(Emit* e, uint32_t v) { e8(e, 0xBA); e32(e, v); }

/* mov dword [rbx+disp], imm32 */
static void emit_mov_m32_imm(Emit* e, uint32_t disp, uint32_t v) {
    e8(e, 0xC7); modrm_bx(e, 0, disp); e32(e, v);
}
/* mov byte [rbx+disp], imm8 */
static void emit_mov_m8_imm(Emit* e, uint32_t disp, uint8_t v) {
    e8(e, 0xC6); modrm_bx(e, 0, disp); e8(e, v);
}
/* mov eax, dword [rbx+disp] */
static void emit_mov_eax_m32(Emit* e, uint32_t disp) { e8(e, 0x8B); modrm_bx(e, 0, disp); }
/* mov dword [rbx+disp], eax */
static void emit_mov_m32_eax(Emit* e, uint32_t disp) { e8(e, 0x89); modrm_bx(e, 0, disp); }
/* movzx eax, byte [rbx+disp] */
static void emit_movzx_eax_m8(Emit* e, uint32_t disp) {
    e8(e, 0x0F); e8(e, 0xB6); modrm_bx(e, 0, disp);
}
/* mov byte [rbx+disp], al */
static void emit_mov_m8_al(Emit* e, uint32_t disp) { e8(e, 0x88); modrm_bx(e, 0, disp); }
/* cmp byte [rbx+disp], imm8 */
static void emit_cmp_m8_imm(Emit* e, uint32_t disp, uint8_t v) {
    e8(e, 0x80); modrm_bx(e, 7, disp); e8(e, v);
}
/* cmp dword [rbx+disp], imm32 */
static void emit_cmp_m32_imm(Emit* e, uint32_t disp, uint32_t v) {
    e8(e, 0x81); modrm_bx(e, 7, disp); e32(e, v);
}
/* add dword [rbx+disp], imm32 */
static void emit_add_m32_imm(Emit* e, uint32_t disp, uint32_t v) {
    e8(e, 0x81); modrm_bx(e, 0, disp); e32(e, v);
}
/* test al, al */
static void emit_test_al_al(Emit* e) { e8(e, 0x84); e8(e, 0xC0); }
/* inc r12d */
static void emit_inc_r12d(Emit* e) { e8(e, 0x41); e8(e, 0xFF); e8(e, 0xC4); }

/* Jcc rel32 with a patch site. cc is the low nibble of the 0F 8x form. */
typedef struct { uint8_t* site; } Fixup;
static Fixup emit_jcc(Emit* e, uint8_t cc) {
    e8(e, 0x0F); e8(e, (uint8_t)(0x80 | cc));
    Fixup f = { e->p };
    e32(e, 0);
    return f;
}
static void fixup_here(Emit* e, Fixup f) {
    if (!f.site || e->overflow) return;
    int32_t rel = (int32_t)(e->p - (f.site + 4));
    memcpy(f.site, &rel, 4);
}

#define CC_E   0x4
#define CC_NE  0x5
#define CC_LE  0xE

/* --- helpers the emitted code calls ---------------------------------------
 *
 * Everything that is a branch on machine state rather than a constant stays in
 * C. The emitter's job is to remove the *fixed* work — the constants, the vector
 * tests, the summing — not to re-express the interpreter in machine code.
 * ------------------------------------------------------------------------- */

/* The interrupt check, lifted from cpu_execution.c so the two cannot drift.
 * Returns non-zero when the block must stop. */
uint8_t cpu_rec_check_irq(Cpu* cpu, uint32_t instruction);
/* The A0h/B0h/C0h side-channel. Non-zero when the call was answered by HLE and
 * the block must stop. Only emitted for the three addresses that can be one. */
uint8_t cpu_rec_bios_vector(Cpu* cpu);
/* The load-delay rotation plus the event dispatch, which is a branch on the
 * downcount the emitter cannot fold. */
void cpu_rec_retire(Cpu* cpu);
void cpu_rec_events(Cpu* cpu);
/* The breakpoint walk, only reached when one is actually set. Non-zero when the
 * debugger paused and the block must stop. */
uint8_t cpu_rec_breakpoint(Cpu* cpu);
/* Replay one i-cache line at a line boundary inside a block. Non-zero when the
 * compiled code is no longer a description of what is there. */
uint8_t cpu_rec_revalidate_line(Cpu* cpu, RecBlock* b, uint32_t line_index);

/* --- block compilation ---------------------------------------------------- */

#define OFF(f) ((uint32_t)offsetof(Cpu, f))

static bool emit_instruction(Emit* e, const RecBlock* b, uint32_t i,
                             uint32_t pc, bool pc_is_known,
                             Fixup* stops, uint32_t* nstop) {
    const RecOp* op = &b->ops[i];
    const uint32_t instr = op->instruction;

    /* Straight-line only, and this is not paranoia — it is the same test
     * cpu_run_block() makes with `expect`, and leaving it out was a real defect.
     *
     * A block can *begin* on a delay slot: one that filled up on a branch ends
     * without it, and a line replay can stop a block exactly there too. The
     * build then decodes forward from that address, but once the delay slot has
     * run the PC is the branch's target, not the next address along — so every
     * instruction the block holds after it belongs to code that is not being
     * executed. The interpreter notices and leaves. Compiled code has current_pc
     * baked in as an immediate and cannot notice anything, so it has to be told.
     *
     * Not emitted for the first instruction: the caller only enters a block when
     * cpu->pc is its address. Checked against cpu->pc before this instruction
     * writes it, which is what makes the comparison mean anything. */
    if (i > 0) {
        emit_cmp_m32_imm(e, OFF(pc), pc);
        stops[(*nstop)++] = emit_jcc(e, CC_NE);
    }

    /* A line boundary other than the first: replay the line before the
     * instruction that lives in it, and stop if what came back is not what this
     * code was emitted from. The first line is replayed by the caller, before
     * the block is entered at all. */
    for (uint32_t li = 1; li < b->line_count; li++) {
        if (b->line_op0[li] != i) continue;
        emit_mov_rdi_rbx(e);
        emit_mov_rsi_imm64(e, (uint64_t)(uintptr_t)b);
        emit_mov_edx_imm32(e, li);
        emit_mov_rax_imm64(e, (uint64_t)(uintptr_t)&cpu_rec_revalidate_line);
        emit_call_rax(e);
        emit_test_al_al(e);
        stops[(*nstop)++] = emit_jcc(e, CC_NE);
        break;
    }

    /* exception_pending = false; current_pc = <const>; in_delay_slot = branch_taken */
    emit_mov_m8_imm(e, OFF(exception_pending), 0);
    emit_mov_m32_imm(e, OFF(current_pc), pc);
    emit_movzx_eax_m8(e, OFF(branch_taken));
    emit_mov_m8_al(e, OFF(in_delay_slot));

    /* if (cpu_rec_check_irq(cpu, instr)) stop; */
    emit_mov_rdi_rbx(e);
    emit_mov_esi_imm32(e, instr);
    emit_mov_rax_imm64(e, (uint64_t)(uintptr_t)&cpu_rec_check_irq);
    emit_call_rax(e);
    emit_test_al_al(e);
    stops[(*nstop)++] = emit_jcc(e, CC_NE);

    /* if (zs1_trace_active) zs1_trace_fold(cpu, instr); */
    {
        emit_mov_rax_imm64(e, (uint64_t)(uintptr_t)&zs1_trace_active);
        e8(e, 0x80); e8(e, 0x38); e8(e, 0x00);            /* cmp byte [rax], 0 */
        Fixup skip = emit_jcc(e, CC_E);
        emit_mov_rdi_rbx(e);
        emit_mov_esi_imm32(e, instr);
        emit_mov_rax_imm64(e, (uint64_t)(uintptr_t)&zs1_trace_fold);
        emit_call_rax(e);
        fixup_here(e, skip);
    }

    /* The execution-trace ring, inline: two stores and an index. It is a crash
     * facility and has to keep working, but it is not worth a call. */
    {
        emit_cmp_m8_imm(e, OFF(exec_trace_frozen), 0);
        Fixup skip = emit_jcc(e, CC_NE);
        emit_mov_eax_m32(e, OFF(exec_trace_head));
        /* mov dword [rbx + rax*4 + off_pc], pc */
        e8(e, 0xC7); e8(e, 0x84); e8(e, 0x83); e32(e, OFF(exec_trace_pc)); e32(e, pc);
        /* mov dword [rbx + rax*4 + off_instr], instr */
        e8(e, 0xC7); e8(e, 0x84); e8(e, 0x83); e32(e, OFF(exec_trace_instr)); e32(e, instr);
        /* head = (head + 1) & (EXEC_TRACE_SIZE - 1) */
        e8(e, 0x83); e8(e, 0xC0); e8(e, 0x01);                    /* add eax, 1 */
        e8(e, 0x25); e32(e, (uint32_t)(EXEC_TRACE_SIZE - 1));     /* and eax, mask */
        emit_mov_m32_eax(e, OFF(exec_trace_head));
        /* if (count < SIZE) count++ */
        emit_cmp_m32_imm(e, OFF(exec_trace_count), (uint32_t)EXEC_TRACE_SIZE);
        Fixup full = emit_jcc(e, CC_E);
        emit_add_m32_imm(e, OFF(exec_trace_count), 1);
        fixup_here(e, full);
        fixup_here(e, skip);
    }

    /* branch_taken = false; pc = next_pc; next_pc = pc + 4.
     *
     * Constants where they are genuinely constant, which is most of the time and
     * is a large part of what makes this worth emitting at all. Two places where
     * they are not, and folding them there was the bug that put the boot logo on
     * a black screen:
     *
     *   - **A delay slot.** The branch immediately before it has just written
     *     next_pc = target, so `pc = next_pc` lands on the target. Writing the
     *     constant pc+4 instead throws the destination of every jump in the guest
     *     away.
     *   - **The first instruction of a block**, which may itself be a delay slot:
     *     a block that filled up on a branch ends without it, and the next block
     *     starts there with next_pc already pointing at the target.
     *
     * Everywhere else the previous instruction's own store is what makes next_pc
     * known, so the constant is exact by construction. */
    emit_mov_m8_imm(e, OFF(branch_taken), 0);
    if (pc_is_known) {
        emit_mov_m32_imm(e, OFF(pc), pc + 4);
        emit_mov_m32_imm(e, OFF(next_pc), pc + 8);
    } else {
        emit_mov_eax_m32(e, OFF(next_pc));          /* eax = next_pc      */
        emit_mov_m32_eax(e, OFF(pc));               /* pc = eax           */
        e8(e, 0x83); e8(e, 0xC0); e8(e, 0x04);      /* add eax, 4         */
        emit_mov_m32_eax(e, OFF(next_pc));          /* next_pc = eax      */
    }

    /* The debugger's breakpoint check, gated on the count exactly as the inline
     * in debugger.h does. Emitted rather than called: the usual answer is no. */
    {
        Interconnect* dummy = NULL; (void)dummy;
        const uint32_t dbg = OFF(inter);
        /* mov rax, [rbx+inter]; cmp dword [rax+bp_count], 0; je skip; call slow */
        e8(e, 0x48); e8(e, 0x8B); modrm_bx(e, 0, dbg);            /* mov rax,[rbx+inter] */
        e8(e, 0x81); e8(e, 0xB8);
        e32(e, (uint32_t)(offsetof(Interconnect, debugger) + offsetof(Debugger, breakpoint_count)));
        e32(e, 0);                                                /* cmp dword [rax+..],0 */
        Fixup skip = emit_jcc(e, CC_E);
        /* step_skip_bp is handled by the slow helper, which is the interpreter's
         * own path; a run with a breakpoint set is not a run that needs speed. */
        emit_mov_rdi_rbx(e);
        emit_mov_rax_imm64(e, (uint64_t)(uintptr_t)&cpu_rec_breakpoint);
        emit_call_rax(e);
        emit_test_al_al(e);
        stops[(*nstop)++] = emit_jcc(e, CC_NE);
        fixup_here(e, skip);
    }

    /* The BIOS vector side-channel, decided here rather than at run time. Three
     * comparisons per instruction become none, for every block that does not
     * start on a vector — which is all of them but three. */
    if (pc == 0x000000A0u || pc == 0x000000B0u || pc == 0x000000C0u) {
        emit_mov_rdi_rbx(e);
        emit_mov_rax_imm64(e, (uint64_t)(uintptr_t)&cpu_rec_bios_vector);
        emit_call_rax(e);
        emit_test_al_al(e);
        stops[(*nstop)++] = emit_jcc(e, CC_NE);
    }

    /* The operation itself. */
    emit_mov_rdi_rbx(e);
    emit_mov_esi_imm32(e, instr);
    emit_mov_rax_imm64(e, (uint64_t)(uintptr_t)op->fn);
    emit_call_rax(e);

    /* if (exception_pending) stop; */
    emit_cmp_m8_imm(e, OFF(exception_pending), 0);
    stops[(*nstop)++] = emit_jcc(e, CC_NE);

    /* Load-delay rotation and regs[0] = 0. */
    emit_mov_rdi_rbx(e);
    emit_mov_rax_imm64(e, (uint64_t)(uintptr_t)&cpu_rec_retire);
    emit_call_rax(e);

    emit_inc_r12d(e);
    return !e->overflow;
}

/* --- the rest of the emitter --------------------------------------------- */

/* ModRM against an arbitrary base register (rax=0, rbx=3), disp32 form. */
static void modrm_base(Emit* e, uint8_t reg, uint8_t base, uint32_t disp) {
    e8(e, (uint8_t)(0x80 | ((reg & 7) << 3) | (base & 7)));
    e32(e, disp);
}

#define CC_G 0xF

/* The cycle accounting, inline and exact.
 *
 * Deferring the constant part to the end of the block was the obvious folding
 * and it is wrong here: handlers read inter->cpu_cycle_counter while they run —
 * muldiv_completion_tick and gte_completion_tick are compared against it — so a
 * counter held still for a block's length changes what MFHI/MFLO decide. The
 * total would come out the same and the machine would not.
 *
 * Emitted rather than called, so the common path has no call at all:
 *
 *   rax = cpu->inter
 *   ecx = inter->cpu_mem_stall_cycles ; inter->cpu_mem_stall_cycles = 0
 *   edx = ecx + 1
 *   inter->cpu_cycle_counter += edx
 *   inter->instructions_retired += 1
 *   cpu->downcount -= edx
 *   if (cpu->downcount <= 0) cpu_rec_events(cpu)
 */
static void emit_cycle_accounting(Emit* e) {
    const uint32_t inter_off = OFF(inter);
    const uint32_t stall_off = (uint32_t)offsetof(Interconnect, cpu_mem_stall_cycles);
    const uint32_t cyc_off   = (uint32_t)offsetof(Interconnect, cpu_cycle_counter);
    const uint32_t ret_off   = (uint32_t)offsetof(Interconnect, instructions_retired);

    e8(e, 0x48); e8(e, 0x8B); modrm_bx(e, 0, inter_off);      /* mov rax, [rbx+inter] */
    e8(e, 0x8B); modrm_base(e, 1, 0, stall_off);              /* mov ecx, [rax+stall] */
    e8(e, 0xC7); modrm_base(e, 0, 0, stall_off); e32(e, 0);   /* mov dword [rax+stall], 0 */
    e8(e, 0x8D); e8(e, 0x51); e8(e, 0x01);                    /* lea edx, [rcx+1] */
    e8(e, 0x01); modrm_base(e, 2, 0, cyc_off);                /* add [rax+cycles], edx */
    e8(e, 0x48); e8(e, 0x83); modrm_base(e, 0, 0, ret_off); e8(e, 0x01); /* add qword [rax+retired],1 */
    e8(e, 0x29); modrm_bx(e, 2, OFF(downcount));              /* sub [rbx+downcount], edx */

    emit_cmp_m32_imm(e, OFF(downcount), 0);
    Fixup skip = emit_jcc(e, CC_G);
    emit_mov_rdi_rbx(e);
    emit_mov_rax_imm64(e, (uint64_t)(uintptr_t)&cpu_rec_events);
    emit_call_rax(e);
    fixup_here(e, skip);
}

/* The frame boundary: VBlank ends the field where it says, not at the end of
 * whatever block contained it. Same rule the block runner follows. */
static Fixup emit_frame_check(Emit* e) {
    e8(e, 0x48); e8(e, 0x8B); modrm_bx(e, 0, OFF(inter));     /* mov rax, [rbx+inter] */
    e8(e, 0x80); modrm_base(e, 7, 0,
        (uint32_t)offsetof(Interconnect, frame_complete)); e8(e, 0);  /* cmp byte [rax+fc],0 */
    return emit_jcc(e, CC_NE);
}

static RecEntry compile_block(const RecBlock* b, uint32_t vaddr) {
    if (!s_code) return NULL;
    if (s_code_used + REC_MAX_BLOCK_BYTES > REC_CODE_SIZE) {
        /* The cache is a bump allocator with no eviction: a full one is flushed
         * whole. Blocks are cheap to rebuild and this happens rarely enough that
         * anything cleverer would be complexity without a reason. */
        LOG_CPU_INFO("[CPU] recompiler: code cache full at %u KB — flushing", s_code_used / 1024);
        cpu_rec_flush();
    }

    Emit e = { s_code + s_code_used, s_code + s_code_used + REC_MAX_BLOCK_BYTES, false };
    uint8_t* start = e.p;

    /* Six stops per instruction is the worst case: the interrupt check, a line
     * revalidation, the breakpoint walk, a BIOS vector, the exception test and
     * the frame boundary. Sized for all of them so a block is never truncated
     * for want of room to record where it can leave. */
    Fixup stops[REC_BLOCK_MAX_OPS * 6 + 8];
    uint32_t nstop = 0;

    /* Prologue. rbx and r12 are callee-saved, so they are pushed and restored;
     * the extra push keeps rsp 16-byte aligned at every call, which SysV
     * requires and which movaps inside a callee will fault on if it is wrong. */
    e8(&e, 0x53);                                   /* push rbx */
    e8(&e, 0x41); e8(&e, 0x54);                     /* push r12 */
    e8(&e, 0x48); e8(&e, 0x83); e8(&e, 0xEC); e8(&e, 0x08);  /* sub rsp, 8 */
    e8(&e, 0x48); e8(&e, 0x89); e8(&e, 0xFB);       /* mov rbx, rdi */
    e8(&e, 0x45); e8(&e, 0x31); e8(&e, 0xE4);       /* xor r12d, r12d */

    uint32_t pc = vaddr;
    /* False for the first instruction: the caller guarantees cpu->pc, never
     * cpu->next_pc, and a block can be entered on a delay slot. */
    bool pc_is_known = false;
    for (uint32_t i = 0; i < b->count; i++) {
        if (nstop + 8 > (uint32_t)(sizeof(stops) / sizeof(stops[0]))) break;
        if (!emit_instruction(&e, b, i, pc, pc_is_known, stops, &nstop)) break;
        /* The instruction after a branch is its delay slot, and the branch will
         * have written next_pc by the time it runs. */
        pc_is_known = !cpu_blocks_ends_block(b->ops[i].instruction);
        emit_cycle_accounting(&e);
        if (i + 1 < b->count) stops[nstop++] = emit_frame_check(&e);
        pc += 4;
    }

    /* Epilogue: every stop lands here, and r12d — the instructions actually
     * retired — is the return value. */
    for (uint32_t k = 0; k < nstop; k++) fixup_here(&e, stops[k]);
    e8(&e, 0x44); e8(&e, 0x89); e8(&e, 0xE0);       /* mov eax, r12d */
    e8(&e, 0x48); e8(&e, 0x83); e8(&e, 0xC4); e8(&e, 0x08);  /* add rsp, 8 */
    e8(&e, 0x41); e8(&e, 0x5C);                     /* pop r12 */
    e8(&e, 0x5B);                                   /* pop rbx */
    e8(&e, 0xC3);                                   /* ret */

    if (e.overflow) return NULL;

    s_code_used += (uint32_t)(e.p - start);
    s_code_used = (s_code_used + 15u) & ~15u;       /* keep entries aligned */
    return (RecEntry)(void*)start;
}

RecEntry cpu_rec_entry(const RecBlock* b, uint32_t vaddr) {
    if (!s_map || !s_code) return NULL;

    RecMapSlot* slot = &s_map[(b->paddr >> 2) & (REC_MAP_SIZE - 1)];
    /* The version is part of the key, not decoration: instruction words are
     * baked into the emitted code as immediates, so a block whose bytes changed
     * under it must not be run through the code emitted from the old ones. */
    if (slot->fn && slot->paddr == b->paddr && slot->vaddr == vaddr &&
        slot->count == b->count && slot->version == b->version) return slot->fn;

    RecEntry fn = compile_block(b, vaddr);
    if (!fn) return NULL;

    slot->paddr   = b->paddr;
    slot->vaddr   = vaddr;
    slot->count   = b->count;
    slot->version = b->version;
    slot->fn      = fn;
    cpu_exec_status_mut()->code_bytes = s_code_used;
    return fn;
}

void cpu_rec_flush(void) {
    if (s_map) memset(s_map, 0, REC_MAP_SIZE * sizeof(RecMapSlot));
    s_code_used = 0;
    cpu_exec_status_mut()->code_bytes = 0;
}

bool cpu_rec_init(void) {
    if (s_code) return true;

    /* MAP_32BIT is not asked for: every address the emitted code needs is loaded
     * as a full 64-bit immediate rather than reached rip-relative, so the cache
     * can live anywhere the kernel puts it. */
    void* mem = mmap(NULL, REC_CODE_SIZE, PROT_READ | PROT_WRITE | PROT_EXEC,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mem == MAP_FAILED) {
        LOG_CPU_ERROR("[CPU] recompiler: cannot map %u MB of executable memory",
                      REC_CODE_SIZE / (1024u * 1024u));
        return false;
    }
    s_map = (RecMapSlot*)calloc(REC_MAP_SIZE, sizeof(RecMapSlot));
    if (!s_map) {
        munmap(mem, REC_CODE_SIZE);
        LOG_CPU_ERROR("[CPU] recompiler: cannot allocate the entry map");
        return false;
    }
    s_code = (uint8_t*)mem;
    s_code_used = 0;
    LOG_CPU_INFO("[CPU] recompiler: %u MB code cache, %u entry slots",
                 REC_CODE_SIZE / (1024u * 1024u), (unsigned)REC_MAP_SIZE);
    return true;
}

void cpu_rec_shutdown(void) {
    if (s_code) munmap(s_code, REC_CODE_SIZE);
    free(s_map);
    s_code = NULL;
    s_map  = NULL;
    s_code_used = 0;
}
