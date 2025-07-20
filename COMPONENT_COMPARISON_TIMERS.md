# Component Comparison: Timer System

## ✅ STATUS: Timer0/VBlank IRQ0 logic CONFIRMED CORRECT (via GDB)

After a thorough GDB session, your Timer0 and VBlank IRQ0 logic now matches PCSX ReARMed:
- Timer0 is configured for VBlank IRQ0 (mode=0x0110, target=0xFFFF).
- Timer0 counts up, requests IRQ0 at the right time, and the BIOS acknowledges it.
- The IRQ0 request/acknowledge cycle is clean and matches the reference.
- The BIOS is not stuck due to timer or IRQ issues.

**Next areas to investigate:** GPU emulation, CPU exception/return logic, or other hardware subsystems.

---

## 🔍 **TIMER SYSTEM COMPARISON**

### **Your Timer Structure vs PCSX ReARMed**

#### **Your Timer Structure:**
```c
typedef struct {
    uint16_t counter; // Current 16-bit counter value
    uint16_t mode;    // 16-bit mode register value
    uint16_t target;  // 16-bit target value
    
    // Internal emulation state
    bool sync_enable;
    uint8_t sync_mode;
    bool reset_on_target;
    bool irq_on_target;
    bool irq_on_ffff;
    bool irq_repeat;
    bool irq_pulse;
    uint8_t clock_source;
    
    bool interrupt_requested;
    bool reached_target_flag;
    bool reached_ffff_flag;
} Timer;
```

#### **PCSX ReARMed Timer Structure:**
```c
typedef struct Rcnt {
    u16 mode, target;
    u32 rate, irq, counterState, irqState;  // ← MISSING FIELDS
    u32 cycle, cycleStart;                  // ← MISSING FIELDS
} Rcnt;
```

---

## ❌ **MISSING FROM YOUR TIMER SYSTEM**

### **1. Critical Missing Fields in Timer Struct**

#### **Missing Field 1: `rate` (Clock Rate)**
```c
// PCSX ReARMed has this
u32 rate;  // Clock rate for this timer

// Your Timer struct is missing this
// Add to your Timer struct:
uint32_t rate;  // Clock rate (1, 5, 8, or lineCycles())
```

#### **Missing Field 2: `irq` (Interrupt Line)**
```c
// PCSX ReARMed has this
u32 irq;  // Which interrupt line this timer uses

// Your Timer struct is missing this
// Add to your Timer struct:
uint32_t irq;  // IRQ line (4 for Timer0, 5 for Timer1, 6 for Timer2)
```

#### **Missing Field 3: `counterState` (Count Direction)**
```c
// PCSX ReARMed has this
u32 counterState;  // CountToTarget or CountToOverflow

// Your Timer struct is missing this
// Add to your Timer struct:
uint32_t counter_state;  // Whether counting to target or overflow
```

#### **Missing Field 4: `irqState` (IRQ State)**
```c
// PCSX ReARMed has this
u32 irqState;  // Current IRQ state

// Your Timer struct is missing this
// Add to your Timer struct:
uint32_t irq_state;  // Current IRQ state (0 or 1)
```

#### **Missing Field 5: `cycle` and `cycleStart` (Cycle Counting)**
```c
// PCSX ReARMed has these
u32 cycle, cycleStart;  // Cycle counting for precise timing

// Your Timer struct is missing these
// Add to your Timer struct:
uint32_t cycle;      // Current cycle count
uint32_t cycle_start; // Cycle when timer was started
```

### **2. Missing Constants and Enums**

#### **Missing Mode Register Constants:**
```c
// PCSX ReARMed has these detailed constants
enum {
    RcSyncModeEnable  = 0x0001, // 0
    Rc01BlankPause    = 0 << 1, // 1,2
    Rc01UnblankReset  = 1 << 1, // 1,2
    Rc01UnblankReset2 = 2 << 1, // 1,2
    Rc2Stop           = 0 << 1, // 1,2
    Rc2Stop2          = 3 << 1, // 1,2
    RcCountToTarget   = 0x0008, // 3
    RcIrqOnTarget     = 0x0010, // 4
    RcIrqOnOverflow   = 0x0020, // 5
    RcIrqRegenerate   = 0x0040, // 6
    RcUnknown7        = 0x0080, // 7
    Rc0PixelClock     = 0x0100, // 8
    Rc1HSyncClock     = 0x0100, // 8
    Rc2OneEighthClock = 0x0200, // 9
    RcUnknown10       = 0x0400, // 10
    RcCountEqTarget   = 0x0800, // 11
    RcOverflow        = 0x1000, // 12
};

// Add these to your timers.h
```

#### **Missing Counter State Constants:**
```c
// PCSX ReARMed has these
static const u32 CountToOverflow  = 0;
static const u32 CountToTarget    = 1;

// Add these to your timers.h
#define TIMER_COUNT_TO_OVERFLOW  0
#define TIMER_COUNT_TO_TARGET    1
```

### **3. Missing Functions**

#### **Missing Function 1: `psxRcntUpdate()` (Main Update Function)**
```c
// PCSX ReARMed has this critical function
void psxRcntUpdate() {
    // Main timer update logic
    // Called every cycle to update timers
}

// Your timers_step() function exists but is incomplete
// Need to enhance it with proper cycle counting
```

#### **Missing Function 2: `scheduleRcntBase()` (Event Scheduling)**
```c
// PCSX ReARMed has this
static void scheduleRcntBase(void) {
    // Schedule next timer event
    // This is critical for proper timing
}

// You're missing event scheduling entirely
```

#### **Missing Function 3: `setIrq()` (Interrupt Setting)**
```c
// PCSX ReARMed has this
static inline void setIrq(u32 irq) {
    psxHu32ref(0x1070) |= SWAPu32(irq);
}

// You have interconnect_request_irq() but it's not working properly
```

#### **Missing Function 4: `frameCycles()` (Frame Timing)**
```c
// PCSX ReARMed has this
static inline u32 frameCycles(void) {
    // Calculate cycles per frame
    return Config.PsxType ? (PSXCLK / 50) : (PSXCLK / 60);
}

// You're missing proper frame timing calculation
```

#### **Missing Function 5: `lineCycles()` (Line Timing)**
```c
// PCSX ReARMed has this
static inline u32 lineCycles(void) {
    // Calculate cycles per scanline
    if (Config.PsxType)
        return PSXCLK / 50 / HSyncTotal[1];
    else
        return PSXCLK / 60 / HSyncTotal[0];
}

// You're missing scanline timing calculation
```

### **4. Missing Global Variables**

#### **Missing Global Variables:**
```c
// PCSX ReARMed has these
extern unsigned int hSyncCount, frame_counter;
extern Rcnt rcnts[];

// You're missing global timing variables
// Add to your timers.h:
extern uint32_t h_sync_count;
extern uint32_t frame_counter;
```

---

## 🔧 **WHAT YOU NEED TO ADD**

### **Step 1: Update Your Timer Struct**
```c
// Add these fields to your Timer struct in include/timers.h
typedef struct {
    uint16_t counter;
    uint16_t mode;
    uint16_t target;
    
    // ← ADD THESE MISSING FIELDS
    uint32_t rate;         // Clock rate for this timer
    uint32_t irq;          // IRQ line (4, 5, or 6)
    uint32_t counter_state; // CountToTarget or CountToOverflow
    uint32_t irq_state;    // Current IRQ state
    uint32_t cycle;        // Current cycle count
    uint32_t cycle_start;  // Cycle when timer was started
    
    // ... existing fields
} Timer;
```

### **Step 2: Add Missing Constants**
```c
// Add to include/timers.h
#define TIMER_COUNT_TO_OVERFLOW  0
#define TIMER_COUNT_TO_TARGET    1

// Timer IRQ lines
#define TIMER0_IRQ  4
#define TIMER1_IRQ  5
#define TIMER2_IRQ  6

// Clock rates
#define TIMER_RATE_1     1
#define TIMER_RATE_5     5
#define TIMER_RATE_8     8
```

### **Step 3: Add Missing Functions**
```c
// Add to include/timers.h
void timers_update(Timers* timers);  // Main update function
void timers_schedule_next(Timers* timers);  // Schedule next event
uint32_t timers_calculate_frame_cycles(void);  // Frame timing
uint32_t timers_calculate_line_cycles(void);   // Line timing
```

### **Step 4: Add Global Variables**
```c
// Add to include/timers.h
extern uint32_t h_sync_count;
extern uint32_t frame_counter;
```

---

## 🎯 **IMPLEMENTATION PRIORITY**

### **[RESOLVED for Timer0/IRQ0]**
1. **Add `cycle` and `cycle_start` fields** - Essential for precise timing
2. **Add `rate` field** - Essential for proper clock rates
3. **Add `irq` field** - Essential for interrupt generation
4. **Enhance `timers_step()` function** - Add proper cycle counting

### **HIGH PRIORITY**
5. **Add `counter_state` and `irq_state` fields** - For proper state tracking
6. **Add missing constants** - For proper mode handling
7. **Add `timers_update()` function** - Main update logic

### **MEDIUM PRIORITY**
8. **Add frame/line timing functions** - For proper synchronization
9. **Add global timing variables** - For system-wide timing

---

## 📋 **NEXT STEPS**

- Timer0/IRQ0 logic is now correct. Move on to GPU, CPU exception/return, or other hardware for further debugging. 