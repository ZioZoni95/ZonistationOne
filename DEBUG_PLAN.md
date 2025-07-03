# PS1 Emulator Debug Plan - Critical Steps

Based on PSX-Spex documentation and the CPU exception loop issue.

## 🎯 **CURRENT STATUS: CPU Exception Handling COMPLETE**

**✅ ACHIEVED:** CPU Exception System Fully Implemented and Tested (June 2025)
- Exception vector jumps (SYSCALL → 0x80000080) ✅
- ERET instruction recognition and execution ✅
- Register updates (EPC, Cause, Status) ✅
- Exception flow control with `exception_pending` flag ✅
- Strict compliance with PSX-Spex/nocash documentation ✅

---

## 🚨 **PREVIOUS CRITICAL ISSUE: CPU Exception Loop - RESOLVED**

**Problem:** Infinite loop of `Cause=0x00` (Interrupt) exceptions  
**Status:** ✅ **RESOLVED** - CPU exception handling now properly implemented  
**Solution:** Added `exception_pending` flag and proper exception flow control

---

## 📋 **PHASE 1: Interrupt System Audit - COMPLETE**

| Step | Status | File(s) | Notes |
|------|--------|---------|-------|
| 1.1: I_STAT Register | ✅ OK | interconnect.c | CDROM IRQ works, Timer0 needs real implementation |
| 1.1: I_MASK Register | ✅ OK | interconnect.c | Mask logic present, test with all sources |
| 1.2: Exception Vector | ✅ COMPLETE | cpu.c | Exception vector logic fully implemented and tested |
| 1.2: Cause Register | ✅ COMPLETE | cpu.c | Cause handling properly implemented |
| 1.3: ERET Instruction | ✅ COMPLETE | cpu.c | ERET logic fully implemented and tested |

---

## 📋 **PHASE 2: Hardware Interrupt Sources**

| Step | Status | File(s) | Notes |
|------|--------|---------|-------|
| 2.1: Timer 0 (VBlank) | 🎯 **NEXT PRIORITY** | timer.c, interconnect.c | Must implement full timer logic for VBlank IRQ |
| 2.1: Timer 1 & 2 | STUB/INCOMPLETE | timer.c | Not critical for boot, stub for now |
| 2.2: GPU Interrupts | STUB/INCOMPLETE | gpu.c, interconnect.c | No evidence of GPU IRQ logic |
| 2.2: GPU DMA Completion | STUB/INCOMPLETE | dma.c, interconnect.c | DMA logic present, IRQs not fully handled |
| 2.3: CDROM Interrupts | ✅ OK | cdrom.c, interconnect.c | Now compliant with docs |

---

## 📋 **PHASE 3: BIOS Interrupt Handling - COMPLETE**

| Step | Status | File(s) | Notes |
|------|--------|---------|-------|
| 3.1: Exception Vector at 0x80000080 | ✅ COMPLETE | cpu.c | Exception vector properly implemented and tested |
| 3.2: BIOS Interrupt Acknowledgment | ✅ OK | interconnect.c | I_STAT/I_MASK logic present, test with Timer0 |
| 3.3: BIOS Return from Exception | ✅ COMPLETE | cpu.c | ERET/return logic fully implemented |

---

## 📋 **PHASE 4: Implementation Checklist**

| Step | Status | File(s) | Notes |
|------|--------|---------|-------|
| 4.1: Interconnect Module | ✅ OK | interconnect.c | Register routing is present, timer logic is stubbed |
| 4.2: CPU Module | ✅ COMPLETE | cpu.c | Exception/ERET logic fully implemented and tested |
| 4.3: Timer Module | 🎯 **NEXT PRIORITY** | timer.c | Timer0 must be implemented for VBlank IRQ |

---

## 📋 **PHASE 5: Testing Strategy**

| Step | Status | File(s) | Notes |
|------|--------|---------|-------|
| 5.1: Isolated Testing | ✅ COMPLETE | tests/ | CPU exception handling thoroughly tested |
| 5.2: BIOS Integration Testing | ✅ COMPLETE | N/A | CPU exception system integrated with main emulator |
| 5.3: Full BIOS Testing | 🔄 IN PROGRESS | N/A | Ready for Timer0 implementation |

---

## **KEY FINDINGS & NEXT STEPS**

- **✅ CPU Exception System is now complete and tested.**
- **Timer0 is the next critical piece.**  
  Implement full Timer0 logic: counting, mode, target, IRQ generation, and acknowledgment.
- **Interconnect and CDROM logic are compliant for IRQ/flag handling.**
- **GPU/DMA IRQs should be reviewed next, but are not the current blocker.**
- **Enhanced logging for IRQ state and timer events will help debugging.**

---

## **SUCCESS CRITERIA**

- [x] No infinite exception loops ✅ **ACHIEVED**
- [x] CPU exception handling properly implemented ✅ **ACHIEVED**
- [x] Exception vector jumps work correctly ✅ **ACHIEVED**
- [x] ERET instruction properly recognized ✅ **ACHIEVED**
- [ ] BIOS properly acknowledges interrupts
- [ ] VBlank timing is stable
- [ ] CPU execution continues normally
- [ ] Sony logo displays correctly

---

## 🚀 **NEXT STEPS**

1. **🎯 Implement Timer0 (VBlank) logic** in timer.c and ensure it triggers IRQs via interconnect.
2. **Test BIOS progress** and interrupt acknowledgment with Timer0.
3. **Add enhanced logging** for timer and IRQ state.
4. **Review GPU/DMA IRQ logic** if BIOS still does not progress.

---

## 🔧 **DEBUGGING TOOLS NEEDED**

### **Enhanced Logging**
```c
// Add to interrupt handling
LOG_DEBUG("IRQ: Source=0x%02x, I_STAT=0x%04x, I_MASK=0x%04x", source, i_stat, i_mask);
LOG_DEBUG("CPU: Exception at PC=0x%08x, Cause=0x%02x", pc, cause);
LOG_DEBUG("BIOS: Acknowledging IRQ, I_STAT: 0x%04x -> 0x%04x", old_stat, new_stat);
```

### **Interrupt State Monitoring**
```c
// Monitor interrupt state changes
// Track which interrupts are pending/acknowledged
// Log exception vector jumps
```

---

## 📚 **KEY PSX-SPEX REFERENCES**

1. **[Interrupts](https://psx-spx.consoledev.net/interrupts/)** - Interrupt controller details
2. **[CPU Specifications](https://psx-spx.consoledev.net/cpuspecs/)** - Exception handling ✅ **IMPLEMENTED**
3. **[Kernel/BIOS](https://psx-spx.consoledev.net/kernelbios/)** - BIOS interrupt handling
4. **[Timers](https://psx-spx.consoledev.net/timers/)** - VBlank timer implementation 🎯 **NEXT**
5. **[GPU](https://psx-spx.consoledev.net/gpu/)** - GPU interrupt generation

---

## 🎯 **SUCCESS CRITERIA**

- [x] No infinite exception loops ✅ **ACHIEVED**
- [x] CPU exception handling properly implemented ✅ **ACHIEVED**
- [x] Exception vector jumps work correctly ✅ **ACHIEVED**
- [x] ERET instruction properly recognized ✅ **ACHIEVED**
- [ ] BIOS properly acknowledges interrupts
- [ ] VBlank timing is stable
- [ ] CPU execution continues normally
- [ ] Sony logo displays correctly

---

## 🚀 **NEXT STEPS**

1. **🎯 Start with Timer0 implementation** - VBlank timer for proper interrupt timing
2. **Add enhanced logging** - Track interrupt state changes
3. **Test isolated components** - Verify each piece works independently
4. **Integrate step by step** - Build up to full BIOS execution

## Recent Focus: CPU Exception Handling - COMPLETE ✅

### Problem
- CPU exception handling was incomplete, causing issues with SYSCALL and ERET instructions
- Exception vector jumps were not working properly
- Register updates (EPC, Cause, Status) were not implemented correctly

### Actions Taken
- **Exception Vector System:**
  - Implemented proper exception vector selection (0x80000080 or 0xBFC00180 based on BEV bit)
  - Added `exception_pending` flag to prevent instruction cycle corruption
  - Fixed exception flow control to ensure proper vector jumps
- **Instruction Recognition:**
  - Fixed SYSCALL instruction (0x0000000C) recognition and exception triggering
  - Fixed ERET instruction (0x42000010) recognition and execution
  - Added proper instruction encoding validation
- **Register Management:**
  - Implemented proper EPC (Exception Program Counter) updates
  - Added Cause register updates with exception codes and BD (Branch Delay) bit
  - Implemented Status register updates with mode/IE bit stacking
- **Testing:**
  - Created comprehensive CPU exception handling test suite
  - Verified exception vector jumps work correctly
  - Confirmed ERET instruction recognition and execution
  - Added integration testing with main emulator

### Next Steps
- **Timer 0 (VBlank) Implementation:**
  - Implement full Timer0 logic for VBlank interrupt generation
  - Integrate with CPU exception system
  - Test BIOS progress with proper VBlank timing
- **GPU Integration:**
  - Complete graphics pipeline integration
  - Implement GPU interrupt generation
- **DMA System:**
  - Full DMA transfer implementation
  - DMA completion interrupt handling

### References
- PSX-Spex and nocash documentation for CPU exception handling, Timer0 implementation, and BIOS integration.

---
_Last updated: June 2025 - CPU Exception Handling Complete_

# Debug Plan

## Current Focus
- Timer0 IRQ0 logic: Fixed for nocash/PSX-Spex, but standalone test still fails.
- Add debug output to Timer0 test, verify counter/target/IRQ logic.

## Step-by-Step Debug
1. **Timer0**: Add debug output, verify IRQ0 request on target crossing.
2. **Interconnect**: Unit test IRQ request/acknowledge, I_STAT/I_MASK.
3. **CPU**: Unit test IRQ exception, COP0 registers.
4. **BIOS**: Integration test for BIOS boot loop with IRQ0.
5. **DMA**: Unit/integration test for DMA IRQs.
6. **GPU**: Integration test for VBlank/Timer0/IRQ0.

## General Strategy
- Test each component in isolation, then in integration.
- Follow nocash/PSX-Spex for all logic and test cases.
- Update this file after each test/fix.

