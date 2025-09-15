# RAM Implementation Analysis Against PSX-SPX Documentation

## Overview
Analyzed our current RAM implementation (src/ram.c, include/ram.h, interconnect mapping) against the comprehensive PSX-SPX memory map specifications at https://psx-spx.consoledev.net/memorymap/

## Current Implementation Status

### ✅ **Correctly Implemented**

1. **Basic RAM Structure**
   - 2MB main RAM (RAM_SIZE = 2 * 1024 * 1024) ✅
   - Physical address range: 0x00000000-0x001FFFFF ✅
   - Little-endian byte ordering ✅
   - 8/16/32-bit access functions ✅

2. **Memory Region Mapping (Virtual to Physical)**
   - KUSEG (0x00000000-0x7FFFFFFF): Direct mapping ✅
   - KSEG0 (0x80000000-0x9FFFFFFF): Cached, maps to physical 0x00000000-0x1FFFFFFF ✅
   - KSEG1 (0xA0000000-0xBFFFFFFF): Uncached, maps to physical 0x00000000-0x1FFFFFFF ✅
   - KSEG2 (0xC0000000-0xFFFFFFFF): Unchanged (for cache control) ✅

3. **Address Translation Logic**
   ```c
   const uint32_t REGION_MASK[8] = {
       0xffffffff, // KUSEG - No mask
       0x7fffffff, // KSEG0 - Mask top bit  
       0x1fffffff, // KSEG1 - Mask top 3 bits
       0xffffffff, // KSEG2 - No mask
   };
   ```
   This correctly implements the PSX-SPX memory region specifications ✅

4. **Basic Error Handling**
   - Bounds checking for all access sizes ✅
   - Special handling for null pointer (0x00000000) access ✅
   - Proper logging for debugging ✅

5. **BIOS Compatibility Features**
   - RFE handler at 0x80 (exception vector) ✅
   - BIOS patch area initialization ✅
   - Zero-filled RAM (matches PS1 power-on state) ✅

## ❌ **Missing or Incomplete Features**

### 1. **RAM Mirroring Support**

According to PSX-SPX, **2MB RAM can be mirrored to the first 8MB** (enabled by default):
```
2MB RAM can be mirrored to the first 8MB (strangely, enabled by default)
```

**Current Issue**: Our implementation only handles 0x00000000-0x001FFFFF (2MB). 
**Missing**: Mirror mapping should handle 0x00200000-0x007FFFFF (additional 6MB).

**Impact**: Games or BIOS code accessing mirrored RAM addresses above 2MB will fail.

### 2. **Scratchpad Memory Missing**

PSX-SPX specifies **1KB Scratchpad at 0x1F800000-0x1F8003FF**:
```
1F800000h 9F800000h    --      1K     Scratchpad (D-Cache used as Fast RAM)
```

**Current Issue**: No scratchpad implementation at all.
**Missing**: 1KB fast RAM area that's separate from main RAM.

**Impact**: Games using scratchpad for performance-critical code will fail.

### 3. **Memory Control Register Handling**

PSX-SPX mentions memory control that affects RAM behavior:
- RAM size configuration via memory control registers
- Mirror enable/disable functionality 
- Base address configuration

**Current Issue**: Memory control registers exist but may not affect RAM behavior.

### 4. **Write Queue Awareness**

PSX-SPX specifies different write queue behavior for different memory regions:
- KUSEG/KSEG0: Write queue enabled
- KSEG1: Write queue disabled (immediate write)

**Current Issue**: Our RAM implementation doesn't differentiate between cached/uncached access.
**Impact**: Hardware register timing may be incorrect.

### 5. **Memory Exception Handling**

PSX-SPX defines specific memory exceptions:
```
Memory Error -> Misalignments
Bus Error    -> Unused Memory Regions
```

**Current Issue**: Limited exception handling in RAM module.
**Missing**: Proper BadVaddr setting for address errors.

## 🔧 **Recommended Improvements**

### Priority 1: Critical for Compatibility

1. **Implement RAM Mirroring**
   ```c
   // In interconnect_load32():
   if (physical_addr <= 0x007FFFFF) {  // First 8MB
       uint32_t ram_offset = physical_addr % RAM_SIZE;  // Mirror to 2MB
       return ram_load32(inter->ram, ram_offset);
   }
   ```

2. **Add Scratchpad Memory**
   ```c
   typedef struct {
       uint8_t data[1024];  // 1KB scratchpad
   } Scratchpad;
   
   // Handle 0x1F800000-0x1F8003FF in interconnect
   ```

### Priority 2: Enhanced Accuracy

1. **Memory Control Register Integration**
   - Connect RAM_SIZE_ADDR (0x1f801060) to actual RAM behavior
   - Implement mirror enable/disable functionality
   - Add base address configuration support

2. **Write Queue Behavior**
   - Add cache/uncached access tracking
   - Implement proper write ordering for KSEG1 vs KSEG0

### Priority 3: Robustness  

1. **Enhanced Exception Handling**
   - Proper alignment checking with BadVaddr updates
   - Bus error generation for unmapped regions
   - Integration with CPU exception system

2. **Performance Optimizations**
   - Direct memory access for aligned operations
   - Reduced bounds checking in hot paths

## Implementation Plan

1. **Phase 1**: Add RAM mirroring (8MB address space)
2. **Phase 2**: Implement scratchpad memory (1KB at 0x1F800000)  
3. **Phase 3**: Connect memory control registers to RAM behavior
4. **Phase 4**: Add write queue differentiation for cached/uncached regions
5. **Phase 5**: Enhanced exception handling and BadVaddr support

## Files to Modify

- `src/interconnect.c` - Update physical address handling for 8MB mirror
- `include/interconnect.h` - Add scratchpad definitions and structures  
- `src/interconnect.c` - Add scratchpad load/store functions
- Memory control integration in interconnect mapping logic
- Exception handling updates in CPU module for proper BadVaddr

## PSX-SPX Compliance Summary

**Current Compliance**: ~70%
- ✅ Basic 2MB RAM with correct addressing
- ✅ Virtual-to-physical address translation  
- ✅ Multi-byte access functions
- ❌ Missing RAM mirroring (8MB total address space)
- ❌ Missing scratchpad (1KB fast RAM)
- ❌ Incomplete memory control register integration
- ❌ Limited write queue behavior differentiation

This analysis provides a clear roadmap for making our RAM implementation fully PSX-SPX compliant and compatible with all PlayStation software.