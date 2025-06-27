# PS1 Emulator Debug Plan - Critical Steps

Based on PSX-Spex documentation and the CPU exception loop issue.

## 🚨 **CRITICAL ISSUE: CPU Exception Loop**

**Problem:** Infinite loop of `Cause=0x00` (Interrupt) exceptions
**Root Cause:** Interrupt not being properly acknowledged/cleared by BIOS

---

## 📋 **PHASE 1: Interrupt System Audit**

| Step | Status | File(s) | Notes |
|------|--------|---------|-------|
| 1.1: I_STAT Register | OK | interconnect.c | CDROM IRQ works, Timer0 needs real implementation |
| 1.1: I_MASK Register | OK | interconnect.c | Mask logic present, test with all sources |
| 1.2: Exception Vector | UNKNOWN | cpu.c | Not shown, verify vector logic |
| 1.2: Cause Register | UNKNOWN | cpu.c | Not shown, verify cause handling |
| 1.3: ERET Instruction | UNKNOWN | cpu.c | Not shown, verify ERET logic |

---

## 📋 **PHASE 2: Hardware Interrupt Sources**

| Step | Status | File(s) | Notes |
|------|--------|---------|-------|
| 2.1: Timer 0 (VBlank) | MISSING/INCOMPLETE | timer.c, interconnect.c | Must implement full timer logic for VBlank IRQ |
| 2.1: Timer 1 & 2 | STUB/INCOMPLETE | timer.c | Not critical for boot, stub for now |
| 2.2: GPU Interrupts | STUB/INCOMPLETE | gpu.c, interconnect.c | No evidence of GPU IRQ logic |
| 2.2: GPU DMA Completion | STUB/INCOMPLETE | dma.c, interconnect.c | DMA logic present, IRQs not fully handled |
| 2.3: CDROM Interrupts | OK | cdrom.c, interconnect.c | Now compliant with docs |

---

## 📋 **PHASE 3: BIOS Interrupt Handling**

| Step | Status | File(s) | Notes |
|------|--------|---------|-------|
| 3.1: Exception Vector at 0x80000080 | UNKNOWN | cpu.c | Not shown, verify BIOS handler is reached |
| 3.2: BIOS Interrupt Acknowledgment | OK | interconnect.c | I_STAT/I_MASK logic present, test with Timer0 |
| 3.3: BIOS Return from Exception | UNKNOWN | cpu.c | Not shown, verify ERET/return logic |

---

## 📋 **PHASE 4: Implementation Checklist**

| Step | Status | File(s) | Notes |
|------|--------|---------|-------|
| 4.1: Interconnect Module | OK | interconnect.c | Register routing is present, timer logic is stubbed |
| 4.2: CPU Module | UNKNOWN | cpu.c | Exception/ERET logic not shown |
| 4.3: Timer Module | MISSING/INCOMPLETE | timer.c | Timer0 must be implemented for VBlank IRQ |

---

## 📋 **PHASE 5: Testing Strategy**

| Step | Status | File(s) | Notes |
|------|--------|---------|-------|
| 5.1: Isolated Testing | NOT DONE | N/A | Should test IRQ controller and timer in isolation |
| 5.2: BIOS Integration Testing | NOT DONE | N/A | Try with minimal BIOS/handler |
| 5.3: Full BIOS Testing | IN PROGRESS | N/A | Stuck at logo due to missing timer IRQ |

---

## **KEY FINDINGS & NEXT STEPS**

- **Timer0 is the critical missing piece.**  
  Implement full Timer0 logic: counting, mode, target, IRQ generation, and acknowledgment.
- **Interconnect and CDROM logic are now compliant for IRQ/flag handling.**
- **CPU exception/ERET logic and GPU/DMA IRQs should be reviewed next, but are not the current blocker.**
- **Enhanced logging for IRQ state and timer events will help debugging.**

---

## **SUCCESS CRITERIA**

- [ ] No infinite exception loops
- [ ] BIOS properly acknowledges interrupts
- [ ] VBlank timing is stable
- [ ] CPU execution continues normally
- [ ] Sony logo displays correctly

---

## 🚀 **NEXT STEPS**

1. **Implement Timer0 (VBlank) logic in timer.c and ensure it triggers IRQs via interconnect.**
2. **Test BIOS progress and interrupt acknowledgment.**
3. **Add enhanced logging for timer and IRQ state.**
4. **Review CPU exception/ERET logic if BIOS still does not progress.**

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
2. **[CPU Specifications](https://psx-spx.consoledev.net/cpuspecs/)** - Exception handling
3. **[Kernel/BIOS](https://psx-spx.consoledev.net/kernelbios/)** - BIOS interrupt handling
4. **[Timers](https://psx-spx.consoledev.net/timers/)** - VBlank timer implementation
5. **[GPU](https://psx-spx.consoledev.net/gpu/)** - GPU interrupt generation

---

## 🎯 **SUCCESS CRITERIA**

- [ ] No infinite exception loops
- [ ] BIOS properly acknowledges interrupts
- [ ] VBlank timing is stable
- [ ] CPU execution continues normally
- [ ] Sony logo displays correctly

---

## 🚀 **NEXT STEPS**

1. **Start with Phase 1** - Audit interrupt controller implementation
2. **Add enhanced logging** - Track interrupt state changes
3. **Test isolated components** - Verify each piece works independently
4. **Integrate step by step** - Build up to full BIOS execution

## Recent Focus: BIOS Boot Loop & GPU Emulation

### Problem
- BIOS was stuck in a polling loop after the logo, not progressing to the menu.
- Renderer was not displaying anything after initialization.

### Actions Taken
- **GPUSTAT Register:**
  - Always set bits 26, 27, 28 to 1 (ready for command, VRAM-to-CPU, and DMA).
  - Set bit 25 to 1 if DMA is enabled.
  - DMA direction bits (29-30) reflect current setting.
  - IRQ bit (24) now toggles on each read to simulate hardware state changes.
- **Logging:**
  - Added detailed logging for all GP0/GP1 commands (opcode and value).
  - Added detailed logging for every GPUSTAT and GPUREAD access.
- **Renderer:**
  - Confirmed renderer is initialized, but not being triggered for display/draw calls.
- **CPU/BIOS:**
  - BIOS is stuck in a loop polling GPUSTAT, not sending new GPU commands.
  - No new draw/display commands are being issued.

### Next Steps
- **VBlank IRQ:**
  - Confirm VBlank IRQs are being triggered and acknowledged. BIOS may be waiting for a VBlank event.
- **BIOS Polling Loop:**
  - Disassemble the BIOS at addresses like 0x80059e0c, 0x80059e04, etc., to see what condition it is waiting for.
- **DMA/Interrupt Logging:**
  - Optionally add more detailed logging for DMA and interrupt register accesses.
- **Renderer:**
  - Investigate why the renderer is not being triggered. Ensure GPU commands that should cause a display update are implemented or stubbed.

### References
- PSX-Spex and nocash documentation for GPUSTAT, BIOS polling, and GPU command behavior.

---
_Last updated: [auto-generated by AI assistant]_

