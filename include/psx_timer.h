#ifndef PSX_TIMER_H
#define PSX_TIMER_H

#include "psx_types.h"

// PSX-SPX: Timer Implementation
// Following guide.tex structure with PSX-SPX timer specifications

// PSX-SPX: Timer Register offsets
#define TIMER0_COUNTER_VALUE    0x1F801100  // Timer 0 Counter
#define TIMER0_COUNTER_MODE     0x1F801104  // Timer 0 Mode
#define TIMER0_COUNTER_TARGET   0x1F801108  // Timer 0 Target

#define TIMER1_COUNTER_VALUE    0x1F801110  // Timer 1 Counter
#define TIMER1_COUNTER_MODE     0x1F801114  // Timer 1 Mode
#define TIMER1_COUNTER_TARGET   0x1F801118  // Timer 1 Target

#define TIMER2_COUNTER_VALUE    0x1F801120  // Timer 2 Counter
#define TIMER2_COUNTER_MODE     0x1F801124  // Timer 2 Mode
#define TIMER2_COUNTER_TARGET   0x1F801128  // Timer 2 Target

// PSX-SPX: Timer Mode bits
#define TIMER_MODE_GATE_ENABLE          0x0001
#define TIMER_MODE_GATE_VBLANK          0x0002
#define TIMER_MODE_RESET_TO_ZERO        0x0008
#define TIMER_MODE_IRQ_ON_TARGET        0x0010
#define TIMER_MODE_IRQ_ON_OVERFLOW      0x0020
#define TIMER_MODE_IRQ_REPEAT           0x0040
#define TIMER_MODE_IRQ_TOGGLE           0x0080
#define TIMER_MODE_CLOCK_SOURCE         0x0300
#define TIMER_MODE_REACHED_TARGET       0x0800
#define TIMER_MODE_REACHED_OVERFLOW     0x1000

typedef struct {
    u32 counter_value;      // Current counter value
    u32 counter_mode;       // Mode/Control register
    u32 counter_target;     // Target value
    
    // Internal state
    u32 current_value;      // Running counter
    bool reached_target;    // Target reached flag
    bool reached_overflow;  // Overflow reached flag
    bool irq_request;       // IRQ pending
    
    // Clock source
    enum {
        CLOCK_SYSTEM = 0,
        CLOCK_DOTCLOCK = 1,
        CLOCK_HBLANK = 2,
        CLOCK_SYSTEM_DIV8 = 3
    } clock_source;
    
} psx_timer_t;

typedef struct {
    psx_timer_t timers[3];  // 3 timers
} psx_timer_system_t;

// Timer interface functions
void timer_init(void);
void timer_reset(void);
void timer_step(u32 cycles);

// Register access
u32 timer_read32(u32 addr);
void timer_write32(u32 addr, u32 value);

// Individual timer operations
void timer_update(int timer_num, u32 cycles);
void timer_check_irq(int timer_num);

#endif // PSX_TIMER_H