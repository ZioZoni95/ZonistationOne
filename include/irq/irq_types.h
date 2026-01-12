#ifndef IRQ_TYPES_H
#define IRQ_TYPES_H

/**
 * @file irq_types.h
 * @brief Interrupt Controller Type Definitions
 * 
 * Modular interrupt system for PlayStation 1 emulator.
 * Thread-safe implementation supporting multi-threaded CPU/GPU execution.
 * 
 * Hardware Context:
 * - PSX has 11 hardware interrupt lines (IRQ0-10)
 * - I_STAT (0x1f801070): Interrupt status register (pending IRQs)
 * - I_MASK (0x1f801074): Interrupt mask register (enabled IRQs)
 * - Edge-triggered interrupt model (rising edge 0->1 sets I_STAT bit)
 * 
 * Thread Safety:
 * - All IRQ operations are protected by mutex
 * - Safe to call from CPU thread and GPU thread
 * - Atomic line state tracking for edge detection
 */

#include <stdint.h>
#include <stdbool.h>
#include "threading.h"

// ============================================================================
// Interrupt Line Definitions
// ============================================================================

/**
 * @brief PSX Hardware Interrupt Lines (IRQ0-IRQ10)
 * 
 * These correspond to bits in I_STAT and I_MASK registers.
 * Reference: PSX-SPX "I/O Ports - Interrupt Control"
 */
typedef enum IrqLine {
    IRQ_VBLANK    = 0,  ///< GPU VBlank interrupt
    IRQ_GPU       = 1,  ///< GPU interrupt (depends on GP1(0x10) mode)
    IRQ_CDROM     = 2,  ///< CD-ROM controller interrupt
    IRQ_DMA       = 3,  ///< DMA controller interrupt
    IRQ_TIMER0    = 4,  ///< Timer 0 interrupt
    IRQ_TIMER1    = 5,  ///< Timer 1 interrupt
    IRQ_TIMER2    = 6,  ///< Timer 2 interrupt
    IRQ_CTRLMEMCARD = 7,///< Controller and Memory Card interrupt
    IRQ_SIO       = 8,  ///< Serial I/O interrupt
    IRQ_SPU       = 9,  ///< Sound Processing Unit interrupt
    IRQ_PIO       = 10, ///< PIO/Lightpen interrupt
    IRQ_LINE_COUNT = 11 ///< Total number of interrupt lines
} IrqLine;

/**
 * @brief Human-readable names for interrupt lines (for logging)
 */
extern const char* IRQ_LINE_NAMES[IRQ_LINE_COUNT];

// ============================================================================
// Interrupt Controller State
// ============================================================================

/**
 * @brief Interrupt Controller State Structure
 * 
 * Manages PSX interrupt system with thread-safe operations.
 * All fields should be accessed through irq_core.h API functions.
 */
typedef struct IrqState {
    // --- Hardware Register State ---
    uint16_t i_stat;        ///< I_STAT register (0x1f801070) - pending IRQs
    uint16_t i_mask;        ///< I_MASK register (0x1f801074) - enabled IRQs
    uint16_t line_state;    ///< Current state of each IRQ line (for edge detection)
    
    // --- Thread Safety ---
    Mutex lock;             ///< Protects all IRQ state from concurrent access
    
    // --- Statistics (for debugging/profiling) ---
    uint32_t request_count[IRQ_LINE_COUNT]; ///< Count of requests per IRQ line
    uint32_t edge_count[IRQ_LINE_COUNT];    ///< Count of rising edges per IRQ line
    uint32_t total_requests;                ///< Total IRQ requests across all lines
    
    // --- Configuration ---
    bool log_enabled;       ///< Enable detailed IRQ logging (rate-limited)
} IrqState;

// ============================================================================
// Hardware Register Addresses
// ============================================================================

#define IRQ_REGS_START        0x1f801070
#define IRQ_STATUS_ADDR       0x1f801070 ///< I_STAT: Read pending IRQs / Write to ACK
#define IRQ_MASK_ADDR         0x1f801074 ///< I_MASK: Read/Write IRQ enable mask
#define IRQ_REGS_END          0x1f801078

#endif // IRQ_TYPES_H
