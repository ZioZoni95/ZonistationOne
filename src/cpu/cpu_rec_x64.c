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

/* mov ecx, dword [rbx+disp] */
static void emit_mov_ecx_m32(Emit* e, uint32_t disp) { e8(e, 0x8B); modrm_bx(e, 1, disp); }
/* <alu> eax, dword [rbx+disp] — opcode is the /r form's first byte */
static void emit_alu_eax_m32(Emit* e, uint8_t opcode, uint32_t disp) {
    e8(e, opcode); modrm_bx(e, 0, disp);
}
/* <alu> eax, imm32 — ext is the ModRM /digit */
static void emit_alu_eax_imm32(Emit* e, uint8_t ext, uint32_t v) {
    e8(e, 0x81); e8(e, (uint8_t)(0xC0 | (ext << 3))); e32(e, v);
}
/* mov eax, imm32 */
static void emit_mov_eax_imm32(Emit* e, uint32_t v) { e8(e, 0xB8); e32(e, v); }
/* not eax */
static void emit_not_eax(Emit* e) { e8(e, 0xF7); e8(e, 0xD0); }
/* test eax, eax — SF and ZF against zero, which is what a signed compare
 * with 0 needs; OF is cleared, so jle/jg read exactly as after a cmp. */
static void emit_test_eax_eax(Emit* e) { e8(e, 0x85); e8(e, 0xC0); }
/* mov dword [rbx + rax*4 + disp], ecx — the register file indexed by a value
 * only known at run time, which is the one place a GPR is not a fixed slot. */
static void emit_mov_regs_rax_ecx(Emit* e, uint32_t disp) {
    e8(e, 0x89); e8(e, 0x8C); e8(e, 0x83); e32(e, disp);
}

/* ModRM against an arbitrary base register (rax=0, rbx=3), disp32 form. */
static void modrm_base(Emit* e, uint8_t reg, uint8_t base, uint32_t disp) {
    e8(e, (uint8_t)(0x80 | ((reg & 7) << 3) | (base & 7)));
    e32(e, disp);
}
/* movzx <reg32>, word [rax+disp] */
static void emit_movzx_r32_m16_rax(Emit* e, uint8_t reg, uint32_t disp) {
    e8(e, 0x0F); e8(e, 0xB7); modrm_base(e, reg, 0, disp);
}
/* test ecx, edx */
static void emit_test_ecx_edx(Emit* e) { e8(e, 0x85); e8(e, 0xD1); }
/* setne cl ; movzx ecx, cl */
static void emit_setne_ecx(Emit* e) {
    e8(e, 0x0F); e8(e, 0x95); e8(e, 0xC1);
    e8(e, 0x0F); e8(e, 0xB6); e8(e, 0xC9);
}
/* shl ecx, imm8 */
static void emit_shl_ecx_imm8(Emit* e, uint8_t n) { e8(e, 0xC1); e8(e, 0xE1); e8(e, n); }
/* or eax, ecx */
static void emit_or_eax_ecx(Emit* e) { e8(e, 0x09); e8(e, 0xC8); }
/* test byte [rbx+disp], imm8 */
static void emit_test_m8_imm(Emit* e, uint32_t disp, uint8_t v) {
    e8(e, 0xF6); modrm_bx(e, 0, disp); e8(e, v);
}
/* shl/shr/sar eax, imm8 — ext 4, 5, 7 */
static void emit_shift_eax_imm8(Emit* e, uint8_t ext, uint8_t n) {
    e8(e, 0xC1); e8(e, (uint8_t)(0xC0 | (ext << 3))); e8(e, n);
}
/* shl/shr/sar eax, cl. x86 masks the count to 5 bits for a 32-bit operand and
 * so does the R3000A, so the MIPS `& 0x1F` needs no code of its own. */
static void emit_shift_eax_cl(Emit* e, uint8_t ext) {
    e8(e, 0xD3); e8(e, (uint8_t)(0xC0 | (ext << 3)));
}
/* setcc al ; movzx eax, al — cc is the low nibble of the 0F 9x form */
static void emit_setcc_eax(Emit* e, uint8_t cc) {
    e8(e, 0x0F); e8(e, (uint8_t)(0x90 | cc)); e8(e, 0xC0);
    e8(e, 0x0F); e8(e, 0xB6); e8(e, 0xC0);
}

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
#define CC_B   0x2
#define CC_L   0xC
#define CC_LE  0xE
#define CC_G   0xF

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
#define REG(i) (OFF(regs) + (uint32_t)(i) * 4u)

/* --- the operations emitted in place of a call ----------------------------
 *
 * Every one of these reads one or two registers, computes, and writes one
 * register. That shape is what makes them safe to emit: no memory access, no
 * effect on pc or next_pc, and no path that can raise an exception. The
 * dispatch is on the handler pointer the block cache already resolved, not on a
 * second decode of the instruction word — a decode here could disagree with
 * cpu_decode.c and emit the wrong operation, and `fn` is what the interpreter
 * would actually have run.
 *
 * The destination register is a compile-time constant, so two tests that cost
 * the interpreter something on every instruction cost nothing here: a write to
 * $zero emits no code at all (which is what makes NOP free — it is SLL R0,R0,0),
 * and the address of the destination is a fixed displacement.
 * ------------------------------------------------------------------------- */

/* cpu_set_reg(cpu, idx, eax) with idx known while compiling.
 *
 * The cancel is not optional. A load still in flight for this register loses to
 * this write — "isn't updated until the next opcode has completed"
 * (psx-spx-docs/docs/cpuspecifications.md:172-174) — and leaving it out lets the
 * load land afterwards and quietly undo the result. */
static void emit_set_reg_eax(Emit* e, uint32_t idx) {
    if (idx == 0) return;                     /* writes to $zero are dropped */
    emit_mov_m32_eax(e, REG(idx));
    emit_cmp_m32_imm(e, OFF(delay_load_reg), idx);
    Fixup skip = emit_jcc(e, CC_NE);
    emit_mov_m32_imm(e, OFF(delay_load_reg), 0);
    fixup_here(e, skip);
}

/* True when the operation was emitted and no call is needed. */
static bool emit_native_op(Emit* e, uint32_t instr, cpu_handler_t fn,
                           uint32_t pc, bool pc_is_known) {
    const uint32_t rs = instr_s(instr), rt = instr_t(instr), rd = instr_d(instr);

    /* Three shapes. `dst` is the register written, and a write to $zero means
     * the whole operation is unobservable — the interpreter still computes it,
     * but nothing can read the result and nothing else happens. */

    /* rd = rs <op> rt */
    uint8_t alu = 0; bool have_alu = false, nor_it = false;
    if      (fn == op_addu) { alu = 0x03; have_alu = true; }   /* add */
    else if (fn == op_subu) { alu = 0x2B; have_alu = true; }   /* sub */
    else if (fn == op_and)  { alu = 0x23; have_alu = true; }   /* and */
    else if (fn == op_or)   { alu = 0x0B; have_alu = true; }   /* or  */
    else if (fn == op_xor)  { alu = 0x33; have_alu = true; }   /* xor */
    else if (fn == op_nor)  { alu = 0x0B; have_alu = true; nor_it = true; }
    if (have_alu) {
        if (rd == 0) return true;
        emit_mov_eax_m32(e, REG(rs));
        emit_alu_eax_m32(e, alu, REG(rt));
        if (nor_it) emit_not_eax(e);
        emit_set_reg_eax(e, rd);
        return true;
    }

    /* rd = (rs < rt), signed or unsigned */
    if (fn == op_slt || fn == op_sltu) {
        if (rd == 0) return true;
        emit_mov_eax_m32(e, REG(rs));
        emit_alu_eax_m32(e, 0x3B, REG(rt));                    /* cmp */
        emit_setcc_eax(e, (fn == op_slt) ? CC_L : CC_B);
        emit_set_reg_eax(e, rd);
        return true;
    }

    /* rd = rt <shift> shamt */
    uint8_t sh = 0xFF;
    if      (fn == op_sll) sh = 4;
    else if (fn == op_srl) sh = 5;
    else if (fn == op_sra) sh = 7;
    if (sh != 0xFF) {
        if (rd == 0) return true;                              /* NOP lands here */
        emit_mov_eax_m32(e, REG(rt));
        uint32_t shamt = instr_shift(instr);
        if (shamt) emit_shift_eax_imm8(e, sh, (uint8_t)shamt);
        emit_set_reg_eax(e, rd);
        return true;
    }

    /* rd = rt <shift> (rs & 31) */
    if      (fn == op_sllv) sh = 4;
    else if (fn == op_srlv) sh = 5;
    else if (fn == op_srav) sh = 7;
    if (sh != 0xFF) {
        if (rd == 0) return true;
        emit_mov_ecx_m32(e, REG(rs));
        emit_mov_eax_m32(e, REG(rt));
        emit_shift_eax_cl(e, sh);
        emit_set_reg_eax(e, rd);
        return true;
    }

    /* rt = rs <op> imm */
    uint8_t ext = 0xFF; uint32_t imm = 0;
    if      (fn == op_addiu) { ext = 0; imm = instr_imm_se(instr); }
    else if (fn == op_andi)  { ext = 4; imm = instr_imm(instr);    }
    else if (fn == op_ori)   { ext = 1; imm = instr_imm(instr);    }
    else if (fn == op_xori)  { ext = 6; imm = instr_imm(instr);    }
    if (ext != 0xFF) {
        if (rt == 0) return true;
        emit_mov_eax_m32(e, REG(rs));
        emit_alu_eax_imm32(e, ext, imm);
        emit_set_reg_eax(e, rt);
        return true;
    }

    /* rt = (rs < imm), signed or unsigned. The immediate is sign-extended for
     * both; only the comparison differs (DOCS/cpuspecifications.md). */
    if (fn == op_slti || fn == op_sltiu) {
        if (rt == 0) return true;
        emit_mov_eax_m32(e, REG(rs));
        emit_alu_eax_imm32(e, 7, instr_imm_se(instr));         /* cmp */
        emit_setcc_eax(e, (fn == op_slti) ? CC_L : CC_B);
        emit_set_reg_eax(e, rt);
        return true;
    }

    /* rt = imm << 16 — one store, no read at all */
    if (fn == op_lui) {
        if (rt == 0) return true;
        emit_mov_eax_imm32(e, instr_imm(instr) << 16);
        emit_set_reg_eax(e, rt);
        return true;
    }

    /* --- branches and jumps ---------------------------------------------
     *
     * A conditional branch's target is a constant here even when cpu->pc is not,
     * and that is worth being precise about: cpu_branch() computes it from
     * cpu->current_pc, which this instruction stored as an immediate a few bytes
     * ago. The delay-slot hazard that broke the pc folding does not reach it.
     *
     * J and JAL are the opposite case. They read cpu->pc, so they are folded
     * only where the pc fold itself was safe; elsewhere they go out to the
     * handler, which reads the live value. Same for JALR's return address. */

    const uint32_t br_target = pc + 4u + (instr_imm_se(instr) << 2);

    uint8_t bcc = 0xFF; bool cmp_reg = false;
    if      (fn == op_beq)  { bcc = CC_NE; cmp_reg = true; }   /* skip when != */
    else if (fn == op_bne)  { bcc = CC_E;  cmp_reg = true; }
    else if (fn == op_blez) { bcc = CC_G;  }                   /* skip when > 0 */
    else if (fn == op_bgtz) { bcc = CC_LE; }
    if (bcc != 0xFF) {
        emit_mov_eax_m32(e, REG(rs));
        if (cmp_reg) emit_alu_eax_m32(e, 0x3B, REG(rt));       /* cmp eax,[rt] */
        else         emit_test_eax_eax(e);
        Fixup skip = emit_jcc(e, bcc);
        emit_mov_m32_imm(e, OFF(next_pc), br_target);
        emit_mov_m8_imm(e, OFF(branch_taken), 1);
        fixup_here(e, skip);
        return true;
    }

    /* JR: the target is a register, so nothing here is constant except the
     * absence of a call. JALR additionally writes a return address taken from
     * cpu->pc, which pins it to the folded case. */
    if (fn == op_jr || (fn == op_jalr && pc_is_known)) {
        emit_mov_eax_m32(e, REG(rs));                          /* read rs first: */
        emit_mov_m32_eax(e, OFF(cop0_tar));                    /* JALR may write  */
        emit_mov_m32_eax(e, OFF(next_pc));                     /* rd == rs        */
        if (fn == op_jalr) {
            emit_mov_eax_imm32(e, pc + 8u);                    /* cpu->pc + 4     */
            emit_set_reg_eax(e, rd);
        }
        emit_mov_m8_imm(e, OFF(branch_taken), 1);
        return true;
    }

    if ((fn == op_j || fn == op_jal) && pc_is_known) {
        /* cpu->pc is pc+4 here, and it is the top nibble that the target keeps. */
        const uint32_t target = ((pc + 4u) & 0xF0000000u) | (instr_imm_jump(instr) << 2);
        if (fn == op_jal) {
            emit_mov_eax_imm32(e, pc + 8u);
            emit_set_reg_eax(e, REG_RA);
        }
        emit_mov_m32_imm(e, OFF(next_pc), target);
        emit_mov_m32_imm(e, OFF(cop0_tar), target);
        emit_mov_m8_imm(e, OFF(branch_taken), 1);
        return true;
    }

    return false;
}

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

    /* The interrupt check. It ran as a call on every instruction; what the call
     * mostly did was the part that never fires.
     *
     * Cause bit 10 is *not* a latch — CheckPendingInterrupt() rewrites it from
     * (I_STAT & I_MASK) every instruction whether an interrupt is taken or not —
     * so that half cannot be skipped and is emitted branchlessly. The decision
     * that follows is SR.IEc && ((SR & Cause) & 0xFF00), which is false almost
     * always; only when it is true does this go out to the helper, which
     * recomputes the same values (idempotent: same inputs, same answer) and then
     * does the part worth a call — the "next instruction is a GTE op" deferral
     * and the exception itself. */
    {
        const uint32_t stat_off = (uint32_t)offsetof(Interconnect, irq_status);
        const uint32_t mask_off = (uint32_t)offsetof(Interconnect, irq_mask);
        e8(e, 0x48); e8(e, 0x8B); modrm_bx(e, 0, OFF(inter));   /* mov rax,[rbx+inter] */
        emit_movzx_r32_m16_rax(e, 1, stat_off);                 /* movzx ecx,[rax+stat] */
        emit_movzx_r32_m16_rax(e, 2, mask_off);                 /* movzx edx,[rax+mask] */
        emit_test_ecx_edx(e);
        emit_setne_ecx(e);                                      /* ecx = pending ? 1:0 */
        emit_shl_ecx_imm8(e, 10);                               /* ecx <<= 10          */
        emit_mov_eax_m32(e, OFF(cause));
        emit_alu_eax_imm32(e, 4, ~(uint32_t)(1u << 10));        /* and eax, ~IP2       */
        emit_or_eax_ecx(e);
        emit_mov_m32_eax(e, OFF(cause));

        emit_alu_eax_m32(e, 0x23, OFF(sr));                     /* and eax, sr         */
        emit_alu_eax_imm32(e, 4, 0xFF00u);                      /* and eax, 0xFF00     */
        Fixup no_irq = emit_jcc(e, CC_E);
        emit_test_m8_imm(e, OFF(sr), 1);                        /* SR.IEc              */
        Fixup no_iec = emit_jcc(e, CC_E);
        emit_mov_rdi_rbx(e);
        emit_mov_esi_imm32(e, instr);
        emit_mov_rax_imm64(e, (uint64_t)(uintptr_t)&cpu_rec_check_irq);
        emit_call_rax(e);
        emit_test_al_al(e);
        stops[(*nstop)++] = emit_jcc(e, CC_NE);
        fixup_here(e, no_irq);
        fixup_here(e, no_iec);
    }

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

    /* The operation itself, emitted where it can be and called where it cannot.
     *
     * The exception check that follows a call is dead after a native operation
     * and is left out: exception_pending was stored 0 at the top of this
     * instruction, and everything between there and here either leaves the block
     * outright (the interrupt check, the breakpoint walk, the BIOS vector) or
     * cannot write it (the trace fold, the execution ring, the pc stores). The
     * operations emit_native_op() accepts touch no memory and raise nothing. */
    if (!emit_native_op(e, instr, op->fn, pc, pc_is_known)) {
        emit_mov_rdi_rbx(e);
        emit_mov_esi_imm32(e, instr);
        emit_mov_rax_imm64(e, (uint64_t)(uintptr_t)op->fn);
        emit_call_rax(e);

        /* if (exception_pending) stop; */
        emit_cmp_m8_imm(e, OFF(exception_pending), 0);
        stops[(*nstop)++] = emit_jcc(e, CC_NE);
    }

    /* The load-delay rotation, inline. It ran as a call on every instruction —
     * cpu_rec_retire() is six memory operations and a test, so the call and its
     * argument setup were most of what it cost.
     *
     *   if (delay_load_reg) regs[delay_load_reg] = delay_load_value;
     *   delay_load_reg = load_reg_idx; delay_load_value = load_value;
     *   load_reg_idx = 0; regs[0] = 0;
     *
     * The rotation is what makes a delay-slot instruction read the register's
     * old value, and it has to run after the operation rather than before —
     * "isn't updated until the next opcode has completed"
     * (psx-spx-docs/docs/cpuspecifications.md:172-174). */
    {
        emit_mov_eax_m32(e, OFF(delay_load_reg));
        emit_test_eax_eax(e);
        Fixup none = emit_jcc(e, CC_E);
        emit_mov_ecx_m32(e, OFF(delay_load_value));
        emit_mov_regs_rax_ecx(e, OFF(regs));
        fixup_here(e, none);
        emit_mov_eax_m32(e, OFF(load_reg_idx));
        emit_mov_m32_eax(e, OFF(delay_load_reg));
        emit_mov_eax_m32(e, OFF(load_value));
        emit_mov_m32_eax(e, OFF(delay_load_value));
        emit_mov_m32_imm(e, OFF(load_reg_idx), 0);
        emit_mov_m32_imm(e, REG(0), 0);
    }

    emit_inc_r12d(e);
    return !e->overflow;
}

/* --- the rest of the emitter --------------------------------------------- */

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
