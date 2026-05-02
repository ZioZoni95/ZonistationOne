#include "cpu.h"
#include "log.h"
#include "interconnect.h"
#include "gte.h"

// --- Exception Handling Helpers ---
static void log_exception_details(Cpu* cpu, ExceptionCause cause) {

    if (cpu->inter) {
        uint32_t fault_instr = interconnect_load32(cpu->inter, cpu->current_pc);
}
    if (cause == EXCEPTION_INTERRUPT) {
        static uint32_t irq_entry_count = 0;
        if (++irq_entry_count % 100 == 0) {
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

static void acknowledge_interrupts(Cpu* cpu) {
    // NOTE: Per PSX-SPX, the BIOS must acknowledge interrupts by:
    // 1. Writing 0 to the corresponding I_STAT bit
    // 2. Then acknowledging at the peripheral I/O port
    // We do NOT auto-clear here - let the BIOS do it!
    if (cpu->inter) {
        uint16_t current_status = cpu->inter->irq_status;
        uint16_t current_mask = cpu->inter->irq_mask;
        uint16_t pending_interrupts = current_status & current_mask;
        static uint32_t irq_exc_count = 0;
        if (++irq_exc_count % 100 == 0) {
}
        // DO NOT auto-clear! The BIOS exception handler will do this.
        
        // GTE interrupt quirk: if EPC points to a GTE instruction, advance past it
        uint32_t epc_instr = interconnect_load32(cpu->inter, cpu->epc);
        if ((epc_instr & 0xFE000000) == 0x4A000000) {
            LOG_CPU_INFO("[CPU] @PSX-Spex GTE interrupt quirk: EPC advanced to 0x%08x @ 0x%08x", cpu->epc + 4);
            cpu->epc += 4;
        }
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
    if (cause == EXCEPTION_INTERRUPT) {
        acknowledge_interrupts(cpu);
    }
    uint32_t handler_addr = get_exception_vector(cpu);
cpu->pc = handler_addr;
    cpu->next_pc = cpu->pc + 4;
    if (cpu->inter) {
        uint32_t handler_instr = interconnect_load32(cpu->inter, handler_addr);
}
}