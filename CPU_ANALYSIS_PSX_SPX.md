# CPU Implementation Analysis Against PSX-SPX Documentation

## Overview
Analyzed our current CPU implementation (src/cpu.c, include/cpu.h) against the comprehensive PSX-SPX CPU specifications at https://psx-spx.consoledev.net/cpuspecifications/

## Current Implementation Status

### ✅ **Correctly Implemented**

1. **Core MIPS R3000A Architecture**
   - 32 general-purpose registers (R0-R31) with R0 hardwired to zero
   - Program Counter (PC) with branch delay slot handling
   - HI/LO registers for multiply/divide results
   - Load delay slot implementation
   - Branch delay slot state tracking

2. **Basic COP0 System Control Coprocessor**
   - SR (Status Register, cop0r12) - Interrupt enables, kernel/user mode, cache control
   - CAUSE (cop0r13) - Exception codes, interrupt pending bits, branch delay flag
   - EPC (Exception Program Counter, cop0r14) - Return address from exceptions
   - RFE (Return From Exception) instruction

3. **Exception Handling System**
   - Proper exception cause codes matching PSX-SPX specification
   - Exception vector calculation based on BEV bit
   - Branch delay slot exception handling
   - Interrupt acknowledgment through interconnect

4. **Instruction Cache**
   - 4KB I-Cache (256 lines × 4 words) as per PSX specifications
   - Cache hit/miss logic with tag comparison

## ❌ **Missing or Incomplete Features**

### 1. **Incomplete COP0 Register Set**

According to PSX-SPX, several COP0 registers are missing:

```c
// Missing COP0 registers that should be added:
uint32_t prid;        // cop0r15 - Processor ID (should return 0x00000002)
uint32_t bad_vaddr;   // cop0r8  - Bad Virtual Address  
uint32_t tar;         // cop0r6  - Target Address (for branch exceptions)

// Debug registers (used by LibCrypt copy protection):
uint32_t bpc;         // cop0r3  - Breakpoint Program Counter
uint32_t bda;         // cop0r5  - Breakpoint Data Address  
uint32_t dcic;        // cop0r7  - Debug and Cache Invalidate Control
uint32_t bpcm;        // cop0r11 - Breakpoint PC Mask
uint32_t bdam;        // cop0r9  - Breakpoint Data Address Mask
```

**Impact**: Missing debug registers affect LibCrypt protection compatibility. Games like "Legacy of Kain: Soul Reaver" use these registers for copy protection.

### 2. **Cache Control Implementation**

PSX-SPX specifies cache isolation functionality (SR.Isc bit) used by PSX Kernel:
- When SR.Isc=1, all load/store operations target D-Cache instead of main memory
- Used with Port FFFE0130h for cache manipulation
- Required for proper BIOS kernel operation

### 3. **Load Delay Timing**

Current load delay may not match PSX-SPX specifications:
- PSX-SPX states load data is NOT available to the next opcode
- COP2 (GTE) operations also have load delay
- Need to verify exact timing compliance

### 4. **Exception Handling Gaps**

Missing exception types and handling:
- **Address Error Store** (AdES, ExcCode=05h) - for misaligned stores
- **Bus Error** on instruction fetch (IBE, ExcCode=06h) 
- **Bus Error** on data access (DBE, ExcCode=07h)
- **BadVaddr** register not updated on address errors

### 5. **Instruction Set Completeness**

Need to verify all PSX-required instructions are implemented:
- Unaligned load/store (LWL, LWR, SWL, SWR)
- All coprocessor instructions (COP0/COP2)
- Proper illegal instruction detection

### 6. **Interrupt Handling Issues**

PSX-SPX specifies special interrupt vs GTE command interaction:
```
// Missing: Check for GTE command at EPC during interrupt
if (cause == EXCEPTION_INTERRUPT) {
    uint32_t epc_instr = interconnect_load32(cpu->inter, cpu->epc);
    if ((epc_instr & 0xFE000000) == 0x4A000000) { // COP2 command
        cpu->epc += 4; // Skip the GTE command to avoid double execution
    }
}
```

**Impact**: Games like Crash Bandicoot trilogy, Spyro, and Jinx rely on this for correct geometry rendering.

## 🔧 **Recommended Improvements**

### Priority 1: Critical for Compatibility
1. **Add missing COP0 registers** - Especially PRID, BadVaddr, and debug registers
2. **Implement cache isolation** - Required for proper BIOS operation
3. **Fix GTE interrupt handling** - Critical for major games

### Priority 2: Enhanced Accuracy  
1. **Verify load delay timing** - Ensure exact PSX-SPX compliance
2. **Add missing exception types** - Address errors, bus errors
3. **Complete instruction set** - Verify all opcodes are implemented

### Priority 3: Debugging Support
1. **Implement debug breakpoints** - BPC/BDA register functionality
2. **Add cache control port** - Memory control register 0xFFFE0130h
3. **Enhance exception logging** - More detailed PSX-SPX compliant reporting

## Implementation Plan

1. **Phase 1**: Add missing COP0 registers and basic functionality
2. **Phase 2**: Implement cache isolation and control
3. **Phase 3**: Fix interrupt/GTE interaction 
4. **Phase 4**: Add missing exception types and proper address error handling
5. **Phase 5**: Verify and complete instruction set implementation

## Files to Modify

- `include/cpu.h` - Add missing COP0 register fields
- `src/cpu.c` - Implement missing register handling in MFC0/MTC0
- `src/cpu.c` - Fix exception handling and interrupt logic
- `include/interconnect.h` - Add cache control memory mapping
- `src/interconnect.c` - Implement cache control port

This analysis provides a roadmap for making our CPU implementation fully PSX-SPX compliant and compatible with demanding games and copy protection schemes.