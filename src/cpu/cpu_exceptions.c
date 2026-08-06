/* SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 ZioZoni95
 *
 * Part of ZoniStation One, a PlayStation 1 emulator.
 * See LICENSE for the full licence text and THIRD-PARTY.md for the
 * components of this project that have other authors.
 */
#include "cpu.h"
#include "log.h"
#include "interconnect.h"

// --- Exception Handling Helpers ---
static void log_exception_details(Cpu* cpu, ExceptionCause cause) {
    if (cause != EXCEPTION_INTERRUPT && cause != EXCEPTION_SYSCALL && cause != EXCEPTION_BREAK) {
        uint32_t epc = cpu->in_delay_slot ? (cpu->current_pc - 4) : cpu->current_pc;
        LOG_CPU_DEBUG("[CPU] Exception 0x%02x at 0x%08x (epc=0x%08x, BD=%s)",
                      (uint8_t)cause, cpu->current_pc, epc,
                      cpu->in_delay_slot ? "true" : "false");

        // One-shot: freeze trace + dump immediately before exception handler overwrites it
        static bool first_exception_dumped = false;
        if (!first_exception_dumped) {
            first_exception_dumped = true;
            // Freeze FIRST, before any more instructions execute
            cpu->exec_trace_frozen = true;
            cpu_dump_exec_trace(cpu, "logs/exec_trace_crash.log");
            LOG_CPU_ERROR("[CPU] === FIRST EXCEPTION FULL STATE (cause=0x%02x) ===", (uint8_t)cause);
            LOG_CPU_ERROR("[CPU]  PC=0x%08x  nPC=0x%08x  curPC=0x%08x  BD=%d",
                          cpu->pc, cpu->next_pc, cpu->current_pc, cpu->in_delay_slot);
            LOG_CPU_ERROR("[CPU]  EPC=0x%08x  Cause=0x%08x  SR=0x%08x",
                          epc, (uint32_t)cause << 2, cpu->sr);
            for (int i = 0; i < 32; i += 4) {
                LOG_CPU_ERROR("[CPU]  $%02d=0x%08x  $%02d=0x%08x  $%02d=0x%08x  $%02d=0x%08x",
                              i,   cpu->regs[i],
                              i+1, cpu->regs[i+1],
                              i+2, cpu->regs[i+2],
                              i+3, cpu->regs[i+3]);
            }
            LOG_CPU_ERROR("[CPU] === END FIRST EXCEPTION STATE ===");
        }
    }
}

static void update_status_register(Cpu* cpu) {
    // On exception: push mode stack. Shift bits 0-5 left by 2.
    // Result: IEc/KUc -> IEp/KUp, IEp/KUp -> IEo/KUo, new IEc/KUc = 0 (kernel + IRQ disabled).
    // R3000A has no EXL bit; bit 1 is KUc (0=kernel, 1=user). Must stay 0 here.
    uint32_t old_sr = cpu->sr;
    uint32_t new_sr = old_sr;
    new_sr &= ~(0x3F); // Clear bits 0-5
    new_sr |= ((old_sr >> 0) & 0x3) << 2;  // old IEc/KUc  -> IEp/KUp
    new_sr |= ((old_sr >> 2) & 0x3) << 4;  // old IEp/KUp  -> IEo/KUo
    // bits 0-1 stay 0: kernel mode, interrupts disabled
    cpu->sr = new_sr;
}

static void update_cause_and_epc(Cpu* cpu, ExceptionCause cause) {
    uint32_t old_cause = cpu->cause;
    // PSX-SPX: COP0 Cause register IP bits
    // Bit 8-9: Software interrupts (read/write latches)
    // Bit 10: Hardware interrupt (NOT a latch - reflects (I_STAT & I_MASK) != 0)
    // Bits 11-15: Always zero on PSX
    uint32_t ip_bits = old_cause & 0x0300; // Preserve software interrupt bits (8-9) only
    
    if (cause == EXCEPTION_INTERRUPT) {
        // For interrupt exceptions, set bit 10 (hardware interrupt pending)
        ip_bits |= (1u << 10);
    }
    
    // Set exception code (bits 2-6) and IP bits (bits 8-15)
    cpu->cause = ip_bits | ((uint32_t)cause << 2);
    
    // Set BD bit (bit 31) if exception in branch delay slot
    if (cpu->in_delay_slot) {
        cpu->cause |= (1u << 31);
        cpu->epc = cpu->current_pc - 4;
    } else {
        cpu->cause &= ~(1u << 31);
        cpu->epc = cpu->current_pc;
    }
}

static uint32_t get_exception_vector(Cpu* cpu) {
    return (cpu->sr & (1 << 22)) ? 0xbfc00180 : 0x80000080;
}

void cpu_exception(Cpu* cpu, ExceptionCause cause) {
    cpu->exception_pending = true;
    log_exception_details(cpu, cause);
    update_status_register(cpu);
    update_cause_and_epc(cpu, cause);
    uint32_t handler_addr = get_exception_vector(cpu);
    cpu->pc = handler_addr;
    cpu->next_pc = cpu->pc + 4;
}