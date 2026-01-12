# Timer Module Modular Refactoring Plan

## Overview
Refactor the timer subsystem following DuckStation architecture with O(1) complexity guarantees, thread safety, and proper hardware accuracy.

## Architecture Goals

### 1. **Modular Structure**
```
include/timers/
  ├── timer_types.h    (350 lines) - Type definitions, enums, structs
  └── timer_core.h     (250 lines) - Public API declarations

src/timers/
  └── timer_core.c     (800 lines) - Complete implementation
```

### 2. **Thread Safety**
- Recursive mutex protection for all timer operations
- Atomic register access
- No race conditions during IRQ handling

### 3. **O(1) Complexity Guarantees**
- Direct register access: O(1)
- Counter increment: O(1)
- IRQ checking: O(1)
- Mode updates: O(1)
- No iterative searches or dynamic allocation

### 4. **Hardware Accuracy**
- Cycle-accurate counter incrementing
- Proper sync modes (pause, reset, free-run)
- Clock source handling (sysclk, dotclock, hblank, div8)
- 2-cycle delays after writes/resets
- Sticky status flags (reached_target, reached_overflow)

## Component Breakdown

### `timer_types.h` (350 lines)

#### Enums (50 lines)
```c
typedef enum {
    TIMER_SYNC_PAUSE_IN_GATE = 0,    // Pause during gate
    TIMER_SYNC_RESET_ON_GATE = 1,    // Reset at gate end
    TIMER_SYNC_RESET_AND_RUN = 2,    // Reset and run on gate start
    TIMER_SYNC_FREE_RUN = 3          // Free run after gate
} TimerSyncMode;

typedef enum {
    TIMER_CLK_SYSCLK = 0,         // System clock
    TIMER_CLK_DOTCLOCK = 1,       // Dotclock (Timer 0)
    TIMER_CLK_HBLANK = 1,         // HBlank (Timer 1)
    TIMER_CLK_SYSCLK_DIV8 = 2     // System clock / 8 (Timer 2)
} TimerClockSource;

typedef enum {
    TIMER_IRQ_PULSE = 0,          // Short pulse (bit 10 = 0 briefly)
    TIMER_IRQ_TOGGLE = 1          // Toggle bit 10
} TimerIRQMode;
```

#### Structures (150 lines)
```c
// Counter Mode bitfield (matches hardware)
typedef struct {
    uint32_t sync_enable : 1;          // Bit 0
    uint32_t sync_mode : 2;            // Bits 1-2
    uint32_t reset_at_target : 1;      // Bit 3
    uint32_t irq_at_target : 1;        // Bit 4
    uint32_t irq_on_overflow : 1;      // Bit 5
    uint32_t irq_repeat : 1;           // Bit 6
    uint32_t irq_pulse_n : 1;          // Bit 7
    uint32_t clock_source : 2;         // Bits 8-9
    uint32_t interrupt_request_n : 1;  // Bit 10 (R)
    uint32_t reached_target : 1;       // Bit 11 (R)
    uint32_t reached_overflow : 1;     // Bit 12 (R)
    uint32_t _reserved : 19;           // Bits 13-31
} TimerMode;

// Single timer state
typedef struct {
    TimerMode mode;               // Mode register (as bitfield)
    uint32_t counter;            // Current counter value (16-bit used)
    uint32_t target;             // Target value (16-bit used)
    
    // Runtime state
    bool gate;                   // Current gate signal state
    bool use_external_clock;     // Using dotclock/hblank/div8
    bool external_counting_enabled; // External clock active
    bool counting_enabled;       // Counter is running
    bool irq_done;              // One-shot IRQ fired flag
    
    // Cycle tracking for accuracy
    uint32_t sysclk_div_8_carry; // Fractional ticks for div8
    uint32_t pause_counter;      // Cycles to stay at 0 after reset
} TimerState;

// All three timers
typedef struct {
    RecursiveMutex lock;              // Thread safety
    TimerState timers[3];             // Timer 0, 1, 2
    struct Interconnect* inter;       // For IRQ requests
    uint32_t sysclk_ticks_carry;      // Overclocking compensation
} TimersState;
```

#### Helper Macros/Constants (150 lines)
```c
// Register offsets
#define TIMER_REG_COUNTER 0x0
#define TIMER_REG_MODE    0x4
#define TIMER_REG_TARGET  0x8

// IRQ numbers
#define TIMER0_IRQ 4
#define TIMER1_IRQ 5
#define TIMER2_IRQ 6

// Clock rates (Hz)
#define PSX_CPU_HZ      33868800
#define DOTCLOCK_HZ     53222400  // GPU dotclock
#define HBLANK_HZ       15734     // Horizontal blank rate

// Hardware timing
#define TIMER_RESET_DELAY_CYCLES 2  // Cycles to stay at 0 after reset
#define TIMER_WRITE_DELAY_CYCLES 2  // Cycles to stay after write

// Inline helper functions
static inline uint32_t timer_mode_to_bits(const TimerMode* mode);
static inline TimerMode timer_mode_from_bits(uint32_t bits);
static inline bool timer_is_irq_enabled(const TimerState* timer);
```

### `timer_core.h` (250 lines)

#### Public API (50 functions, ~5 lines each)
```c
// Lifecycle
void timers_init(TimersState* timers, struct Interconnect* inter);
void timers_reset(TimersState* timers);
void timers_shutdown(TimersState* timers);

// Register I/O
uint32_t timers_read_register(TimersState* timers, uint32_t offset);
void timers_write_register(TimersState* timers, struct Interconnect* inter, 
                           uint32_t offset, uint32_t value);

// Per-timer access
uint32_t timer_read_counter(TimersState* timers, uint32_t timer_index);
uint32_t timer_read_mode(TimersState* timers, uint32_t timer_index);
uint32_t timer_read_target(TimersState* timers, uint32_t timer_index);
void timer_write_counter(TimersState* timers, uint32_t timer_index, uint32_t value);
void timer_write_mode(TimersState* timers, struct Interconnect* inter, 
                     uint32_t timer_index, uint32_t value);
void timer_write_target(TimersState* timers, uint32_t timer_index, uint32_t value);

// Cycle stepping
void timers_add_sysclk_ticks(TimersState* timers, struct Interconnect* inter, 
                             uint32_t sysclk_ticks);
void timer_add_ticks(TimersState* timers, struct Interconnect* inter, 
                    uint32_t timer_index, uint32_t ticks);

// Gate control (from GPU)
void timer_set_gate(TimersState* timers, uint32_t timer_index, bool state);

// Query functions
bool timer_is_using_external_clock(const TimersState* timers, uint32_t timer_index);
bool timer_is_sync_enabled(const TimersState* timers, uint32_t timer_index);
bool timer_is_external_irq_enabled(const TimersState* timers, uint32_t timer_index);
int32_t timer_get_ticks_until_irq(const TimersState* timers, uint32_t timer_index);
```

### `timer_core.c` (800 lines)

#### Internal Functions (400 lines)
```c
// Mode management
static void update_counting_enabled(TimerState* timer, uint32_t timer_index);
static void check_for_irq(TimersState* timers, struct Interconnect* inter,
                         uint32_t timer_index, uint32_t old_counter);

// IRQ handling  
static void trigger_timer_irq(TimersState* timers, struct Interconnect* inter,
                              uint32_t timer_index);
static void update_irq_state(TimersState* timers, struct Interconnect* inter,
                            uint32_t timer_index);

// Clock management
static uint32_t get_timer_clock_divider(const TimerState* timer, uint32_t timer_index);
static bool should_count_this_cycle(const TimerState* timer, bool gate_active);

// Sync mode handling
static void handle_gate_transition(TimersState* timers, TimerState* timer,
                                   uint32_t timer_index, bool new_gate_state);
```

#### Register Implementation (200 lines)
- Read register: Decode offset to timer/register
- Write register: Update state, check IRQs
- Counter read: Return current value
- Mode read: Return mode bits with sticky flags
- Target read: Return target value
- Counter write: Set value, apply 2-cycle delay
- Mode write: Parse bits, update internal state, clear sticky flags
- Target write: Set value, check for immediate IRQ

#### Stepping Implementation (200 lines)
- Add sysclk ticks: Distribute to all timers
- Add timer ticks: Increment counter, handle wraps, check IRQs
- Handle overflow: Wrap to 0 or target
- Handle target match: Set flag, optionally reset, trigger IRQ
- Clock division: Handle div8 for timer 2

## Integration Points

### Interconnect
```c
// In interconnect.h
#include "timers/timer_core.h"

struct Interconnect {
    // ... other fields ...
    TimersState timers_state;
};

// In interconnect.c - read handler
uint32_t interconnect_read32(struct Interconnect* inter, uint32_t addr) {
    if (addr >= 0x1F801100 && addr < 0x1F801130) {
        return timers_read_register(&inter->timers_state, addr & 0x3F);
    }
}

// In interconnect.c - write handler
void interconnect_store32(struct Interconnect* inter, uint32_t addr, uint32_t value) {
    if (addr >= 0x1F801100 && addr < 0x1F801130) {
        timers_write_register(&inter->timers_state, inter, addr & 0x3F, value);
        return;
    }
}
```

### CPU Cycle Distribution
```c
// In cpu_core.c main loop
void cpu_run_next_instruction(Cpu* cpu) {
    // ... execute instruction ...
    uint32_t cycles = get_instruction_cycles();
    
    // Distribute cycles to timers
    timers_add_sysclk_ticks(&cpu->inter->timers_state, cpu->inter, cycles);
}
```

### GPU Integration
```c
// In gpu.c
void gpu_update_hblank(Gpu* gpu, bool active) {
    // Notify timer 0 of dotclock ticks
    timer_add_ticks(&gpu->inter->timers_state, gpu->inter, 0, dotclock_ticks);
    
    // Notify timer 1 of hblank edge
    timer_set_gate(&gpu->inter->timers_state, 0, active); // Timer 0 hblank gate
}

void gpu_update_vblank(Gpu* gpu, bool active) {
    timer_set_gate(&gpu->inter->timers_state, 1, active); // Timer 1 vblank gate
}
```

## Hardware Accuracy Details

### Counter Behavior
1. **After Mode Write**: Counter stays at current value for 2 cycles, then resumes
2. **After Counter Write**: Written value held for 2 cycles
3. **On Target Match**: Counter stays at 0 for 2 cycles (if reset_at_target)
4. **On Overflow**: Counter stays at 0 for 1 cycle

### IRQ Behavior
1. **Pulse Mode**: Bit 10 goes 0 for a few cycles, then back to 1
2. **Toggle Mode**: Bit 10 inverts on each IRQ condition
3. **One-shot**: First IRQ fires, subsequent conditions ignored until mode write
4. **Repeat**: Every condition triggers IRQ

### Sync Modes
**Timer 0/1 (Hblank/Vblank):**
- Mode 0: Pause during gate
- Mode 1: Reset at gate end
- Mode 2: Reset and run only during gate
- Mode 3: Pause until first gate, then free-run

**Timer 2:**
- Mode 0/3: Stop forever
- Mode 1/2: Free run (ignore sync)

## Complexity Analysis

| Operation | Complexity | Reason |
|-----------|------------|--------|
| Register Read | O(1) | Direct memory access |
| Register Write | O(1) | Direct update + flag check |
| Add Ticks | O(1) | Arithmetic + comparison |
| Check IRQ | O(1) | Condition evaluation |
| Gate Update | O(1) | State transition |
| Mode Update | O(1) | Bitfield parsing |

**Total Module**: All operations O(1)

## Testing Strategy

### Unit Tests
1. Counter incrementing at correct rates
2. Target match detection and IRQ
3. Overflow detection and IRQ
4. Mode bit parsing
5. Sync mode behavior
6. Clock source selection
7. Gate signal handling

### Integration Tests
1. IRQ delivery to CPU
2. GPU gate signal timing
3. BIOS timer usage
4. Multiple timers running simultaneously

## Migration Path

1. ✅ Backup old files (`timers.h` → `timers.h.backup`, `timers.c` → `timers.c.backup`)
2. ✅ Create new directory structure
3. ✅ Implement `timer_types.h`
4. ✅ Implement `timer_core.h`
5. ✅ Implement `timer_core.c`
6. ✅ Update Makefile
7. ✅ Update interconnect integration
8. ✅ Update CPU cycle distribution
9. ✅ Update GPU gate signals
10. ✅ Compile and verify
11. ✅ Run integration tests

## Success Criteria

- ✅ Compilation successful
- ✅ All register reads/writes working
- ✅ Timer interrupts triggering correctly
- ✅ BIOS boot uses timers (visible in logs)
- ✅ No timing regressions
- ✅ Thread-safe operation
- ✅ O(1) complexity maintained
- ✅ Hardware-accurate behavior

## References

- **DOCS/timers.md**: Hardware specification
- **DuckStation/src/core/timers.cpp**: Reference implementation
- **No$PSX Specs**: Detailed timer behavior
- **Mednafen**: Alternative implementation
