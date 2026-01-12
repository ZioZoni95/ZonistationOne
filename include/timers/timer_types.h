// SPDX-License-Identifier: MIT
// Timer Types and Definitions
// Based on DuckStation implementation and No$PSX documentation

#ifndef TIMER_TYPES_H
#define TIMER_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "threading.h"

// Forward declaration
struct Interconnect;

// =============================================================================
// TIMER CONSTANTS
// =============================================================================

#define TIMER_COUNT 3                  // Three hardware timers (0, 1, 2)

// Register offsets (per timer, stride 0x10)
#define TIMER_REG_COUNTER 0x0          // Counter value (R/W, 16-bit)
#define TIMER_REG_MODE    0x4          // Mode register (R/W, 16-bit)
#define TIMER_REG_TARGET  0x8          // Target value (R/W, 16-bit)

// Base addresses
#define TIMER0_BASE 0x1F801100
#define TIMER1_BASE 0x1F801110
#define TIMER2_BASE 0x1F801120

// IRQ lines
#define TIMER0_IRQ 4
#define TIMER1_IRQ 5
#define TIMER2_IRQ 6

// Clock rates (Hz)
#define PSX_CPU_HZ      33868800       // 33.8688 MHz
#define PSX_SYSCLK_HZ   PSX_CPU_HZ     // System clock = CPU clock
#define DOTCLOCK_HZ     53222400       // GPU dotclock (~53.2 MHz)
#define HBLANK_HZ       15734          // Horizontal blank rate (~15.7 kHz)

// Hardware timing constants
#define TIMER_RESET_DELAY_CYCLES 2     // Cycles counter stays at 0 after reset
#define TIMER_WRITE_DELAY_CYCLES 2     // Cycles counter holds after write
#define TIMER_OVERFLOW_DELAY_CYCLES 1  // Cycles at 0 after FFFFh wrap

// =============================================================================
// ENUMERATIONS
// =============================================================================

/**
 * @brief Timer synchronization modes (Mode bits 1-2)
 * 
 * For Timer 0 (Hblank) and Timer 1 (Vblank):
 * - PAUSE_IN_GATE: Pause counter while gate is active
 * - RESET_ON_GATE: Reset counter to 0 when gate ends
 * - RESET_AND_RUN: Reset to 0 at gate start, run only during gate
 * - FREE_RUN: Pause until first gate, then ignore gate
 * 
 * For Timer 2:
 * - PAUSE_IN_GATE/FREE_RUN: Stop counter forever
 * - RESET_ON_GATE/RESET_AND_RUN: Free run (ignore sync)
 */
typedef enum {
    TIMER_SYNC_PAUSE_IN_GATE = 0,      // Pause during gate active
    TIMER_SYNC_RESET_ON_GATE = 1,      // Reset at gate end
    TIMER_SYNC_RESET_AND_RUN = 2,      // Reset and run on gate start
    TIMER_SYNC_FREE_RUN = 3            // Free run after first gate
} TimerSyncMode;

/**
 * @brief Timer clock sources (Mode bits 8-9)
 * 
 * Timer 0: 0/2 = System Clock, 1/3 = Dotclock
 * Timer 1: 0/2 = System Clock, 1/3 = Hblank
 * Timer 2: 0/1 = System Clock, 2/3 = System Clock / 8
 */
typedef enum {
    TIMER_CLK_SYSCLK = 0,              // System clock (33.8688 MHz)
    TIMER_CLK_DOTCLOCK = 1,            // Dotclock (Timer 0 only)
    TIMER_CLK_HBLANK = 1,              // Hblank (Timer 1 only)
    TIMER_CLK_SYSCLK_DIV8 = 2          // System clock / 8 (Timer 2)
} TimerClockSource;

/**
 * @brief Timer IRQ mode (Mode bit 7)
 */
typedef enum {
    TIMER_IRQ_PULSE = 0,               // Short pulse (bit 10 briefly 0)
    TIMER_IRQ_TOGGLE = 1               // Toggle bit 10 on/off
} TimerIRQMode;

// =============================================================================
// STRUCTURES
// =============================================================================

/**
 * @brief Timer Mode Register bitfield (matches hardware layout)
 * 
 * Bit  | R/W | Description
 * -----|-----|--------------------------------------------------
 * 0    | R/W | Synchronization Enable (0=Free Run, 1=Sync)
 * 1-2  | R/W | Synchronization Mode (see TimerSyncMode)
 * 3    | R/W | Reset counter to 0 (0=At FFFFh, 1=At Target)
 * 4    | R/W | IRQ when Counter=Target (0=Disable, 1=Enable)
 * 5    | R/W | IRQ when Counter=FFFFh (0=Disable, 1=Enable)
 * 6    | R/W | IRQ Once/Repeat (0=One-shot, 1=Repeatedly)
 * 7    | R/W | IRQ Pulse/Toggle (0=Pulse, 1=Toggle)
 * 8-9  | R/W | Clock Source (see TimerClockSource)
 * 10   | R   | Interrupt Request (0=Yes, 1=No) [inverted]
 * 11   | R   | Reached Target (0=No, 1=Yes) [sticky, cleared on read]
 * 12   | R   | Reached FFFFh (0=No, 1=Yes) [sticky, cleared on read]
 * 13-15| R   | Unknown (always 0)
 * 
 * Note: Bit 10 (interrupt_request_n) is inverted logic (0=IRQ active)
 */
typedef union {
    uint32_t bits;
    struct {
        uint32_t sync_enable : 1;          // Bit 0
        uint32_t sync_mode : 2;            // Bits 1-2 (TimerSyncMode)
        uint32_t reset_at_target : 1;      // Bit 3
        uint32_t irq_at_target : 1;        // Bit 4
        uint32_t irq_on_overflow : 1;      // Bit 5
        uint32_t irq_repeat : 1;           // Bit 6
        uint32_t irq_pulse_n : 1;          // Bit 7 (0=pulse, 1=toggle)
        uint32_t clock_source : 2;         // Bits 8-9 (TimerClockSource)
        uint32_t interrupt_request_n : 1;  // Bit 10 (R) - inverted!
        uint32_t reached_target : 1;       // Bit 11 (R) - sticky
        uint32_t reached_overflow : 1;     // Bit 12 (R) - sticky
        uint32_t _reserved : 19;           // Bits 13-31
    };
} TimerMode;

// Static assertions for TimerMode size
_Static_assert(sizeof(TimerMode) == sizeof(uint32_t), "TimerMode must be 32 bits");

/**
 * @brief Single timer state (one of three hardware timers)
 * 
 * Represents the complete state of Timer 0, 1, or 2.
 * All fields are protected by the parent TimersState mutex.
 */
typedef struct {
    // -------------------------------------------------------------------------
    // Hardware registers
    // -------------------------------------------------------------------------
    TimerMode mode;                    // Mode register (16-bit used)
    uint32_t counter;                  // Current counter value (16-bit used)
    uint32_t target;                   // Target value (16-bit used)
    
    // -------------------------------------------------------------------------
    // Runtime state
    // -------------------------------------------------------------------------
    bool gate;                         // Current gate signal state
    bool use_external_clock;           // Using external clock (dotclock/hblank/div8)
    bool external_counting_enabled;    // External clock is active
    bool counting_enabled;             // Counter is currently incrementing
    bool irq_done;                     // One-shot IRQ has fired (suppresses further IRQs)
    
    // -------------------------------------------------------------------------
    // Cycle tracking for accuracy
    // -------------------------------------------------------------------------
    uint32_t sysclk_div_8_carry;       // Fractional cycles for Timer 2 div8 mode
    uint32_t pause_counter;            // Cycles to remain at 0 after reset/write
    
} TimerState;

/**
 * @brief Complete timers subsystem state
 * 
 * Contains all three hardware timers plus global state.
 * Thread-safe: All access protected by recursive mutex.
 */
typedef struct {
    // -------------------------------------------------------------------------
    // Thread safety
    // -------------------------------------------------------------------------
    Mutex lock;                        // Protects all fields
    
    // -------------------------------------------------------------------------
    // Timer array
    // -------------------------------------------------------------------------
    TimerState timers[TIMER_COUNT];    // Timer 0, 1, 2
    
    // -------------------------------------------------------------------------
    // System integration
    // -------------------------------------------------------------------------
    struct Interconnect* inter;        // Back-pointer for IRQ requests
    
    // -------------------------------------------------------------------------
    // Global cycle tracking
    // -------------------------------------------------------------------------
    uint32_t sysclk_ticks_carry;       // Overclocking compensation (usually 0)
    uint32_t sysclk_div_8_carry;       // Carry for Timer 2 sysclk/8 mode
    
} TimersState;

// =============================================================================
// INLINE HELPER FUNCTIONS
// =============================================================================

/**
 * @brief Convert TimerMode union to raw 32-bit value
 * @param mode Pointer to TimerMode
 * @return Raw 32-bit register value
 * 
 * Complexity: O(1)
 */
static inline uint32_t timer_mode_to_bits(const TimerMode* mode) {
    return mode->bits;
}

/**
 * @brief Convert raw 32-bit value to TimerMode union
 * @param bits Raw register value
 * @return TimerMode union
 * 
 * Complexity: O(1)
 */
static inline TimerMode timer_mode_from_bits(uint32_t bits) {
    TimerMode mode;
    mode.bits = bits;
    return mode;
}

/**
 * @brief Check if timer has any IRQ enabled
 * @param timer Pointer to TimerState
 * @return true if IRQ at target or overflow enabled
 * 
 * Complexity: O(1)
 */
static inline bool timer_is_irq_enabled(const TimerState* timer) {
    return timer->mode.irq_at_target || timer->mode.irq_on_overflow;
}

/**
 * @brief Check if timer is in pulse IRQ mode
 * @param timer Pointer to TimerState
 * @return true if pulse mode (not toggle)
 * 
 * Complexity: O(1)
 */
static inline bool timer_is_pulse_mode(const TimerState* timer) {
    return !timer->mode.irq_pulse_n; // Bit is inverted (0=pulse)
}

/**
 * @brief Get timer index from base address offset
 * @param offset Offset from 0x1F801100 (0x00, 0x10, 0x20)
 * @return Timer index (0, 1, 2) or -1 if invalid
 * 
 * Complexity: O(1)
 */
static inline int timer_index_from_offset(uint32_t offset) {
    uint32_t timer_offset = offset & 0x30; // Extract timer select bits
    if (timer_offset == 0x00) return 0;
    if (timer_offset == 0x10) return 1;
    if (timer_offset == 0x20) return 2;
    return -1; // Invalid
}

/**
 * @brief Get register type from offset
 * @param offset Register offset within timer block
 * @return Register offset (0x0, 0x4, 0x8) or 0xC for invalid
 * 
 * Complexity: O(1)
 */
static inline uint32_t timer_reg_from_offset(uint32_t offset) {
    return offset & 0xC; // Extract register select bits
}

/**
 * @brief Check if timer should use external clock
 * @param timer_index Timer index (0, 1, 2)
 * @param clock_source Clock source bits from mode register
 * @return true if external clock should be used
 * 
 * Complexity: O(1)
 */
static inline bool timer_uses_external_clock(uint32_t timer_index, uint32_t clock_source) {
    if (timer_index == 0) {
        // Timer 0: clock_source 1 or 3 = dotclock
        return (clock_source & 1) != 0;
    } else if (timer_index == 1) {
        // Timer 1: clock_source 1 or 3 = hblank
        return (clock_source & 1) != 0;
    } else if (timer_index == 2) {
        // Timer 2: clock_source 2 or 3 = sysclk/8
        return (clock_source & 2) != 0;
    }
    return false;
}

/**
 * @brief Get IRQ number for timer
 * @param timer_index Timer index (0, 1, 2)
 * @return IRQ line number (4, 5, 6)
 * 
 * Complexity: O(1)
 */
static inline uint32_t timer_get_irq_line(uint32_t timer_index) {
    return TIMER0_IRQ + timer_index;
}

/**
 * @brief Clamp counter value to 16 bits
 * @param value Counter value
 * @return Clamped value (0x0000-0xFFFF)
 * 
 * Complexity: O(1)
 */
static inline uint32_t timer_clamp_counter(uint32_t value) {
    return value & 0xFFFF;
}

/**
 * @brief Check if counter reached target
 * @param counter Current counter value
 * @param target Target value
 * @param old_counter Previous counter value
 * @return true if counter crossed or equals target
 * 
 * Complexity: O(1)
 */
static inline bool timer_reached_target(uint32_t counter, uint32_t target, uint32_t old_counter) {
    // Handle target=0 special case (always matches)
    if (target == 0) {
        return counter == 0 && old_counter != 0;
    }
    // Check if we crossed or equal target
    return (counter >= target && old_counter < target);
}

/**
 * @brief Check if counter reached overflow (0xFFFF)
 * @param counter Current counter value
 * @return true if counter >= 0xFFFF
 * 
 * Complexity: O(1)
 */
static inline bool timer_reached_overflow(uint32_t counter) {
    return counter >= 0xFFFF;
}

// =============================================================================
// DEBUGGING MACROS
// =============================================================================

#ifdef TIMER_DEBUG_VERBOSE
    #define TIMER_LOG_DEBUG(...) LOG_DEBUG(__VA_ARGS__)
#else
    #define TIMER_LOG_DEBUG(...) ((void)0)
#endif

#define TIMER_LOG_INFO(...)  LOG_INFO(__VA_ARGS__)
#define TIMER_LOG_WARN(...)  LOG_WARN(__VA_ARGS__)
#define TIMER_LOG_ERROR(...) LOG_ERROR(__VA_ARGS__)

#endif // TIMER_TYPES_H
