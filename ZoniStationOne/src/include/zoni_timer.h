#ifndef ZONI_TIMER_H
#define ZONI_TIMER_H

#include "zoni_common.h"

// PlayStation timer constants (following PCSX ReARMed)
#define ZONI_TIMER_COUNT 3
#define ZONI_TIMER_BASE 0x1F801100

// Timer interrupt bits (following PCSX ReARMed)
#define ZONI_TIMER_IRQ_0 0x10  // Timer 0 interrupt
#define ZONI_TIMER_IRQ_1 0x20  // Timer 1 interrupt  
#define ZONI_TIMER_IRQ_2 0x40  // Timer 2 interrupt

// Timer mode bits
#define ZONI_TIMER_MODE_IRQ_ENABLE 0x1000
#define ZONI_TIMER_MODE_IRQ_TARGET 0x0800
#define ZONI_TIMER_MODE_IRQ_REPEAT 0x0400
#define ZONI_TIMER_MODE_IRQ_PULSE  0x0200
#define ZONI_TIMER_MODE_CLOCK_SRC  0x0100

// Timer structure (following PCSX ReARMed structure)
typedef struct {
    u16 mode;           // Timer mode register
    u16 target;         // Timer target value
    u32 rate;           // Timer rate (cycles per increment)
    u32 irq;            // Interrupt bit for this timer
    u32 counter_state;  // Current counter state
    u32 irq_state;      // Interrupt state
    u32 cycle;          // Cycle counter
    u32 cycle_start;    // Cycle when timer started
} zoni_timer_t;

// Timer system structure
typedef struct {
    zoni_timer_t timers[ZONI_TIMER_COUNT];
    u32 h_sync_count;   // Horizontal sync counter
    u32 frame_counter;   // Frame counter
    bool initialized;
} zoni_timer_system_t;

// Function prototypes
zoni_error_t zoni_timer_init(zoni_timer_system_t* timer_system);
void zoni_timer_reset(zoni_timer_system_t* timer_system);
void zoni_timer_update(zoni_timer_system_t* timer_system, u32 cycles);

// Timer register access functions
u32 zoni_timer_read_count(zoni_timer_system_t* timer_system, u32 index);
u32 zoni_timer_read_mode(zoni_timer_system_t* timer_system, u32 index);
u32 zoni_timer_read_target(zoni_timer_system_t* timer_system, u32 index);

void zoni_timer_write_count(zoni_timer_system_t* timer_system, u32 index, u32 value);
void zoni_timer_write_mode(zoni_timer_system_t* timer_system, u32 index, u32 value);
void zoni_timer_write_target(zoni_timer_system_t* timer_system, u32 index, u32 value);

// Interrupt handling
bool zoni_timer_check_interrupts(zoni_timer_system_t* timer_system);
u32 zoni_timer_get_interrupt_status(zoni_timer_system_t* timer_system);

#endif // ZONI_TIMER_H
