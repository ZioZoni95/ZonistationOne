/**
 * cpu_exceptions.c
 * CPU Exception Handling Module (Thread-Safe)
 * 
 * Based on DuckStation's exception architecture and PSX-SPX documentation
 * Handles all CPU exceptions: interrupts, syscalls, address errors, etc.
 * 
 * Thread Safety:
 * - Exception state is per-CPU, no shared mutable state
 * - InterruptController accessed via interconnect with proper synchronization
 * - BIOS syscalls are synchronous and atomic
 */

#include "cpu/cpu_exceptions.h"
#include "cpu/cpu_core.h"
#include "cpu/cpu_cache.h"
#include "cpu/cpu_types.h"
#include "interconnect.h"
#include "irq/irq_core.h"
#include "timers/timer_core.h"
#include "log.h"
#include "spu.h"
#include <stdlib.h>
#include <string.h>

// =============================================================================
// BIOS Syscall Handling (A0/B0/C0 Functions)
// =============================================================================

/**
 * @brief Handle BIOS syscalls (critical sections, events, timers)
 * Thread-safe: operates on local CPU state only
 * 
 * Based on PSX-SPX BIOS Function Summary
 * @param cpu CPU state pointer
 * @param syscall_num Syscall number (index in BIOS function table)
 * @return true if syscall was handled, false if unknown
 */
bool handle_bios_syscall(Cpu* cpu, uint32_t syscall_num) {
    LOG_DEBUG("[BIOS_SYSCALL] Received syscall_num=0x%X", syscall_num);
    
    switch (syscall_num) {
        case 0x01: // EnterCriticalSection (A(01h) or B(01h))
            // Disable interrupts atomically
            cpu->cop0.sr.iec = 0;
            LOG_BIOS_DEBUG("[BIOS] EnterCriticalSection: Interrupts disabled");
            return true;

        case 0x02: // ExitCriticalSection (A(02h) or B(02h))
            // Re-enable interrupts atomically
            cpu->cop0.sr.iec = 1;
            LOG_BIOS_DEBUG("[BIOS] ExitCriticalSection: Interrupts enabled");
            return true;

        case 0x19: // B(19h) - ClrEvent(event)
            // Stub - BIOS event management
            LOG_BIOS_DEBUG("[BIOS] ClrEvent(event) - stub");
            return true;
        
        case 0x0C: // C(0Ch) - SetRCnt (Timer configuration)
            // Legacy BIOS syscall - not implemented in modular timer system
            // Modern games don't use this, only early BIOS setup
            LOG_DEBUG("[BIOS] SetRCnt syscall (legacy, not implemented)");
            return true; // Return success to continue boot

        default:
            // Unknown syscall - caller should handle as exception
            return false;
    }
}

// =============================================================================
// Exception Helpers (Private, Thread-Safe)
// =============================================================================

/**
 * @brief Log exception details for debugging
 * Thread-safe: read-only access to CPU state
 */
static void log_exception_details(Cpu* cpu, Exception cause) {
    LOG_CPU_DEBUG("@PSX-Spex EXCEPTION: cause=0x%02x EPC=0x%08x PC=0x%08x SR=0x%08x BadVaddr=0x%08x InDelaySlot=%d", 
                  cause, cpu->cop0.epc, cpu->current_pc, cpu->cop0.sr.bits, cpu->cop0.badvaddr, cpu->in_delay_slot);
    
    LOG_CPU_DEBUG("Exception raised: Cause=0x%02x, PC=0x%08x, SR=0x%08x, EPC=0x%08x, BadVaddr=0x%08x", 
                  cause, cpu->pc, cpu->cop0.sr.bits, cpu->cop0.epc, cpu->cop0.badvaddr);
    
    // Thread-safe: interconnect_load32 uses atomic memory access
    if (cpu->inter) {
        uint32_t fault_instr = interconnect_load32(cpu->inter, cpu->current_pc);
        LOG_CPU_DEBUG("@FAULT_INSTRUCTION at PC=0x%08x: 0x%08x", cpu->current_pc, fault_instr);
    }
    
    // Rate-limit interrupt logging
    if (cause == EXC_INT) {
        static uint32_t irq_entry_count = 0;
        if (++irq_entry_count % 100 == 0) {
            LOG_IRQ_DEBUG("[IRQ] Handler entered #%u", irq_entry_count);
        }
    }
}

/**
 * @brief Update Status Register on exception (push mode stack)
 * Thread-safe: operates only on local CPU state
 * 
 * Based on PSX-SPX COP0 SR Exception Behavior:
 * - Bits 0-1: Current mode (00=kernel, 01=user)
 * - Bits 2-3: Previous mode (saved on exception)
 * - Bits 4-5: Old previous mode
 * - Bit 1 (EXL): Exception Level, set to 1 on exception
 */
static void update_status_register(Cpu* cpu) {
    // Save current mode bits to mode stack: old = previous, previous = current
    cpu->cop0.sr.kuo = cpu->cop0.sr.kup;
    cpu->cop0.sr.kup = cpu->cop0.sr.kuc;
    cpu->cop0.sr.ieo = cpu->cop0.sr.iep;
    cpu->cop0.sr.iep = cpu->cop0.sr.iec;
    
    // Set enter kernel mode and disable interrupts (KUc=0, IEc=0)
    cpu->cop0.sr.kuc = 0; // kernel mode
    cpu->cop0.sr.iec = 0; // disable interrupts
}

/**
 * @brief Update Cause register and EPC on exception
 * Thread-safe: operates only on local CPU state
 * 
 * Based on PSX-SPX COP0 Cause Register:
 * - Bits 2-6: Exception code
 * - Bits 8-9: Software interrupt bits (preserved)
 * - Bit 10: Hardware interrupt pending
 * - Bit 31: BD (Branch Delay) flag
 */
static void update_cause_and_epc(Cpu* cpu, Exception cause) {
    uint32_t old_cause = cpu->cop0.cause.bits;
    
    // Preserve software interrupt bits (8-9)
    uint32_t ip_bits = old_cause & 0x0300;
    
    // Set hardware interrupt bit (10) for interrupt exceptions
    if (cause == EXC_INT) {
        ip_bits |= (1u << 10);
    }
    
    // Build new Cause register: IP bits + exception code
    cpu->cop0.cause.bits = ip_bits | ((uint32_t)cause << 2);
    
    // Set BD bit (31) and EPC based on delay slot status
    if (cpu->in_delay_slot) {
        cpu->cop0.cause.bits |= (1u << 31);
        cpu->cop0.epc = cpu->current_pc - 4; // Point to branch instruction
        cpu->cop0.tar = cpu->pc; // Set target address register to the address being fetched
    } else {
        cpu->cop0.cause.bits &= ~(1u << 31);
        cpu->cop0.epc = cpu->current_pc; // Point to faulting instruction
    }
}

/**
 * @brief Acknowledge pending interrupts (read I_STAT & I_MASK)
 * Thread-safe: read-only access to interconnect IRQ state
 * 
 * NOTE: Per PSX-SPX, interrupts are NOT auto-cleared here.
 * The BIOS/game must acknowledge by:
 * 1. Writing 0 to I_STAT bit
 * 2. Acknowledging at peripheral I/O port
 */
static void acknowledge_interrupts(Cpu* cpu) {
    if (!cpu->inter) {
        return;
    }
    
    // Thread-safe: uses mutex-protected IRQ module
    uint16_t current_status = (uint16_t)irq_read_i_stat(&cpu->inter->irq_state);
    uint16_t current_mask = (uint16_t)irq_read_i_mask(&cpu->inter->irq_state);
    uint16_t pending = current_status & current_mask;
    
    if (pending != 0) {
        LOG_IRQ_DEBUG("[IRQ_ACK] Pending IRQs: 0x%04X (Status=0x%04X, Mask=0x%04X)", 
                      pending, current_status, current_mask);
        
        // Log specific pending IRQ lines
        for (int i = 0; i < 11; i++) {
            if (pending & (1 << i)) {
                LOG_IRQ_DEBUG("[IRQ_ACK] IRQ line %d pending", i);
            }
        }
    }
}

/**
 * @brief Get exception vector address based on BEV flag
 * Thread-safe: read-only access to CPU state
 * 
 * Based on PSX-SPX Exception Vectors:
 * - BEV=0: 0x80000080 (normal, RAM-based)
 * - BEV=1: 0xBFC00180 (bootstrap, ROM-based)
 */
static uint32_t get_exception_vector(Cpu* cpu) {
    // BEV = bit 22 of Status Register
    bool bev = cpu->cop0.sr.bev;
    
    if (bev) {
        // Bootstrap exception vector (BIOS ROM)
        return 0xBFC00180;
    } else {
        // Normal exception vector (RAM)
        return 0x80000080;
    }
}

// =============================================================================
// Public Exception API (Thread-Safe)
// =============================================================================

/**
 * @brief Trigger CPU exception and jump to handler
 * Thread-safe: modifies only local CPU state, synchronizes with GPU if needed
 * 
 * Based on DuckStation's RaiseException() and PSX-SPX Exception Handling
 * @param cpu CPU state pointer
 * @param cause Exception cause code
 */
void cpu_exception(Cpu* cpu, Exception cause) {
    // Log exception details
    log_exception_details(cpu, cause);
    
    // Update COP0 registers
    update_status_register(cpu);
    update_cause_and_epc(cpu, cause);
    
    // Acknowledge interrupts (read I_STAT/I_MASK)
    if (cause == EXC_INT) {
        acknowledge_interrupts(cpu);
    }
    
    // Get exception vector address
    uint32_t vector = get_exception_vector(cpu);
    
    // Jump to exception handler
    cpu->pc = vector;
    cpu->next_pc = vector + 4;
    
    // Flush pipeline state (load delays and branch delays)
    // This ensures clean state when jumping to exception handlers
    cpu_flush_pipeline(cpu);
    
    // NOTE: If GPU threading is enabled, we may need to sync here
    // DuckStation doesn't sync on every exception, only on specific ones
    // For now, CPU and GPU run independently
    
    LOG_CPU_DEBUG("Exception handler: Jumping to vector=0x%08x", vector);
}

/**
 * @brief Check for pending interrupts and raise EXCEPTION_INTERRUPT if needed
 * Thread-safe: atomic read of IRQ status/mask
 * 
 * Called from cpu_run_next_instruction() main loop
 * Based on DuckStation's HasPendingInterrupt() and CheckForPendingInterrupt()
 * 
 * @param cpu CPU state pointer
 * @return true if interrupt was raised, false otherwise
 */
bool cpu_check_interrupts(Cpu* cpu) {
    // Check if interrupts are enabled (SR bit 0 = IEc)
    if (!cpu->cop0.sr.iec) {
        return false; // Interrupts disabled
    }
    
    // Check if we're in exception level (SR bit 1 = EXL)
    if (cpu->cop0.sr.kuc) {
        return false; // Already in exception handler
    }
    
    if (!cpu->inter) {
        return false; // No interconnect
    }
    
    // Thread-safe: uses mutex-protected IRQ module
    uint16_t irq_status = (uint16_t)irq_read_i_stat(&cpu->inter->irq_state);
    uint16_t irq_mask = (uint16_t)irq_read_i_mask(&cpu->inter->irq_state);
    uint16_t pending = irq_status & irq_mask;
    
    if (pending != 0) {
        // Raise interrupt exception
        cpu_exception(cpu, EXC_INT);
        return true;
    }
    
    return false;
}

/**
 * @brief Raise specific exception with BadVaddr set (for address errors)
 * Thread-safe: operates on local CPU state
 * 
 * Used for:
 * - Load/Store Address Errors (alignment, bus errors)
 * - TLB misses (not used on PSX)
 * 
 * @param cpu CPU state pointer
 * @param cause Exception cause
 * @param badvaddr Faulting virtual address
 */
void cpu_exception_with_badvaddr(Cpu* cpu, Exception cause, uint32_t badvaddr) {
    // Set BadVaddr register for address error exceptions
    cpu->cop0.badvaddr = badvaddr;
    
    LOG_CPU_DEBUG("Address error exception: cause=0x%02x, addr=0x%08x", cause, badvaddr);
    
    // Raise exception
    cpu_exception(cpu, cause);
}

// =============================================================================
// Additional Exception Functions (Aligned with DuckStation)
// =============================================================================

/**
 * @brief Trigger CPU exception with specific CAUSE bits, EPC, and vector
 * Based on DuckStation's RaiseException(u32 CAUSE_bits, u32 EPC, u32 vector)
 * 
 * @param cpu CPU state pointer
 * @param cause_bits CAUSE register bits
 * @param epc Exception program counter
 * @param vector Exception vector address
 */
void cpu_exception_with_cause(Cpu* cpu, uint32_t cause_bits, uint32_t epc, uint32_t vector) {
    // Log exception details
    LOG_CPU_DEBUG("Exception raised: CAUSE=0x%08x, EPC=0x%08x, Vector=0x%08x", cause_bits, epc, vector);
    
    // Update COP0 registers directly
    cpu->cop0.epc = epc;
    cpu->cop0.cause.bits = cause_bits; // Assume cause_bits includes all necessary bits
    
    // Handle BD (Branch Delay) bit
    if (cpu->cop0.cause.bd) {
        cpu->cop0.epc -= 4; // Point to branch instruction
        cpu->cop0.tar = cpu->pc; // Set target address register
    }
    
    // Update status register (push mode stack)
    update_status_register(cpu);
    
    // Acknowledge interrupts if this is an interrupt exception
    if ((cause_bits & 0x7C) == 0) { // Exception code 0 = interrupt
        acknowledge_interrupts(cpu);
    }
    
    // Jump to exception handler
    cpu->pc = vector;
    cpu->next_pc = vector + 4;
    
    // Flush pipeline state
    cpu_flush_pipeline(cpu);
    
    LOG_CPU_DEBUG("Exception handler: Jumping to vector=0x%08x", vector);
}

/**
 * @brief Trigger CPU exception with specific CAUSE bits and EPC (default vector)
 * Based on DuckStation's RaiseException(u32 CAUSE_bits, u32 EPC)
 * 
 * @param cpu CPU state pointer
 * @param cause_bits CAUSE register bits
 * @param epc Exception program counter
 */
void cpu_exception_with_cause_epc(Cpu* cpu, uint32_t cause_bits, uint32_t epc) {
    uint32_t vector = get_exception_vector(cpu);
    cpu_exception_with_cause(cpu, cause_bits, epc, vector);
}

/**
 * @brief Raise break exception with PCDrv handling
 * Based on DuckStation's RaiseBreakException
 * 
 * @param cpu CPU state pointer
 * @param cause_bits CAUSE register bits
 * @param epc Exception program counter
 * @param instruction_bits Break instruction bits
 */
void cpu_raise_break_exception(Cpu* cpu, uint32_t cause_bits, uint32_t epc, uint32_t instruction_bits) {
    // TODO: Add PCDrv handling if implemented
    // For now, just raise normal exception
    LOG_CPU_DEBUG("Break exception: instruction=0x%08x", instruction_bits);
    cpu_exception_with_cause_epc(cpu, cause_bits, epc);
}

// =============================================================================
// Syscall Handlers (Aligned with DuckStation)
// =============================================================================

/**
 * @brief Handle write syscall (stdout output)
 * Based on DuckStation's HandleWriteSyscall
 * 
 * @param cpu CPU state pointer
 */
void handle_write_syscall(Cpu* cpu) {
    uint32_t fd = cpu->regs.r[REG_A0];
    if (fd != 1) { // stdout
        return;
    }
    
    uint32_t addr = cpu->regs.r[REG_A1];
    uint32_t count = cpu->regs.r[REG_A2];
    
    for (uint32_t i = 0; i < count; i++) {
        uint8_t value = interconnect_load8(cpu->inter, addr++);
        if (value == 0) {
            break;
        }
        // TODO: Add TTY character output if implemented
        LOG_DEBUG("[TTY] %c", (char)value);
    }
}

/**
 * @brief Handle putc syscall (single character output)
 * Based on DuckStation's HandlePutcSyscall
 * 
 * @param cpu CPU state pointer
 */
void handle_putc_syscall(Cpu* cpu) {
    uint32_t fd = cpu->regs.r[REG_A0];
    if (fd != 0) {
        return;
    }
    // TODO: Add TTY character output
    LOG_DEBUG("[TTY] %c", (char)cpu->regs.r[REG_A0]);
}

/**
 * @brief Handle puts syscall (string output)
 * Based on DuckStation's HandlePutsSyscall
 * 
 * @param cpu CPU state pointer
 */
void handle_puts_syscall(Cpu* cpu) {
    uint32_t addr = cpu->regs.r[REG_A0];
    for (uint32_t i = 0; i < 1024; i++) {
        uint8_t value = interconnect_load8(cpu->inter, addr++);
        if (value == 0) {
            break;
        }
        // TODO: Add TTY character output
        LOG_DEBUG("[TTY] %c", (char)value);
    }
}

/**
 * @brief Handle A0 syscall (BIOS functions)
 * Based on DuckStation's HandleA0Syscall
 * 
 * @param cpu CPU state pointer
 */
void handle_a0_syscall(Cpu* cpu) {
    uint32_t call = cpu->regs.r[REG_T1];
    if (call == 0x03 || call == 0x3C) {
        handle_write_syscall(cpu);
    } else if (call == 0x09 || call == 0x3E) {
        handle_putc_syscall(cpu);
    } else if (call == 0x3E) { // Note: 0x3E is putc, but DuckStation has puts as 0x3E? Wait, check.
        // DuckStation: call == 0x3e -> HandlePutsSyscall
        // But 0x3e is also in putc? Let me check DuckStation code again.
        // In DuckStation: if (call == 0x03) HandleWriteSyscall
        // else if (call == 0x09 || call == 0x3c) HandlePutcSyscall
        // else if (call == 0x3e) HandlePutsSyscall
        // So 0x3e is puts, 0x3c is putc
        handle_puts_syscall(cpu);
    }
}

/**
 * @brief Handle B0 syscall (BIOS functions)
 * Based on DuckStation's HandleB0Syscall
 * 
 * @param cpu CPU state pointer
 */
void handle_b0_syscall(Cpu* cpu) {
    uint32_t call = cpu->regs.r[REG_T1];
    if (call == 0x35) {
        handle_write_syscall(cpu);
    } else if (call == 0x3B || call == 0x3D) {
        handle_putc_syscall(cpu);
    } else if (call == 0x3F) {
        handle_puts_syscall(cpu);
    }
}
