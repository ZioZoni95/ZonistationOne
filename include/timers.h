// timers.h
#ifndef TIMERS_H
#define TIMERS_H

#include <stdint.h>
#include <stdbool.h>


// Forward declaration needed if Timers struct holds Interconnect pointer later
struct Interconnect;

// --- Timer Register Offsets (relative to timer base) ---
// Timer 0: 0x1F801100, Timer 1: 0x1F801110, Timer 2: 0x1F801120
#define TMR_REG_VAL    0x0 // Counter Value Register (16-bit R/W)
#define TMR_REG_MODE   0x4 // Mode Register (16-bit R/W)
#define TMR_REG_TARGET 0x8 // Target Value Register (16-bit R/W)

// --- Timer Mode Register Bits (per nocash/PSX-Spex) ---
// Bit 0: Sync Enable
// Bit 1-2: Sync Mode
// Bit 3: Reset on Target
// Bit 4: IRQ on Target
// Bit 5: IRQ on 0xFFFF
// Bit 6: IRQ Repeat
// Bit 7: IRQ Pulse
// Bit 8-9: Clock Source
// Bit 10: IRQ Request (read-only)
// Bit 11: Reached Target (sticky)
// Bit 12: Reached 0xFFFF (sticky)

// --- Structure for a Single Timer ---
typedef struct {
    uint16_t counter; // Current 16-bit counter value
    uint16_t mode;    // 16-bit mode register value
    uint16_t target;  // 16-bit target value

    // Internal emulation state derived from mode register & runtime behavior
    bool sync_enable;       // Mode[0]
    uint8_t sync_mode;      // Mode[1-2]
    bool reset_on_target;   // Mode[3]
    bool irq_on_target;     // Mode[4]
    bool irq_on_ffff;       // Mode[5]
    bool irq_repeat;        // Mode[6]
    bool irq_pulse;         // Mode[7]
    uint8_t clock_source;   // Mode[8-9]

    bool interrupt_requested; // Internal flag: True if IRQ condition met this cycle
    bool reached_target_flag; // Internal sticky flag mirroring Mode[11]
    bool reached_ffff_flag;   // Internal sticky flag mirroring Mode[12]

    // --- Added for cycle-accurate emulation ---
    uint32_t rate;           // Clock rate for this timer
    uint32_t irq;            // IRQ line (4 for Timer0, 5 for Timer1, 6 for Timer2)
    uint32_t counter_state;  // Counting mode (to target or overflow)
    uint32_t irq_state;      // Current IRQ state (0 or 1)
    uint32_t cycle;          // Current cycle count
    uint32_t cycle_start;    // Cycle when timer was started

    // Variables for handling fractional clock cycles might be needed here later
    // double fractional_cycles;

} Timer;

// --- Structure for all Three Timers ---
typedef struct {
    Timer timers[3]; // Array containing state for Timer 0, Timer 1, Timer 2

    // Pointer back to interconnect needed for requesting interrupts
    struct Interconnect* inter;
    double fractional_ticks[3]; // <<< ADD THIS

} Timers;

// --- Timer/IRQ/Mode Constants ---
#define TIMER_COUNT_TO_OVERFLOW  0
#define TIMER_COUNT_TO_TARGET    1
#define TIMER0_IRQ  4
#define TIMER1_IRQ  5
#define TIMER2_IRQ  6
#define TIMER_RATE_1     1
#define TIMER_RATE_5     5
#define TIMER_RATE_8     8

// --- Function Prototypes ---

/**
 * @brief Initializes the state of all three timers.
 * @param timers Pointer to the Timers structure.
 * @param inter Pointer to the Interconnect (needed for interrupts).
 */
void timers_init(Timers* timers, struct Interconnect* inter);

/**
 * @brief Reads a 16-bit value from a timer register.
 * @param timers Pointer to the Timers structure.
 * @param timer_index Index of the timer (0, 1, or 2).
 * @param offset Register offset (0x0, 0x4, 0x8).
 * @return The 16-bit value read.
 */
uint16_t timer_read16(Timers* timers, int timer_index, uint32_t offset);

/**
 * @brief Reads a 32-bit value from a timer register pair (Not standard PSX access).
 * Included for completeness if needed by interconnect, but likely just reads lower 16.
 * @param timers Pointer to the Timers structure.
 * @param timer_index Index of the timer (0, 1, or 2).
 * @param offset Register offset (0x0, 0x4, 0x8).
 * @return The 32-bit value (likely just the 16-bit register zero-extended).
 */
uint32_t timer_read32(Timers* timers, int timer_index, uint32_t offset);


/**
 * @brief Writes a 16-bit value to a timer register.
 * @param timers Pointer to the Timers structure.
 * @param timer_index Index of the timer (0, 1, or 2).
 * @param offset Register offset (0x0, 0x4, 0x8).
 * @param value The 16-bit value to write.
 */
void timer_write16(Timers* timers, int timer_index, uint32_t offset, uint16_t value);

/**
 * @brief Writes a 32-bit value to a timer register pair (Not standard PSX access).
 * Included for completeness if needed by interconnect, likely just writes lower 16.
 * @param timers Pointer to the Timers structure.
 * @param timer_index Index of the timer (0, 1, or 2).
 * @param offset Register offset (0x0, 0x4, 0x8).
 * @param value The 32-bit value (lower 16 bits are likely used).
 */
void timer_write32(Timers* timers, int timer_index, uint32_t offset, uint32_t value);

/**
 * @brief Steps the timers forward by a number of elapsed master clock cycles.
 * Updates counters, checks for target/overflow, and requests interrupts.
 * @param timers Pointer to the Timers structure.
 * @param cycles Number of master clock cycles that have passed.
 */
void timers_step(Timers* timers, uint32_t cycles);

// --- BIOS Timer Functions (stubs, not used by BIOS itself) ---
int bios_init_timer(int t, uint16_t reload, uint16_t flags);
int bios_get_timer(int t);
int bios_enable_timer_irq(int t);
int bios_disable_timer_irq(int t);
int bios_restart_timer(int t);
int bios_ChangeClearRCnt(int t, int flag);

// --- New Timer/Event Function Prototypes ---
void timers_update(Timers* timers);  // Main update function for event system
void timers_schedule_next(Timers* timers);  // Schedule next timer event
uint32_t timers_calculate_frame_cycles(void);  // Frame timing
uint32_t timers_calculate_line_cycles(void);   // Line timing

// --- BEGIN: PCSX ReARMed-inspired logic ---
// Called on every VBlank to reset Timer0 and schedule its event, ensuring correct VBlank/Timer0/IRQ0 coupling as required by the PS1 BIOS.
void timers_on_vblank(Timers* timers);
// --- END: PCSX ReARMed-inspired logic ---

void timers_schedule_next_event(Timers* timers, int timer_index);

// --- BEGIN: PCSX ReARMed-inspired Timer Event Handlers ---
void timer0_event_handler(struct Interconnect* sys);
void timer1_event_handler(struct Interconnect* sys);
void timer2_event_handler(struct Interconnect* sys);
// --- END: PCSX ReARMed-inspired Timer Event Handlers ---

#endif // TIMERS_H