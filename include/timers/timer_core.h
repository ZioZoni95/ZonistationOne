// SPDX-License-Identifier: MIT
// Timer Core Public API
// Based on DuckStation implementation and No$PSX documentation

#ifndef TIMER_CORE_H
#define TIMER_CORE_H

#include "timers/timer_types.h"
#include <stdint.h>
#include <stdbool.h>

// Forward declarations
struct Interconnect;

// =============================================================================
// LIFECYCLE MANAGEMENT
// =============================================================================

/**
 * @brief Initialize the timers subsystem
 * @param timers Pointer to TimersState structure
 * @param inter Pointer to Interconnect (for IRQ requests)
 * 
 * Initializes all three timers to reset state:
 * - Counter = 0
 * - Mode = 0 (free run, no sync, no IRQ)
 * - Target = 0
 * - Counting enabled
 * 
 * Complexity: O(1)
 * Thread-safe: Yes (initializes mutex)
 */
void timers_init(TimersState* timers, struct Interconnect* inter);

/**
 * @brief Reset all timers to initial state
 * @param timers Pointer to TimersState structure
 * 
 * Same as init but preserves interconnect pointer.
 * 
 * Complexity: O(1)
 * Thread-safe: Yes (acquires mutex)
 */
void timers_reset(TimersState* timers);

/**
 * @brief Shutdown the timers subsystem
 * @param timers Pointer to TimersState structure
 * 
 * Releases resources (currently just destroys mutex).
 * 
 * Complexity: O(1)
 * Thread-safe: N/A (should not be called from multiple threads)
 */
void timers_shutdown(TimersState* timers);

// =============================================================================
// REGISTER I/O (Interconnect Interface)
// =============================================================================

/**
 * @brief Read from timer register (32-bit)
 * @param timers Pointer to TimersState structure
 * @param offset Offset from 0x1F801100 (0x00-0x2F)
 * @return 32-bit value (16-bit register data in lower half)
 * 
 * Register layout:
 * - 0x00/0x10/0x20: Counter (read current value)
 * - 0x04/0x14/0x24: Mode (read mode bits)
 * - 0x08/0x18/0x28: Target (read target value)
 * 
 * Side effects:
 * - Reading Mode clears sticky flags (bits 11, 12)
 * 
 * Complexity: O(1)
 * Thread-safe: Yes (acquires mutex)
 */
uint32_t timers_read_register(TimersState* timers, uint32_t offset);

/**
 * @brief Write to timer register (32-bit)
 * @param timers Pointer to TimersState structure
 * @param inter Pointer to Interconnect (for IRQ requests)
 * @param offset Offset from 0x1F801100 (0x00-0x2F)
 * @param value 32-bit value (only lower 16 bits used)
 * 
 * Register layout:
 * - 0x00/0x10/0x20: Counter (write sets value, 2-cycle delay)
 * - 0x04/0x14/0x24: Mode (write updates mode, resets counter, clears sticky flags)
 * - 0x08/0x18/0x28: Target (write sets target value)
 * 
 * Side effects:
 * - Writing Mode resets counter to 0, updates internal state, may trigger IRQ
 * - Writing Counter applies 2-cycle hold delay
 * - Writing Target may immediately trigger IRQ if counter >= new target
 * 
 * Complexity: O(1)
 * Thread-safe: Yes (acquires mutex)
 */
void timers_write_register(TimersState* timers, struct Interconnect* inter, 
                           uint32_t offset, uint32_t value);

// =============================================================================
// PER-TIMER REGISTER ACCESS
// =============================================================================

/**
 * @brief Read timer counter value
 * @param timers Pointer to TimersState structure
 * @param timer_index Timer index (0, 1, 2)
 * @return Current 16-bit counter value
 * 
 * Complexity: O(1)
 * Thread-safe: Yes (acquires mutex)
 */
uint32_t timer_read_counter(TimersState* timers, uint32_t timer_index);

/**
 * @brief Read timer mode register
 * @param timers Pointer to TimersState structure
 * @param timer_index Timer index (0, 1, 2)
 * @return 16-bit mode register value
 * 
 * Side effect: Clears sticky flags (bits 11, 12)
 * 
 * Complexity: O(1)
 * Thread-safe: Yes (acquires mutex)
 */
uint32_t timer_read_mode(TimersState* timers, uint32_t timer_index);

/**
 * @brief Read timer target value
 * @param timers Pointer to TimersState structure
 * @param timer_index Timer index (0, 1, 2)
 * @return Current 16-bit target value
 * 
 * Complexity: O(1)
 * Thread-safe: Yes (acquires mutex)
 */
uint32_t timer_read_target(TimersState* timers, uint32_t timer_index);

/**
 * @brief Write timer counter value
 * @param timers Pointer to TimersState structure
 * @param timer_index Timer index (0, 1, 2)
 * @param value New 16-bit counter value
 * 
 * Side effect: Counter holds at written value for 2 cycles
 * 
 * Complexity: O(1)
 * Thread-safe: Yes (acquires mutex)
 */
void timer_write_counter(TimersState* timers, uint32_t timer_index, uint32_t value);

/**
 * @brief Write timer mode register
 * @param timers Pointer to TimersState structure
 * @param inter Pointer to Interconnect (for IRQ requests)
 * @param timer_index Timer index (0, 1, 2)
 * @param value New 16-bit mode value
 * 
 * Side effects:
 * - Resets counter to 0 with 2-cycle hold
 * - Updates internal state (clock source, sync mode, IRQ enables)
 * - Clears sticky flags and IRQ done flag
 * - May immediately trigger IRQ if conditions met
 * 
 * Complexity: O(1)
 * Thread-safe: Yes (acquires mutex)
 */
void timer_write_mode(TimersState* timers, struct Interconnect* inter, 
                     uint32_t timer_index, uint32_t value);

/**
 * @brief Write timer target value
 * @param timers Pointer to TimersState structure
 * @param inter Pointer to Interconnect (for IRQ requests)
 * @param timer_index Timer index (0, 1, 2)
 * @param value New 16-bit target value
 * 
 * Side effect: May immediately trigger IRQ if counter >= new target
 * 
 * Complexity: O(1)
 * Thread-safe: Yes (acquires mutex)
 */
void timer_write_target(TimersState* timers, struct Interconnect* inter,
                       uint32_t timer_index, uint32_t value);

// =============================================================================
// CYCLE STEPPING (CPU/GPU Integration)
// =============================================================================

/**
 * @brief Add system clock ticks to all timers
 * @param timers Pointer to TimersState structure
 * @param inter Pointer to Interconnect (for IRQ requests)
 * @param sysclk_ticks Number of system clock cycles to advance
 * 
 * Distributes cycles to all three timers based on their clock sources:
 * - Timer 0: sysclk or external (dotclock)
 * - Timer 1: sysclk or external (hblank)
 * - Timer 2: sysclk or sysclk/8
 * 
 * Honors pause delays from writes/resets.
 * Checks for IRQ conditions after incrementing.
 * 
 * Complexity: O(1)
 * Thread-safe: Yes (acquires mutex)
 */
void timers_add_sysclk_ticks(TimersState* timers, struct Interconnect* inter, 
                             uint32_t sysclk_ticks);

/**
 * @brief Add dot clock ticks (external clock for Timer 0)
 * @param timers Pointer to TimersState structure
 * @param inter Pointer to Interconnect (for IRQ requests)
 * @param dotclock_ticks Number of dot clock ticks to add
 * 
 * Used by GPU to provide dot clock ticks for Timer 0 when in external mode.
 * Only affects Timer 0 if using external counting.
 * 
 * Complexity: O(1)
 * Thread-safe: Yes (acquires mutex)
 */
void timers_add_dotclock_ticks(TimersState* timers, struct Interconnect* inter, 
                              uint32_t dotclock_ticks);

/**
 * @brief Add HBlank ticks (external clock for Timer 1)
 * @param timers Pointer to TimersState structure
 * @param inter Pointer to Interconnect (for IRQ requests)
 * @param hblank_ticks Number of HBlank ticks to add
 * 
 * Used when HBlank occurs to provide ticks for Timer 1 when in external mode.
 * Only affects Timer 1 if using external counting.
 * 
 * Complexity: O(1)
 * Thread-safe: Yes (acquires mutex)
 */
void timers_add_hblank_ticks(TimersState* timers, struct Interconnect* inter, 
                            uint32_t hblank_ticks);

/**
 * @brief Add ticks to specific timer
 * @param timers Pointer to TimersState structure
 * @param inter Pointer to Interconnect (for IRQ requests)
 * @param timer_index Timer index (0, 1, 2)
 * @param ticks Number of ticks to add
 * 
 * Used for external clocks (dotclock, hblank).
 * Increments counter, handles overflow/target, checks IRQ.
 * 
 * Complexity: O(1)
 * Thread-safe: Yes (acquires mutex)
 */
void timer_add_ticks(TimersState* timers, struct Interconnect* inter, 
                    uint32_t timer_index, uint32_t ticks);

// =============================================================================
// GATE CONTROL (GPU Integration)
// =============================================================================

/**
 * @brief Set timer gate signal state
 * @param timers Pointer to TimersState structure
 * @param inter Pointer to Interconnect (for IRQ requests)
 * @param timer_index Timer index (0, 1, 2)
 * @param state Gate signal state (true=active, false=inactive)
 * 
 * Updates gate state and handles sync mode transitions:
 * - PAUSE_IN_GATE: Enable/disable counting
 * - RESET_ON_GATE: Reset counter when gate ends
 * - RESET_AND_RUN: Reset and enable counting when gate starts
 * - FREE_RUN: Disable sync after first gate
 * 
 * Complexity: O(1)
 * Thread-safe: Yes (acquires mutex)
 */
void timer_set_gate(TimersState* timers, struct Interconnect* inter,
                   uint32_t timer_index, bool state);

// =============================================================================
// QUERY FUNCTIONS
// =============================================================================

/**
 * @brief Check if timer is using external clock
 * @param timers Pointer to TimersState structure
 * @param timer_index Timer index (0, 1, 2)
 * @return true if using dotclock/hblank/div8
 * 
 * Complexity: O(1)
 * Thread-safe: Yes (acquires mutex)
 */
bool timer_is_using_external_clock(const TimersState* timers, uint32_t timer_index);

/**
 * @brief Check if timer sync is enabled
 * @param timers Pointer to TimersState structure
 * @param timer_index Timer index (0, 1, 2)
 * @return true if sync enabled (mode bit 0)
 * 
 * Complexity: O(1)
 * Thread-safe: Yes (acquires mutex)
 */
bool timer_is_sync_enabled(const TimersState* timers, uint32_t timer_index);

/**
 * @brief Check if timer external IRQ is enabled
 * @param timers Pointer to TimersState structure
 * @param timer_index Timer index (0, 1, 2)
 * @return true if using external clock and IRQ enabled
 * 
 * Used by GPU to determine if it needs to track timer events.
 * 
 * Complexity: O(1)
 * Thread-safe: Yes (acquires mutex)
 */
bool timer_is_external_irq_enabled(const TimersState* timers, uint32_t timer_index);

/**
 * @brief Get cycles until next IRQ
 * @param timers Pointer to TimersState structure
 * @param timer_index Timer index (0, 1, 2)
 * @return Number of ticks until next IRQ, or INT32_MAX if no IRQ pending
 * 
 * Calculates minimum of:
 * - Ticks until counter == target (if IRQ at target enabled)
 * - Ticks until counter == 0xFFFF (if IRQ at overflow enabled)
 * 
 * Returns INT32_MAX if:
 * - No IRQ enabled
 * - Counter not counting
 * - One-shot IRQ already fired
 * 
 * Complexity: O(1)
 * Thread-safe: Yes (acquires mutex)
 */
int32_t timer_get_ticks_until_irq(const TimersState* timers, uint32_t timer_index);

/**
 * @brief Check if timer is currently counting
 * @param timers Pointer to TimersState structure
 * @param timer_index Timer index (0, 1, 2)
 * @return true if counter is incrementing
 * 
 * Counter may be paused due to:
 * - Sync mode (paused by gate)
 * - Timer 2 with sync mode 0/3
 * - Write/reset delay
 * 
 * Complexity: O(1)
 * Thread-safe: Yes (acquires mutex)
 */
bool timer_is_counting(const TimersState* timers, uint32_t timer_index);

// =============================================================================
// STATE MANAGEMENT (Savestate Support)
// =============================================================================

/**
 * @brief Get size of timer state for serialization
 * @return Size in bytes
 * 
 * Complexity: O(1)
 */
size_t timers_get_state_size(void);

/**
 * @brief Serialize timer state
 * @param timers Pointer to TimersState structure
 * @param buffer Output buffer (must be >= timers_get_state_size())
 * @return Number of bytes written
 * 
 * Complexity: O(1)
 * Thread-safe: Yes (acquires mutex)
 */
size_t timers_save_state(const TimersState* timers, void* buffer);

/**
 * @brief Deserialize timer state
 * @param timers Pointer to TimersState structure
 * @param buffer Input buffer
 * @param size Buffer size
 * @return true if successful
 * 
 * Complexity: O(1)
 * Thread-safe: Yes (acquires mutex)
 */
bool timers_load_state(TimersState* timers, const void* buffer, size_t size);

// =============================================================================
// DEBUGGING/DIAGNOSTICS
// =============================================================================

/**
 * @brief Get timer state as string for debugging
 * @param timers Pointer to TimersState structure
 * @param timer_index Timer index (0, 1, 2)
 * @param buffer Output buffer
 * @param size Buffer size
 * @return Number of characters written
 * 
 * Format: "Timer0: counter=0x1234 target=0x5678 mode=0xABCD [COUNTING] [SYNC] [IRQ_TARGET]"
 * 
 * Complexity: O(1)
 * Thread-safe: Yes (acquires mutex)
 */
int timers_get_state_string(const TimersState* timers, uint32_t timer_index, 
                           char* buffer, size_t size);

/**
 * @brief Dump all timer states to log
 * @param timers Pointer to TimersState structure
 * 
 * Logs detailed state of all three timers for debugging.
 * 
 * Complexity: O(1)
 * Thread-safe: Yes (acquires mutex)
 */
void timers_dump_state(const TimersState* timers);

#endif // TIMER_CORE_H
