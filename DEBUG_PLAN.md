# PS1 Emulator Debug Plan - Critical Steps

Based on PSX-Spex documentation and the CPU exception loop issue.

## 🚨 **CRITICAL ISSUE: CPU Exception Loop**

**Problem:** Infinite loop of `Cause=0x00` (Interrupt) exceptions
**Root Cause:** Interrupt not being properly acknowledged/cleared by BIOS

---

## 📋 **PHASE 1: Interrupt System Audit**

### **Step 1.1: Verify Interrupt Controller Implementation**
- [ ] **Check I_STAT Register (0x1F801070)**
  - Ensure interrupt bits are properly set when hardware triggers
  - Verify bits are cleared when BIOS acknowledges
  - Reference: [PSX-Spex: Interrupts](https://psx-spx.consoledev.net/interrupts/)

- [ ] **Check I_MASK Register (0x1F801074)**
  - Verify interrupt enable/disable logic
  - Ensure proper masking of individual interrupt sources
  - Reference: [PSX-Spex: Interrupts](https://psx-spx.consoledev.net/interrupts/)

### **Step 1.2: CPU Exception Vector Implementation**
- [ ] **Exception Vector Table**
  - Verify CPU jumps to `0x80000080` for general exceptions
  - Verify CPU jumps to `0xBFC00180` for reset/boot exceptions
  - Reference: [PSX-Spex: CPU Specifications](https://psx-spx.consoledev.net/cpuspecs/)

- [ ] **Exception Cause Register**
  - Ensure `Cause=0x00` correctly indicates interrupt exception
  - Verify other exception types are handled properly
  - Reference: [PSX-Spex: CPU Specifications](https://psx-spx.consoledev.net/cpuspecs/)

### **Step 1.3: Return from Exception (ERET)**
- [ ] **ERET Instruction Implementation**
  - Verify CPU properly restores PC and SR from EPC and ESR
  - Ensure interrupt enable state is restored correctly
  - Reference: [PSX-Spex: CPU Specifications](https://psx-spx.consoledev.net/cpuspecs/)

---

## 📋 **PHASE 2: Hardware Interrupt Sources**

### **Step 2.1: Timer Interrupts**
- [ ] **Timer 0 (VBlank)**
  - Verify Timer 0 generates interrupt at correct frequency
  - Check interrupt acknowledgment clears timer interrupt bit
  - Reference: [PSX-Spex: Timers](https://psx-spx.consoledev.net/timers/)

- [ ] **Timer 1 & 2**
  - Verify other timers don't interfere with VBlank timing
  - Check proper interrupt routing

### **Step 2.2: GPU Interrupts**
- [ ] **GPU Command Completion**
  - Verify GPU sets interrupt when command FIFO is empty
  - Check interrupt acknowledgment clears GPU interrupt
  - Reference: [PSX-Spex: GPU](https://psx-spx.consoledev.net/gpu/)

- [ ] **GPU DMA Completion**
  - Verify DMA completion interrupts are handled
  - Check proper interrupt routing to CPU

### **Step 2.3: CDROM Interrupts**
- [ ] **CDROM Command Completion**
  - Verify CDROM sets interrupt when command completes
  - Check interrupt acknowledgment clears CDROM interrupt
  - Reference: [PSX-Spex: CDROM](https://psx-spx.consoledev.net/cdromdrive/)

---

## 📋 **PHASE 3: BIOS Interrupt Handling**

### **Step 3.1: BIOS Exception Handler**
- [ ] **Exception Vector at 0x80000080**
  - Verify BIOS code at this address handles interrupt exceptions
  - Check if BIOS properly acknowledges interrupts
  - Reference: [PSX-Spex: Kernel/BIOS](https://psx-spx.consoledev.net/kernelbios/)

### **Step 3.2: BIOS Interrupt Acknowledgment**
- [ ] **Interrupt Acknowledgment Pattern**
  - BIOS should read I_STAT, clear specific bits, write back
  - Verify emulator properly handles this read-modify-write cycle
  - Reference: [PSX-Spex: Interrupts](https://psx-spx.consoledev.net/interrupts/)

### **Step 3.3: BIOS Return from Exception**
- [ ] **ERET Execution**
  - Verify BIOS executes ERET after handling interrupt
  - Check CPU properly resumes execution
  - Reference: [PSX-Spex: Kernel/BIOS](https://psx-spx.consoledev.net/kernelbios/)

---

## 📋 **PHASE 4: Implementation Checklist**

### **Step 4.1: Interconnect Module**
- [ ] **Memory-Mapped I/O for Interrupt Registers**
  ```c
  // 0x1F801070 - I_STAT (Interrupt Status)
  // 0x1F801074 - I_MASK (Interrupt Mask)
  ```
  - Verify proper read/write handling
  - Check interrupt bit manipulation

### **Step 4.2: CPU Module**
- [ ] **Exception Handling**
  ```c
  // Exception vector dispatch
  // EPC/ESR register management
  // ERET instruction implementation
  ```
  - Verify exception vector calculation
  - Check register save/restore

### **Step 4.3: Timer Module**
- [ ] **VBlank Timer**
  ```c
  // Timer 0 = VBlank (typically 60Hz)
  // Interrupt generation and acknowledgment
  ```
  - Verify timer frequency and interrupt generation
  - Check interrupt acknowledgment

---

## 📋 **PHASE 5: Testing Strategy**

### **Step 5.1: Isolated Testing**
- [ ] **Test Interrupt Controller Alone**
  - Set interrupt bits manually
  - Verify CPU exception generation
  - Test interrupt acknowledgment

### **Step 5.2: BIOS Integration Testing**
- [ ] **Test with Minimal BIOS**
  - Use simple interrupt handler
  - Verify end-to-end interrupt flow
  - Check no infinite loops

### **Step 5.3: Full BIOS Testing**
- [ ] **Test with Real BIOS**
  - Monitor interrupt acknowledgment patterns
  - Verify proper exception handling
  - Check stable execution

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

**Ready to begin debugging? Let's start with the interrupt controller audit!** 