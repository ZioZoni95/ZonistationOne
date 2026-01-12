#ifndef CPU_EXCEPTIONS_H
#define CPU_EXCEPTIONS_H

#include "cpu_types.h"

// Forward declaration
typedef struct Cpu Cpu;

// ============================================================
// Exception Handling
// ============================================================

/**
 * @brief Triggers a CPU exception
 * @param cpu Pointer to CPU state
 * @param cause Exception cause code
 */
void cpu_exception(Cpu* cpu, Exception cause);

/**
 * @brief Triggers a CPU exception with specific CAUSE and EPC
 * @param cpu Pointer to CPU state
 * @param cause_bits CAUSE register bits
 * @param epc Exception program counter
 * @param vector Exception vector address
 */
void cpu_exception_with_cause(Cpu* cpu, uint32_t cause_bits, uint32_t epc, uint32_t vector);

/**
 * @brief Triggers a CPU exception with specific CAUSE and EPC (default vector)
 * @param cpu Pointer to CPU state
 * @param cause_bits CAUSE register bits
 * @param epc Exception program counter
 */
void cpu_exception_with_cause_epc(Cpu* cpu, uint32_t cause_bits, uint32_t epc);

/**
 * @brief Raise break exception with PCDrv handling
 * @param cpu Pointer to CPU state
 * @param cause_bits CAUSE register bits
 * @param epc Exception program counter
 * @param instruction_bits Break instruction bits
 */
void cpu_raise_break_exception(Cpu* cpu, uint32_t cause_bits, uint32_t epc, uint32_t instruction_bits);

/**
 * @brief Dispatch interrupt exception
 * @param cpu Pointer to CPU state
 */
void cpu_dispatch_interrupt(Cpu* cpu);

/**
 * @brief Handles BIOS system calls
 * @param cpu Pointer to CPU state
 * @param syscall_num System call number
 * @return true if handled, false otherwise
 */
bool handle_bios_syscall(Cpu* cpu, uint32_t syscall_num);

/**
 * @brief Handle A0 syscall (BIOS functions)
 * @param cpu Pointer to CPU state
 */
void handle_a0_syscall(Cpu* cpu);

/**
 * @brief Handle B0 syscall (BIOS functions)
 * @param cpu Pointer to CPU state
 */
void handle_b0_syscall(Cpu* cpu);

/**
 * @brief Handle write syscall (stdout output)
 * @param cpu Pointer to CPU state
 */
void handle_write_syscall(Cpu* cpu);

/**
 * @brief Handle putc syscall (single character output)
 * @param cpu Pointer to CPU state
 */
void handle_putc_syscall(Cpu* cpu);

/**
 * @brief Handle puts syscall (string output)
 * @param cpu Pointer to CPU state
 */
void handle_puts_syscall(Cpu* cpu);

/**
 * @brief Check for pending interrupts and raise if needed
 * @param cpu Pointer to CPU state
 * @return true if interrupt was raised
 */
bool cpu_check_interrupts(Cpu* cpu);

/**
 * @brief Raise exception with BadVaddr register set (for address errors)
 * @param cpu Pointer to CPU state
 * @param cause Exception cause code
 * @param badvaddr Faulting virtual address
 */
void cpu_exception_with_badvaddr(Cpu* cpu, Exception cause, uint32_t badvaddr);

#endif // CPU_EXCEPTIONS_H
