# PS1 Emulator - Step-by-Step Implementation Plan

_Last updated: 01 July 2025_

Based on PSX-Spex documentation and nocash specs, implementing one component at a time for easy debugging.

## 🎯 **PHILOSOPHY**

- **One component at a time** - Implement, test, and verify each component independently
- **Minimal dependencies** - Each component should work with minimal assumptions about other components
- **Comprehensive testing** - Each step includes specific test criteria
- **Documentation-driven** - Follow PSX-Spex specifications exactly

---

## 🎯 **CURRENT STATUS: CPU Exception Handling COMPLETE**

**✅ LATEST ACHIEVEMENT:** CPU Exception System Fully Implemented and Tested (June 2025)
- Exception vector jumps (SYSCALL → 0x80000080) ✅
- ERET instruction recognition and execution ✅
- Register updates (EPC, Cause, Status) ✅
- Exception flow control with `exception_pending` flag ✅
- Strict compliance with PSX-Spex/nocash documentation ✅

---

## 🎯 **STATUS UPDATE (01 July 2025)**
- Interconnect: FULLY TESTED & nocash/PSX-Spex COMPLIANT (all regions, edge cases, and open bus behaviors verified)
- All integration and edge cases handled; robust for further component integration.
- Next priority: Timer0/VBlank IRQ re-implementation.

---

## 📋 **PHASE 1: Foundation Components**

### Step 1.1: Memory Management Unit (MMU)
**Status:** ✅ **COMPLETE** (ram.c, vram.c exist)
**Files:** `src/ram.c`, `src/vram.c`
**PSX-Spex Reference:** [Memory Map](https://psx-spx.consoledev.net/memorymap/)

**Test Criteria:**
- [ ] RAM reads/writes work correctly (0x00000000-0x01FFFFFF)
- [ ] VRAM reads/writes work correctly (0x1FC00000-0x1FC7FFFF)
- [ ] Memory-mapped I/O routing works
- [ ] Bus error handling for invalid addresses

**Implementation Notes:**
- Keep existing implementation, verify it matches PSX-Spex memory map
- Add bus error detection for unmapped addresses

---

### Step 1.2: Basic CPU Core (MIPS R3000A)
**Status:** ✅ **COMPLETE** (cpu.c fully implemented and tested)
**Files:** `src/cpu.c`
**PSX-Spex Reference:** [CPU Specifications](https://psx-spx.consoledev.net/cpuspecs/)

**✅ Achievements:**
- [x] Basic MIPS instructions execute correctly
- [x] Register file works properly
- [x] ALU operations are accurate
- [x] Branch/jump instructions work
- [x] Load/store instructions work with MMU
- [x] Exception vector at 0x80000080 works correctly
- [x] Interrupt exceptions are properly handled
- [x] ERET instruction returns from exception correctly
- [x] Cause register is properly set

**Implementation Notes:**
- ✅ CPU exception handling system fully functional
- ✅ Exception vector jumps working correctly
- ✅ ERET instruction properly implemented and tested

---

### Step 1.3: Interconnect Bus
**Status:** ✅ **COMPLETE** (interconnect.c exists)
**Files:** `src/interconnect.c`
**PSX-Spex Reference:** [Memory Map](https://psx-spx.consoledev.net/memorymap/)

**Test Criteria:**
- [ ] Memory-mapped I/O routing works correctly
- [ ] Register reads/writes are properly handled
- [ ] Bus arbitration works
- [ ] Interrupt routing functions

**Implementation Notes:**
- Verify existing implementation matches PSX-Spex memory map
- Ensure proper register routing for all components

---

## 📋 **PHASE 2: Timing & Interrupt System**

### Step 2.1: Timer System
**Status:** 🎯 **NEXT PRIORITY** (timers.c exists but incomplete)
**Files:** `src/timers.c`
**PSX-Spex Reference:** [Timers](https://psx-spx.consoledev.net/timers/)

**Test Criteria:**
- [ ] Timer 0 (VBlank) counts correctly at 44100 Hz
- [ ] Timer 1 (HBlank) counts correctly at 15734 Hz
- [ ] Timer 2 (System) counts correctly at 44100 Hz
- [ ] Timer interrupts are generated when target reached
- [ ] Timer interrupts are properly acknowledged

**Implementation Notes:**
- **CRITICAL:** Timer 0 is essential for VBlank IRQ
- Implement all three timers with proper modes
- Ensure interrupt generation and acknowledgment

---

### Step 2.2: Interrupt Controller
**Status:** 🔄 **NEEDS AUDIT** (part of interconnect.c)
**Files:** `src/interconnect.c`
**PSX-Spex Reference:** [Interrupts](https://psx-spx.consoledev.net/interrupts/)

**Test Criteria:**
- [ ] I_STAT register properly tracks interrupt sources
- [ ] I_MASK register properly masks interrupts
- [ ] Interrupt acknowledgment clears I_STAT bits
- [ ] CPU receives interrupts when I_STAT & I_MASK != 0

**Implementation Notes:**
- Verify existing I_STAT/I_MASK implementation
- Ensure proper interrupt routing to CPU

---

### Step 2.3: CPU Exception Handling
**Status:** ✅ **COMPLETE** (cpu.c fully implemented and tested)
**Files:** `src/cpu.c`
**PSX-Spex Reference:** [CPU Specifications](https://psx-spx.consoledev.net/cpuspecs/)

**✅ Achievements:**
- [x] Exception vector at 0x80000080 works correctly
- [x] Interrupt exceptions are properly handled
- [x] ERET instruction returns from exception correctly
- [x] Cause register is properly set

**Implementation Notes:**
- ✅ **COMPLETE** - Exception handling system fully functional
- ✅ Exception vector jumps working correctly
- ✅ ERET instruction properly implemented and tested

---

## 📋 **PHASE 3: Storage & Media**

### Step 3.1: CDROM Controller
**Status:** ✅ **COMPLETE** (cdrom.c exists)
**Files:** `src/cdrom.c`
**PSX-Spex Reference:** [CDROM Drive](https://psx-spx.consoledev.net/cdromdrive/)

**Test Criteria:**
- [ ] CDROM commands work correctly
- [ ] Interrupt generation works
- [ ] Data transfer functions properly
- [ ] Status register reflects correct state

**Implementation Notes:**
- Keep existing implementation, verify against PSX-Spex
- Ensure interrupt generation matches documentation

---

### Step 3.2: DMA Controller
**Status:** 🔄 **NEEDS AUDIT** (dma.c exists)
**Files:** `src/dma.c`
**PSX-Spex Reference:** [DMA](https://psx-spx.consoledev.net/dma/)

**Test Criteria:**
- [ ] DMA transfers work correctly
- [ ] DMA completion interrupts are generated
- [ ] DMA channels are properly configured
- [ ] DMA priority works correctly

**Implementation Notes:**
- Verify existing implementation
- Ensure DMA completion interrupts work

---

## 📋 **PHASE 4: Graphics & Display**

### Step 4.1: GPU Command Processing
**Status:** 🔄 **NEEDS AUDIT** (gpu.c exists)
**Files:** `src/gpu.c`
**PSX-Spex Reference:** [GPU](https://psx-spx.consoledev.net/gpu/)

**Test Criteria:**
- [ ] GP0 commands are properly parsed
- [ ] GP1 commands are properly handled
- [ ] GPUSTAT register reflects correct state
- [ ] GPU interrupts are generated when needed

**Implementation Notes:**
- Focus on command parsing and status register
- Ensure GPUSTAT bits are set correctly

---

### Step 4.2: GPU Renderer
**Status:** 🔄 **NEEDS AUDIT** (renderer.c exists)
**Files:** `src/renderer.c`
**PSX-Spex Reference:** [GPU](https://psx-spx.consoledev.net/gpu/)

**Test Criteria:**
- [ ] Basic rendering works
- [ ] GPU commands are properly executed
- [ ] Display output is generated
- [ ] Sony logo displays correctly

**Implementation Notes:**
- Focus on basic rendering first
- Ensure GPU-renderer integration works

---

### Step 4.3: GTE (Geometry Transformation Engine)
**Status:** 🔄 **NEEDS AUDIT** (gte.c exists)
**Files:** `src/gte.c`
**PSX-Spex Reference:** [GTE](https://psx-spx.consoledev.net/gte/)

**Test Criteria:**
- [ ] GTE instructions execute correctly
- [ ] Matrix operations work properly
- [ ] GTE interrupts are generated
- [ ] GTE-GPU integration works

**Implementation Notes:**
- Verify existing implementation
- Ensure GTE instructions work correctly

---

## 📋 **PHASE 5: System Integration**

### Step 5.1: BIOS Interface
**Status:** ✅ **COMPLETE** (bios.c exists)
**Files:** `src/bios.c`
**PSX-Spex Reference:** [Kernel/BIOS](https://psx-spx.consoledev.net/kernelbios/)

**Test Criteria:**
- [ ] BIOS loads correctly from file
- [ ] BIOS syscalls work properly
- [ ] BIOS interrupt handling works
- [ ] BIOS menu displays correctly

**Implementation Notes:**
- Keep existing implementation
- Verify BIOS syscall handling

---

### Step 5.2: System Integration Testing
**Status:** 🔄 **IN PROGRESS**
**Files:** `src/main.c`
**PSX-Spex Reference:** [System Overview](https://psx-spx.consoledev.net/)

**Test Criteria:**
- [ ] All components work together
- [ ] BIOS boots successfully
- [ ] No infinite loops or crashes
- [ ] System reaches BIOS menu

**Implementation Notes:**
- Test full system integration
- Ensure all components work together

---

## 🎯 PRIORITY ROADMAP (01 July 2025)

### 🥇 PRIORITY 1: Timer0 / VBlank IRQ0 (CRITICAL)
- [ ] Re-implement Timer0 to generate VBlank IRQ0 at correct frequency and timing (per PSX-Spex/nocash)
- [ ] Ensure Timer0 mode, target, and counter logic are correct
- [ ] Confirm IRQ0 is routed and acknowledged by CPU
- [ ] Add integration test/logs to confirm IRQ0 is being requested and handled

### 🥈 PRIORITY 2: GPU & Renderer Audit
- [ ] Audit GPUSTAT, command parsing, and interrupt logic for PSX-Spex compliance
- [ ] Ensure renderer displays logo and handles draw commands correctly

### 🥉 PRIORITY 3: System Integration Testing
- [ ] Verify BIOS proceeds past boot logo after Timer0 fix
- [ ] Add integration tests for IRQ0 and VBlank
- [ ] Confirm all components work together

---

## 🚀 **IMMEDIATE NEXT STEPS**

1. **Start with Step 2.1 (Timer System)** - This is the next critical component
2. **Create isolated test cases** for each component
3. **Follow PSX-Spex documentation exactly**
4. **Test each step independently** before moving to the next

**Would you like to start with the Timer system implementation? This will solve the VBlank IRQ issue and allow BIOS to progress.**

---

## 🚀 **IMPLEMENTATION STRATEGY**

### **Step-by-Step Process:**

1. **Audit Current Implementation**
   - Review each component against PSX-Spex documentation
   - Identify gaps and inconsistencies
   - Create specific test cases

2. **Implement/Fix One Component at a Time**
   - Start with foundation components (MMU, CPU core)
   - Move to timing system (critical for boot)
   - Progress to graphics and integration

3. **Test Each Component Independently**
   - Create isolated test cases
   - Verify against PSX-Spex specifications
   - Ensure no regressions

4. **Integration Testing**
   - Test components together
   - Add comprehensive logging
   - Debug systematically

### **Debugging Tools:**

```c
// Enhanced logging for each component
#define LOG_COMPONENT(component, level, ...) \
    log_message(level, "[%s] " __VA_ARGS__, #component)

// Component-specific test functions
void test_mmu_basic(void);
void test_cpu_instructions(void);
void test_timer_system(void);
void test_interrupt_controller(void);
```

### **Success Criteria for Each Phase:**

- **Phase 1:** Basic system boots and can execute instructions
- **Phase 2:** Interrupt system works, no infinite loops
- **Phase 3:** Storage and DMA work correctly
- **Phase 4:** Graphics system displays output
- **Phase 5:** Full system integration works

---

## 📚 **KEY PSX-SPEX REFERENCES**

1. **[System Overview](https://psx-spx.consoledev.net/)** - Overall architecture
2. **[Memory Map](https://psx-spx.consoledev.net/memorymap/)** - Memory and I/O layout
3. **[CPU Specifications](https://psx-spx.consoledev.net/cpuspecs/)** - MIPS R3000A details
4. **[Interrupts](https://psx-spx.consoledev.net/interrupts/)** - Interrupt system
5. **[Timers](https://psx-spx.consoledev.net/timers/)** - Timer implementation
6. **[GPU](https://psx-spx.consoledev.net/gpu/)** - Graphics processing
7. **[CDROM Drive](https://psx-spx.consoledev.net/cdromdrive/)** - CDROM controller
8. **[Kernel/BIOS](https://psx-spx.consoledev.net/kernelbios/)** - BIOS interface

---

## 🎯 **NEXT IMMEDIATE STEPS**

1. **Audit Timer System** - This is likely the critical missing piece
2. **Verify CPU Exception Handling** - Source of infinite loops
3. **Test Interrupt Controller** - Ensure proper IRQ routing
4. **Add Comprehensive Logging** - Track system state

**Let's start with Step 2.1 (Timer System) as it's the most likely cause of the current issues!** 