# Component Comparison: Event System

## 🔍 **EVENT SYSTEM COMPARISON**

### **Your Event System: COMPLETELY MISSING**

#### **What You Have:**
- ❌ **No event system files** - `psxevents.h` and `psxevents.c` don't exist
- ❌ **No event coordination** - Components can't schedule events
- ❌ **No timing coordination** - No central timing management
- ❌ **No interrupt scheduling** - No way to schedule future interrupts

#### **What PCSX ReARMed Has:**
```c
// psxevents.h - Complete event system
enum {
    PSXINT_SIO = 0,      // sioInterrupt
    PSXINT_CDR,          // cdrInterrupt
    PSXINT_CDREAD,       // cdrPlayReadInterrupt
    PSXINT_GPUDMA,       // gpuInterrupt
    PSXINT_MDECOUTDMA,   // mdec1Interrupt
    PSXINT_SPUDMA,       // spuInterrupt
    PSXINT_SPU_IRQ,      // spuDelayedIrq
    PSXINT_MDECINDMA,    // mdec0Interrupt
    PSXINT_GPUOTCDMA,    // gpuotcInterrupt
    PSXINT_CDRDMA,       // cdrDmaInterrupt
    PSXINT_NEWDRC_CHECK, // (none)
    PSXINT_RCNT,         // psxRcntUpdate
    PSXINT_CDRLID,       // cdrLidSeekInterrupt
    PSXINT_IRQ10,        // irq10Interrupt
    PSXINT_SPU_UPDATE,   // spuUpdate
    PSXINT_COUNT
};
```

---

## ❌ **MISSING FROM YOUR EVENT SYSTEM**

### **1. Complete Event System Files**

#### **Missing File 1: `psxevents.h`**
```c
// PCSX ReARMed has this complete header
#ifndef __PSXEVENTS_H__
#define __PSXEVENTS_H__

#include "psxcommon.h"

enum {
    PSXINT_SIO = 0,      // sioInterrupt
    PSXINT_CDR,          // cdrInterrupt
    PSXINT_CDREAD,       // cdrPlayReadInterrupt
    PSXINT_GPUDMA,       // gpuInterrupt
    PSXINT_MDECOUTDMA,   // mdec1Interrupt
    PSXINT_SPUDMA,       // spuInterrupt
    PSXINT_SPU_IRQ,      // spuDelayedIrq
    PSXINT_MDECINDMA,    // mdec0Interrupt
    PSXINT_GPUOTCDMA,    // gpuotcInterrupt
    PSXINT_CDRDMA,       // cdrDmaInterrupt
    PSXINT_NEWDRC_CHECK, // (none)
    PSXINT_RCNT,         // psxRcntUpdate
    PSXINT_CDRLID,       // cdrLidSeekInterrupt
    PSXINT_IRQ10,        // irq10Interrupt
    PSXINT_SPU_UPDATE,   // spuUpdate
    PSXINT_COUNT
};

#define set_event_raw_abs(e, abs) { \
    u32 abs_ = abs; \
    s32 di_ = psxRegs.next_interupt - abs_; \
    psxRegs.event_cycles[e] = abs_; \
    if (di_ > 0) { \
        psxRegs.next_interupt = abs_; \
    } \
}

#define set_event(e, c) do { \
    psxRegs.interrupt |= (1 << (e)); \
    psxRegs.intCycle[e].cycle = c; \
    psxRegs.intCycle[e].sCycle = psxRegs.cycle; \
    set_event_raw_abs(e, psxRegs.cycle + (c)) \
} while (0)

u32  schedule_timeslice(struct psxRegisters *regs);
void irq_test(union psxCP0Regs_ *cp0);
void gen_interupt(union psxCP0Regs_ *cp0);
void events_restore(void);

#endif
```

#### **Missing File 2: `psxevents.c`**
```c
// PCSX ReARMed has this complete implementation
#include "psxevents.h"
#include "r3000a.h"
#include "cdrom.h"
#include "psxdma.h"
#include "mdec.h"

// Event scheduling and timing coordination
u32 schedule_timeslice(psxRegisters *regs);
void irq_test(psxCP0Regs *cp0);
void gen_interupt(psxCP0Regs *cp0);
void events_restore(void);
```

### **2. Critical Missing Functions**

#### **Missing Function 1: `set_event()` (Event Scheduling)**
```c
// PCSX ReARMed has this macro
#define set_event(e, c) do { \
    psxRegs.interrupt |= (1 << (e)); \
    psxRegs.intCycle[e].cycle = c; \
    psxRegs.intCycle[e].sCycle = psxRegs.cycle; \
    set_event_raw_abs(e, psxRegs.cycle + (c)) \
} while (0)

// You're missing event scheduling entirely
// Need to add: void interconnect_schedule_event(Interconnect* inter, enum psxEvent event, uint32_t cycles);
```

#### **Missing Function 2: `schedule_timeslice()` (Timing Coordination)**
```c
// PCSX ReARMed has this
u32 schedule_timeslice(psxRegisters *regs) {
    u32 i, c = regs->cycle;
    u32 irqs = regs->interrupt;
    s32 min, dif;

    min = PSXCLK;
    for (i = 0; irqs != 0; i++, irqs >>= 1) {
        if (!(irqs & 1))
            continue;
        dif = regs->event_cycles[i] - c;
        if (0 < dif && dif < min)
            min = dif;
    }
    regs->next_interupt = c + min;
    return regs->next_interupt;
}

// You're missing timing coordination entirely
// Need to add: uint32_t interconnect_schedule_timeslice(Interconnect* inter);
```

#### **Missing Function 3: `irq_test()` (Interrupt Testing)**
```c
// PCSX ReARMed has this
void irq_test(psxCP0Regs *cp0) {
    psxRegisters *regs = cp0TOpsxRegs(cp0);
    u32 cycle = regs->cycle;
    u32 irq, irq_bits;

    for (irq = 0, irq_bits = regs->interrupt; irq_bits != 0; irq++, irq_bits >>= 1) {
        if (!(irq_bits & 1))
            continue;
        if ((s32)(cycle - regs->event_cycles[irq]) >= 0) {
            regs->interrupt &= ~(1u << irq);
            irq_funcs[irq]();
        }
    }
}

// You're missing interrupt testing entirely
// Need to add: void interconnect_test_interrupts(Interconnect* inter);
```

#### **Missing Function 4: `gen_interupt()` (Interrupt Generation)**
```c
// PCSX ReARMed has this
void gen_interupt(psxCP0Regs *cp0) {
    psxRegisters *regs = cp0TOpsxRegs(cp0);
    irq_test(cp0);
    schedule_timeslice(regs);
}

// You're missing interrupt generation entirely
// Need to add: void interconnect_generate_interrupts(Interconnect* inter);
```

### **3. Missing Data Structures**

#### **Missing Event Data in CPU/System:**
```c
// PCSX ReARMed has these in psxRegisters
typedef struct psxRegisters {
    // ... other fields
    u32 interrupt;           // Event interrupt flags
    u32 event_cycles[20];    // Cycle when each event occurs
    u32 next_interupt;       // Next interrupt cycle
    struct {
        u32 cycle;
        u32 sCycle;
    } intCycle[20];          // Event cycle tracking
} psxRegisters;

// You're missing event data structures entirely
// Need to add to your Interconnect struct:
// uint32_t event_interrupts;
// uint32_t event_cycles[PSXINT_COUNT];
// uint32_t next_interrupt;
```

#### **Missing Event Function Table:**
```c
// PCSX ReARMed has this
typedef void (irq_func)();

static irq_func * const irq_funcs[] = {
    [PSXINT_SIO]        = sioInterrupt,
    [PSXINT_CDR]        = cdrInterrupt,
    [PSXINT_CDREAD]     = cdrPlayReadInterrupt,
    [PSXINT_GPUDMA]     = gpuInterrupt,
    [PSXINT_MDECOUTDMA] = mdec1Interrupt,
    [PSXINT_SPUDMA]     = spuInterrupt,
    [PSXINT_MDECINDMA]  = mdec0Interrupt,
    [PSXINT_GPUOTCDMA]  = gpuotcInterrupt,
    [PSXINT_CDRDMA]     = cdrDmaInterrupt,
    [PSXINT_NEWDRC_CHECK] = irqNoOp,
    [PSXINT_CDRLID]     = cdrLidSeekInterrupt,
    [PSXINT_IRQ10]      = irq10Interrupt,
    [PSXINT_SPU_UPDATE] = spuUpdate,
    [PSXINT_SPU_IRQ]    = spuDelayedIrq,
    [PSXINT_RCNT]       = psxRcntUpdate,
};

// You're missing event function table entirely
// Need to add: Function pointer array for event handlers
```

### **4. Missing Integration with CPU**

#### **Missing CPU Integration:**
```c
// PCSX ReARMed calls these from CPU
irq_test(&psxRegs.CP0);
gen_interupt(&psxRegs.CP0);
schedule_timeslice(&psxRegs);

// You're missing CPU integration entirely
// Need to add calls to event system from your CPU main loop
```

---

## 🔧 **WHAT YOU NEED TO ADD**

### **Step 1: Create Event System Files**

#### **Create `include/psxevents.h`:**
```c
#ifndef __PSXEVENTS_H__
#define __PSXEVENTS_H__

#include <stdint.h>

// Event types
enum psxEvent {
    PSXINT_SIO = 0,      // sioInterrupt
    PSXINT_CDR,          // cdrInterrupt
    PSXINT_CDREAD,       // cdrPlayReadInterrupt
    PSXINT_GPUDMA,       // gpuInterrupt
    PSXINT_MDECOUTDMA,   // mdec1Interrupt
    PSXINT_SPUDMA,       // spuInterrupt
    PSXINT_SPU_IRQ,      // spuDelayedIrq
    PSXINT_MDECINDMA,    // mdec0Interrupt
    PSXINT_GPUOTCDMA,    // gpuotcInterrupt
    PSXINT_CDRDMA,       // cdrDmaInterrupt
    PSXINT_NEWDRC_CHECK, // (none)
    PSXINT_RCNT,         // psxRcntUpdate
    PSXINT_CDRLID,       // cdrLidSeekInterrupt
    PSXINT_IRQ10,        // irq10Interrupt
    PSXINT_SPU_UPDATE,   // spuUpdate
    PSXINT_COUNT
};

// Event system functions
void interconnect_schedule_event(struct Interconnect* inter, enum psxEvent event, uint32_t cycles);
void interconnect_test_interrupts(struct Interconnect* inter);
void interconnect_generate_interrupts(struct Interconnect* inter);
uint32_t interconnect_schedule_timeslice(struct Interconnect* inter);

#endif
```

#### **Create `src/psxevents.c`:**
```c
#include "psxevents.h"
#include "interconnect.h"
#include "cpu.h"
#include "timers.h"
#include "gpu.h"
#include "cdrom.h"

// Event function table
typedef void (*event_handler_func)(struct Interconnect*);

static event_handler_func event_handlers[PSXINT_COUNT] = {
    [PSXINT_SIO]        = NULL,  // TODO: Add SIO
    [PSXINT_CDR]        = NULL,  // TODO: Add CDROM
    [PSXINT_CDREAD]     = NULL,  // TODO: Add CDROM
    [PSXINT_GPUDMA]     = NULL,  // TODO: Add GPU DMA
    [PSXINT_MDECOUTDMA] = NULL,  // TODO: Add MDEC
    [PSXINT_SPUDMA]     = NULL,  // TODO: Add SPU
    [PSXINT_SPU_IRQ]    = NULL,  // TODO: Add SPU
    [PSXINT_MDECINDMA]  = NULL,  // TODO: Add MDEC
    [PSXINT_GPUOTCDMA]  = NULL,  // TODO: Add GPU OTC DMA
    [PSXINT_CDRDMA]     = NULL,  // TODO: Add CDROM DMA
    [PSXINT_NEWDRC_CHECK] = NULL,  // No-op
    [PSXINT_RCNT]       = timers_update,  // Timer update
    [PSXINT_CDRLID]     = NULL,  // TODO: Add CDROM
    [PSXINT_IRQ10]      = NULL,  // TODO: Add IRQ10
    [PSXINT_SPU_UPDATE] = NULL,  // TODO: Add SPU
};

// Event scheduling
void interconnect_schedule_event(struct Interconnect* inter, enum psxEvent event, uint32_t cycles) {
    if (event >= PSXINT_COUNT) return;
    
    inter->event_interrupts |= (1 << event);
    inter->event_cycles[event] = inter->cpu_cycle + cycles;
    
    // Update next interrupt if this one is sooner
    if (inter->next_interrupt > inter->event_cycles[event]) {
        inter->next_interrupt = inter->event_cycles[event];
    }
}

// Interrupt testing
void interconnect_test_interrupts(struct Interconnect* inter) {
    uint32_t current_cycle = inter->cpu_cycle;
    uint32_t irq_bits = inter->event_interrupts;
    
    for (int event = 0; irq_bits != 0; event++, irq_bits >>= 1) {
        if (!(irq_bits & 1)) continue;
        
        if ((int32_t)(current_cycle - inter->event_cycles[event]) >= 0) {
            // Event has occurred
            inter->event_interrupts &= ~(1u << event);
            
            // Call event handler
            if (event_handlers[event]) {
                event_handlers[event](inter);
            }
        }
    }
}

// Interrupt generation
void interconnect_generate_interrupts(struct Interconnect* inter) {
    interconnect_test_interrupts(inter);
    interconnect_schedule_timeslice(inter);
}

// Timeslice scheduling
uint32_t interconnect_schedule_timeslice(struct Interconnect* inter) {
    uint32_t current_cycle = inter->cpu_cycle;
    uint32_t irq_bits = inter->event_interrupts;
    int32_t min_diff = 0x7FFFFFFF;
    
    for (int event = 0; irq_bits != 0; event++, irq_bits >>= 1) {
        if (!(irq_bits & 1)) continue;
        
        int32_t diff = inter->event_cycles[event] - current_cycle;
        if (diff > 0 && diff < min_diff) {
            min_diff = diff;
        }
    }
    
    inter->next_interrupt = current_cycle + min_diff;
    return inter->next_interrupt;
}
```

### **Step 2: Add Event Data to Interconnect**

#### **Update `include/interconnect.h`:**
```c
// Add to your Interconnect struct
typedef struct Interconnect {
    // ... existing fields
    
    // Event system data
    uint32_t event_interrupts;           // Event interrupt flags
    uint32_t event_cycles[PSXINT_COUNT]; // Cycle when each event occurs
    uint32_t next_interrupt;             // Next interrupt cycle
    uint32_t cpu_cycle;                  // Current CPU cycle count
    
    // ... existing fields
} Interconnect;
```

### **Step 3: Integrate with CPU Main Loop**

#### **Update your CPU main loop:**
```c
// In your CPU main loop, add:
void cpu_step(Cpu* cpu, Interconnect* inter) {
    // ... existing CPU execution
    
    // Update cycle count
    inter->cpu_cycle += cycles_executed;
    
    // Check for events
    if (inter->cpu_cycle >= inter->next_interrupt) {
        interconnect_generate_interrupts(inter);
    }
}
```

---

## 🎯 **IMPLEMENTATION PRIORITY**

### **CRITICAL (Blocking Boot)**
1. **Create event system files** - Essential for timing coordination
2. **Add event data to Interconnect** - Essential for event storage
3. **Integrate with CPU main loop** - Essential for event processing
4. **Add Timer0 event scheduling** - Essential for VBlank IRQ0

### **HIGH PRIORITY**
5. **Add event handlers for existing components** - Timer, GPU, CDROM
6. **Add missing event types** - For future components
7. **Add event restoration** - For save states

### **MEDIUM PRIORITY**
8. **Add event debugging** - For development
9. **Add event optimization** - For performance
10. **Add event validation** - For stability

---

## 📋 **NEXT STEPS**

1. **Create `include/psxevents.h`** with event definitions
2. **Create `src/psxevents.c`** with event implementation
3. **Add event data to Interconnect struct**
4. **Integrate event system with CPU main loop**
5. **Add Timer0 event scheduling** - This is critical for VBlank IRQ0

**The Event System is the most critical missing piece! Without it, your timers, GPU, and other components can't coordinate timing properly. This is why your BIOS isn't booting correctly.**

Would you like me to help you implement the Event System, or should we move on to the next component (DMA)? 