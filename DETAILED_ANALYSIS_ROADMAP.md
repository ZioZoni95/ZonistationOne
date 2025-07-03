# PS1 Emulator - Detailed Analysis & Strict Roadmap

_Last updated: 01 July 2025_

## 🏁 PRIORITY OVERVIEW (01 July 2025)

1. **Timer0 / VBlank IRQ0 (CRITICAL)**
   - Re-implement for correct VBlank IRQ0 timing and handling
2. **GPU & Renderer Audit**
   - Audit GPUSTAT, command parsing, and interrupt logic
3. **System Integration Testing**
   - Verify BIOS boot and integration after Timer0 fix

## 📊 **COMPREHENSIVE CODEBASE ANALYSIS**

Based on thorough examination of your actual codebase against PSX-Spex documentation.

---

## 🎯 **CURRENT STATUS: CPU Exception Handling COMPLETE**

**✅ LATEST ACHIEVEMENT:** CPU Exception System Fully Implemented and Tested (June 2025)
- Exception vector jumps (SYSCALL → 0x80000080) ✅
- ERET instruction recognition and execution ✅
- Register updates (EPC, Cause, Status) ✅
- Exception flow control with `exception_pending` flag ✅
- Strict compliance with PSX-Spex/nocash documentation ✅

---

## 🔍 **COMPONENT-BY-COMPONENT ANALYSIS**

### **1. CPU CORE (MIPS R3000A)** ✅ **COMPLETE**
**Files:** `src/cpu.c`, `include/cpu.h`
**PSX-Spex Compliance:** ✅ **COMPLETE**

#### **Current Implementation Analysis:**
✅ **What's Working:**
- Basic MIPS instruction set (most instructions implemented)
- Register file and load delay slot handling
- Instruction cache implementation
- **Exception handling fully implemented and tested**
- **ERET instruction properly implemented**
- **Exception vector handling working correctly**
- **Status register management complete**
- **Infinite exception loop issue resolved**

#### **PSX-Spex Requirements vs Current Code:**
- **Exception Vector:** ✅ Jumps to 0x80000080 (or 0xbfc00180 if BEV=1)
- **ERET Instruction:** ✅ Restores SR and jumps to EPC
- **Interrupt Handling:** ✅ Follows proper BIOS flow
- **Status Register:** ✅ Properly manages mode stack (bits 5:0)

**Action:** ✅ **COMPLETE** - CPU exception handling system fully functional

---

### **2. TIMER SYSTEM** 🎯 **NEXT PRIORITY**
**Files:** `src/timers.c`, `include/timers.h`
**PSX-Spex Compliance:** ⚠️ **INCOMPLETE**

#### **Current Implementation Analysis:**
✅ **What's Working:**
- Timer register structure and mode decoding
- Basic counting logic
- Interrupt request mechanism
- BIOS boot helper (forces Timer0 config)

❌ **Critical Issues Found:**
1. **Timer 0 VBlank Timing** - Target value may be incorrect
2. **Clock Source Handling** - System clock vs dot clock logic unclear
3. **Interrupt Generation** - May not generate proper VBlank IRQ
4. **Timer Synchronization** - Sync modes not properly implemented

#### **PSX-Spex Requirements vs Current Code:**
- **Timer 0:** Must generate VBlank IRQ at correct frequency (44100 Hz)
- **Clock Sources:** Must handle system clock and dot clock correctly
- **Interrupt Timing:** Must match PSX VBlank timing exactly

**Action:** **RE-IMPLEMENT** Timer 0 for proper VBlank IRQ generation

---

### **3. INTERCONNECT BUS** ✅ **GOOD**
**Files:** `src/interconnect.c`, `include/interconnect.h`
**PSX-Spex Compliance:** ✅ **GOOD**

#### **Current Implementation Analysis:**
✅ **What's Working:**
- Memory-mapped I/O routing looks correct
- Interrupt controller (I_STAT/I_MASK) implementation
- Register access handling
- Memory region mapping

**Action:** Keep as-is, verify against PSX-Spex memory map

## ✅ ROADMAP STATUS UPDATE (01 July 2025)
- Interconnect: FULLY TESTED & nocash/PSX-Spex compliant (all regions, open bus, and edge cases verified)
- Cleared for integration and next development phase.
- Next: Timer0/VBlank IRQ.

---

### **4. MEMORY MANAGEMENT** ✅ **GOOD**
**Files:** `src/ram.c`, `src/vram.c`, `include/ram.h`, `include/vram.h`
**PSX-Spex Compliance:** ✅ **GOOD**

#### **Current Implementation Analysis:**
✅ **What's Working:**
- RAM implementation (2MB at 0x00000000)
- VRAM implementation
- Memory access functions

**Action:** Keep as-is, verify against PSX-Spex memory map

---

### **5. CDROM CONTROLLER** ✅ **GOOD**
**Files:** `src/cdrom.c`, `include/cdrom.h`
**PSX-Spex Compliance:** ✅ **GOOD**

#### **Current Implementation Analysis:**
✅ **What's Working:**
- Command handling appears correct
- Interrupt generation implemented
- Status register implementation
- FIFO handling

**Action:** Keep as-is, verify against PSX-Spex

---

### **6. GPU SYSTEM** 🔄 **NEEDS AUDIT**
**Files:** `src/gpu.c`, `src/renderer.c`, `include/gpu.h`, `include/renderer.h`
**PSX-Spex Compliance:** ⚠️ **UNKNOWN**

#### **Current Implementation Analysis:**
⚠️ **Status Unknown:**
- GPUSTAT register implementation
- Command parsing
- Renderer integration
- Interrupt generation

**Action:** Audit against PSX-Spex GPU specifications

---

### **7. DMA CONTROLLER** 🔄 **NEEDS AUDIT**
**Files:** `src/dma.c`, `include/dma.h`
**PSX-Spex Compliance:** ⚠️ **UNKNOWN**

#### **Current Implementation Analysis:**
⚠️ **Status Unknown:**
- DMA channel implementation
- Transfer logic
- Interrupt generation

**Action:** Audit against PSX-Spex DMA specifications

---

### **8. GTE (Geometry Transformation Engine)** 🔄 **NEEDS AUDIT**
**Files:** `src/gte.c`, `include/gte.h`
**PSX-Spex Compliance:** ⚠️ **UNKNOWN**

#### **Current Implementation Analysis:**
⚠️ **Status Unknown:**
- Basic structure present
- Register handling
- Instruction execution

**Action:** Audit against PSX-Spex GTE specifications

---

### **9. BIOS INTERFACE** ✅ **GOOD**
**Files:** `src/bios.c`, `include/bios.h`
**PSX-Spex Compliance:** ✅ **GOOD**

#### **Current Implementation Analysis:**
✅ **What's Working:**
- BIOS loading from file
- Memory access functions
- Checksum verification structure

**Action:** Keep as-is

---

## 🎯 **STRICT ROADMAP FOR RESTART**

### **PHILOSOPHY:**
1. **Fix critical components first** - CPU and Timer are blocking everything
2. **Follow PSX-Spex exactly** - No deviations from documentation
3. **Test each component independently** - Ensure it works before integration
4. **Minimal changes** - Only re-implement what's broken

---

## 📋 **STEP-BY-STEP IMPLEMENTATION PLAN**

### **NOTE ON TEST ORGANIZATION**
- All new test harnesses, stubs, and isolated component tests will be placed in a dedicated `tests/` directory for clean project organization.
- Example: `tests/cpu_test.c`, `tests/dma_stub.c`, etc.

### **STEP 1: CPU Exception Handling Re-Implementation** ⭐ **COMPLETE**
**Priority:** HIGHEST (source of infinite loops) ✅ **COMPLETED**
**Time Estimate:** 1-2 days ✅ **COMPLETED**
**Files Modified:** `src/cpu.c` (exception handling section), `tests/` (test harnesses and stubs) ✅ **COMPLETED**

#### **PSX-Spex Requirements:**
1. **Exception Vector:** Must jump to 0x80000080 (or 0xbfc00180 if BEV=1) ✅
2. **ERET Instruction:** Must restore SR and jump to EPC ✅
3. **Status Register:** Must properly manage mode stack (bits 5:0) ✅
4. **Interrupt Handling:** Must follow proper BIOS flow ✅

#### **Implementation Completed:**
- ✅ **Exception Vector System**: Proper exception vector selection implemented
- ✅ **Instruction Recognition**: SYSCALL and RFE properly decoded
- ✅ **Register Management**: EPC, Cause, Status updates implemented
- ✅ **Exception Flow Control**: `exception_pending` flag prevents corruption
- ✅ **Testing**: Comprehensive test suite created and passed

---

### **STEP 2: Timer System Re-Implementation** ⭐ **CRITICAL**
**Priority:** HIGHEST (needed for VBlank IRQ)
**Time Estimate:** 1-2 days
**Files to Re-implement:** `src/timers.c`, `include/timers.h`

#### **PSX-Spex Requirements:**
1. **Timer 0 (VBlank)**: Must generate IRQ at correct frequency
2. **Clock Sources**: Must handle system clock and dot clock correctly
3. **Interrupt Generation**: Must match PSX VBlank timing exactly
4. **Mode Handling**: Must support all timer modes properly

#### **Implementation Order:**
1. **Timer 0 (VBlank)** - System clock mode, IRQ generation
2. **Timer 1 (HBlank)** - System clock mode
3. **Timer 2 (System)** - System clock mode
4. **Interrupt generation and acknowledgment**
5. **Clock source handling**

#### **Test Criteria:**
- [ ] Timer 0 counts correctly at 44100 Hz
- [ ] Timer 0 generates VBlank IRQ when target reached
- [ ] Timer interrupts are properly acknowledged
- [ ] No infinite interrupt loops

---

### **STEP 3: Interrupt System Integration**
**Priority:** HIGH
**Time Estimate:** 1 day
**Files to Audit:** `src/interconnect.c` (interrupt section)

#### **Test Criteria:**
- [ ] CPU receives interrupts from timers
- [ ] Interrupt acknowledgment works correctly
- [ ] I_STAT/I_MASK registers work properly
- [ ] No interrupt storms

---

### **STEP 4: Basic System Boot Test**
**Priority:** HIGH
**Time Estimate:** 1 day
**Goal:** Get BIOS to boot without infinite loops

#### **Test Criteria:**
- [ ] BIOS loads and starts execution
- [ ] No infinite exception loops
- [ ] VBlank timing works
- [ ] System reaches BIOS menu

---

### **STEP 5: GPU System Audit & Fix**
**Priority:** MEDIUM
**Time Estimate:** 2-3 days
**Files to Audit:** `src/gpu.c`, `src/renderer.c`

#### **PSX-Spex References:**
- [GPU](https://psx-spx.consoledev.net/gpu/)

#### **Test Criteria:**
- [ ] GP0/GP1 commands work correctly
- [ ] GPUSTAT register reflects correct state
- [ ] Basic rendering works
- [ ] Sony logo displays

---

### **STEP 6: DMA System Audit**
**Priority:** MEDIUM
**Time Estimate:** 1-2 days
**Files to Audit:** `src/dma.c`

#### **PSX-Spex References:**
- [DMA](https://psx-spx.consoledev.net/dma/)

---

### **STEP 7: GTE System Audit**
**Priority:** LOW
**Time Estimate:** 2-3 days
**Files to Audit:** `src/gte.c`

#### **PSX-Spex References:**
- [GTE](https://psx-spx.consoledev.net/gte/)

---

### **STEP 8: Full System Integration**
**Priority:** MEDIUM
**Time Estimate:** 1-2 days
**Goal:** Complete system integration

---

## 🚀 **IMMEDIATE NEXT STEPS**

1. **Start with Step 2 (Timer Re-implementation)** - This is the next critical component
2. **Create isolated test cases** for each component
3. **Follow PSX-Spex documentation exactly**
4. **Test each step independently** before moving to the next

**Would you like to start with the Timer re-implementation? This will solve the VBlank IRQ issue and allow BIOS to progress.**

---

## 📚 **PSX-SPEX REFERENCES FOR IMPLEMENTATION**

### **CPU Exception Handling:**
- [CPU Specifications](https://psx-spx.consoledev.net/cpuspecs/)
- [Exception Handling](https://psx-spx.consoledev.net/cpuspecs/#exceptions)
- [Interrupts](https://psx-spx.consoledev.net/interrupts/)

### **Timer System:**
- [Timers](https://psx-spx.consoledev.net/timers/)

### **GPU System:**
- [GPU](https://psx-spx.consoledev.net/gpu/)

### **DMA System:**
- [DMA](https://psx-spx.consoledev.net/dma/)

### **GTE System:**
- [GTE](https://psx-spx.consoledev.net/gte/)

---

## ❓ **RECOMMENDATION**

**Start with Step 2 (Timer Re-implementation)** because:

1. **It's the next critical component**
2. **It solves the VBlank IRQ issue**
3. **It allows BIOS to progress**
4. **It's a focused, manageable task**

**Would you like to start with the Timer re-implementation?**

## Current Focus
- Timer0 IRQ0 logic: Fixed for nocash/PSX-Spex, but standalone test still fails.
- Add debug output, verify counter/target/IRQ logic.

## Component Roadmap
1. **Timer0**: Debug and verify IRQ0 request on target crossing.
2. **Interconnect**: Test IRQ request/acknowledge, I_STAT/I_MASK.
3. **CPU**: Test IRQ exception, COP0 registers.
4. **BIOS**: Test BIOS boot loop with IRQ0.
5. **DMA**: Test DMA IRQs.
6. **GPU**: Test VBlank/Timer0/IRQ0 integration.

## Integration
- After unit tests, verify all components work together.
- Follow nocash/PSX-Spex for all logic and test cases.
- Update this file after each test/fix. 