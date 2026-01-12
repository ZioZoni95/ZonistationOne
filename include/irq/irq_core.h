#ifndef IRQ_CORE_H
#define IRQ_CORE_H

/**
 * @file irq_core.h
 * @brief Interrupt Controller Core API
 * 
 * Thread-safe interrupt controller for PlayStation 1 emulator.
 * All functions are safe to call from multiple threads (CPU, GPU, etc.).
 * 
 * Usage Pattern:
 * 1. Initialize: irq_init()
 * 2. Peripherals request IRQs: irq_request()
 * 3. CPU checks pending IRQs: irq_check_pending()
 * 4. BIOS/software reads I_STAT: irq_read_i_stat()
 * 5. BIOS/software writes I_STAT to ACK: irq_write_i_stat()
 * 6. Cleanup: irq_shutdown()
 */

#include "irq_types.h"
#include <stdint.h>
#include <stdbool.h>

// ============================================================================
// Initialization and Cleanup
// ============================================================================

/**
 * @brief Initialize the interrupt controller
 * 
 * Sets up initial state, creates mutex, clears all IRQ registers.
 * Must be called before any other IRQ functions.
 * 
 * @param irq Pointer to IrqState structure
 */
void irq_init(IrqState* irq);

/**
 * @brief Shutdown the interrupt controller
 * 
 * Cleans up resources, destroys mutex.
 * 
 * @param irq Pointer to IrqState structure
 */
void irq_shutdown(IrqState* irq);

// ============================================================================
// Interrupt Request API (for peripherals)
// ============================================================================

/**
 * @brief Request an interrupt on a specific line (thread-safe)
 * 
 * This is the primary API for peripherals (GPU, DMA, Timers, etc.) to
 * signal an interrupt. Uses edge-triggered behavior: only sets I_STAT
 * bit on rising edge (0->1 transition).
 * 
 * Thread-safe: Uses mutex to protect shared state.
 * 
 * @param irq Pointer to IrqState structure
 * @param line Interrupt line to request (IRQ0-IRQ10)
 * @param source Human-readable source name (for logging)
 * @param cpu Pointer to CPU state (for interrupt request update)
 */
void irq_request(IrqState* irq, IrqLine line, const char* source);

/**
 * @brief Clear/deassert an interrupt line (thread-safe)
 * 
 * Deasserts the line state (for edge detection on next request).
 * Note: Does NOT clear I_STAT bit - software must write I_STAT to ACK.
 * 
 * Thread-safe: Uses mutex to protect shared state.
 * 
 * @param irq Pointer to IrqState structure
 * @param line Interrupt line to clear (IRQ0-IRQ10)
 * @param source Human-readable source name (for logging)
 */
void irq_clear(IrqState* irq, IrqLine line, const char* source);

/**
 * @brief Set interrupt line state directly (thread-safe, low-level)
 * 
 * Sets line high or low with automatic edge detection.
 * Most peripherals should use irq_request() instead.
 * 
 * Thread-safe: Uses mutex to protect shared state.
 * 
 * @param irq Pointer to IrqState structure
 * @param line Interrupt line (IRQ0-IRQ10)
 * @param state true = line high, false = line low
 * @param cpu Pointer to CPU state (for interrupt request update)
 */
void irq_set_line(IrqState* irq, IrqLine line, bool state);

// ============================================================================
// Interrupt Check API (for CPU)
// ============================================================================

/**
 * @brief Check if any interrupts are pending and enabled (thread-safe)
 * 
 * Returns true if (I_STAT & I_MASK) != 0, meaning at least one
 * IRQ is both pending (in I_STAT) and enabled (in I_MASK).
 * 
 * CPU should call this to determine if an interrupt should be taken.
 * 
 * Thread-safe: Uses mutex to protect shared state.
 * 
 * @param irq Pointer to IrqState structure
 * @return true if interrupt should be taken, false otherwise
 */
bool irq_check_pending(const IrqState* irq);

/**
 * @brief Get the pending and enabled IRQ bitmask (thread-safe)
 * 
 * Returns (I_STAT & I_MASK), the set of IRQs that are both
 * pending and enabled.
 * 
 * Thread-safe: Uses mutex to protect shared state.
 * 
 * @param irq Pointer to IrqState structure
 * @return Bitmask of pending and enabled IRQs
 */
uint16_t irq_get_pending_mask(const IrqState* irq);

// ============================================================================
// Hardware Register Access API (for memory mapped I/O)
// ============================================================================

/**
 * @brief Read I_STAT register (0x1f801070) (thread-safe)
 * 
 * Returns the current interrupt status register value.
 * Each bit represents a pending IRQ (1 = pending, 0 = not pending).
 * 
 * Thread-safe: Uses mutex to protect shared state.
 * 
 * @param irq Pointer to IrqState structure
 * @return Current I_STAT register value
 */
uint32_t irq_read_i_stat(const IrqState* irq);

/**
 * @brief Write I_STAT register (0x1f801070) to acknowledge IRQs (thread-safe)
 * 
 * PSX behavior: Writing 0 to a bit in I_STAT clears (acknowledges) that IRQ.
 * Bits set to 1 in the write value are ignored.
 * 
 * Example: To acknowledge IRQ0, write ~(1<<0) = 0xFFFE
 * 
 * Thread-safe: Uses mutex to protect shared state.
 * 
 * @param irq Pointer to IrqState structure
 * @param value Value written by software
 */
void irq_write_i_stat(IrqState* irq, uint32_t value);

/**
 * @brief Read I_MASK register (0x1f801074) (thread-safe)
 * 
 * Returns the current interrupt mask register value.
 * Each bit enables/disables an IRQ line (1 = enabled, 0 = disabled).
 * 
 * Thread-safe: Uses mutex to protect shared state.
 * 
 * @param irq Pointer to IrqState structure
 * @return Current I_MASK register value
 */
uint32_t irq_read_i_mask(const IrqState* irq);

/**
 * @brief Write I_MASK register (0x1f801074) (thread-safe)
 * 
 * Sets which IRQ lines are enabled.
 * Each bit enables/disables an IRQ line (1 = enabled, 0 = disabled).
 * 
 * Thread-safe: Uses mutex to protect shared state.
 * 
 * @param irq Pointer to IrqState structure
 * @param value Value written by software
 */
void irq_write_i_mask(IrqState* irq, uint32_t value);

// ============================================================================
// Utility and Debug API
// ============================================================================

/**
 * @brief Get the name of an interrupt line (for logging)
 * 
 * @param line Interrupt line (IRQ0-IRQ10)
 * @return Human-readable name string
 */
const char* irq_get_line_name(IrqLine line);

/**
 * @brief Get interrupt statistics for a specific line
 * 
 * Returns the number of times an IRQ was requested and triggered.
 * 
 * @param irq Pointer to IrqState structure
 * @param line Interrupt line (IRQ0-IRQ10)
 * @param out_request_count Output: number of irq_request() calls
 * @param out_edge_count Output: number of rising edge detections
 */
void irq_get_stats(const IrqState* irq, IrqLine line,
                   uint32_t* out_request_count, uint32_t* out_edge_count);

/**
 * @brief Enable or disable detailed IRQ logging
 * 
 * When enabled, IRQ operations will be logged (rate-limited).
 * Useful for debugging interrupt issues.
 * 
 * @param irq Pointer to IrqState structure
 * @param enabled true to enable logging, false to disable
 */
void irq_set_logging(IrqState* irq, bool enabled);

/**
 * @brief Reset interrupt controller to initial state
 * 
 * Clears all registers and statistics. Used during emulator reset.
 * 
 * @param irq Pointer to IrqState structure
 */
void irq_reset(IrqState* irq);

/**
 * @brief Serialize interrupt controller state (for save states)
 * 
 * @param irq Pointer to IrqState structure
 * @param data Output buffer for state data
 * @param size Size of output buffer
 * @return Number of bytes written, or -1 on error
 */
int irq_serialize(const IrqState* irq, void* data, size_t size);

/**
 * @brief Deserialize interrupt controller state (for save states)
 * 
 * @param irq Pointer to IrqState structure
 * @param data Input buffer with state data
 * @param size Size of input buffer
 * @return Number of bytes read, or -1 on error
 */
int irq_deserialize(IrqState* irq, const void* data, size_t size);

#endif // IRQ_CORE_H
