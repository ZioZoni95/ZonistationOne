# CPU Refactoring Completion Guide

## Overview
This guide provides step-by-step instructions to complete the CPU refactoring from the current 47% completion to 100%.

## Current Status
✅ **Completed** (100%):
- All 5 header files (324 lines)
- cpu_types.c (225 lines) - Disassembler + BIOS function names
- cpu_cache.c (101 lines) - I-cache fetch logic
- cpu_exceptions.c (323 lines) - Exception handling + BIOS syscalls
- cpu_instructions.c (1004 lines) - All 60+ instruction handlers  
- cpu_core.c (600+ lines) - Main execution loop
- Makefile updated to use modular CPU
- cpu.h converted to compatibility wrapper (25 lines)
- Tested and verified - BIOS boots successfully

⏳ **Remaining** (0%):
- None! Refactoring complete ✅

## Step-by-Step Completion Plan

### Step 1: Create cpu_exceptions.c (~30 minutes)

**File**: `src/cpu/cpu_exceptions.c`

**Extract from cpu.c**:
1. Lines 390-410: `log_exception_details()` function
2. Lines 412-425: `update_status_register()` function
3. Lines 427-450: `update_cause_and_epc()` function
4. Lines 452-480: `acknowledge_interrupts()` function
5. Lines 482-485: `get_exception_vector()` function
6. Lines 487-510: `cpu_exception()` function
7. Lines 365-420: `handle_bios_syscall()` function

**Header includes**:
```c
#include "cpu/cpu_exceptions.h"
#include "cpu/cpu_core.h"
#include "cpu/cpu_cache.h"
#include "interconnect.h"
#include "timers.h"
#include "log.h"
#include "spu.h"
#include <stdlib.h>
```

**Mark functions as `static` except**:
- `cpu_exception()` - PUBLIC
- `handle_bios_syscall()` - PUBLIC

### Step 2: Create cpu_instructions.c (~60 minutes)

**File**: `src/cpu/cpu_instructions.c`

**Extract from cpu.c**:
1. Lines 1110-1290: `decode_and_execute()` function
2. Lines 1290-2175: All 64+ `op_xxx()` instruction handlers

**Header includes**:
```c
#include "cpu/cpu_instructions.h"
#include "cpu/cpu_core.h"
#include "cpu/cpu_cache.h"
#include "cpu/cpu_exceptions.h"
#include "interconnect.h"
#include "gte.h"
#include "log.h"
#include "spu.h"
```

**Mark functions as `static` except**:
- `decode_and_execute()` - PUBLIC

**Instruction handlers** (all `static`):
```c
static void op_lui(Cpu* cpu, uint32_t instruction);
static void op_ori(Cpu* cpu, uint32_t instruction);
// ... (62 more handlers)
```

### Step 3: Create cpu_core.c (~45 minutes)

**File**: `src/cpu/cpu_core.c`

**Extract from cpu.c**:
1. Lines 210-255: `cpu_init()` function
2. Lines 260-270: `cpu_reg()` function
3. Lines 275-285: `cpu_set_reg()` function
4. Lines 290-295: `cpu_branch()` function
5. Lines 505-1000: `cpu_run_next_instruction()` function

**Header includes**:
```c
#include "cpu/cpu_core.h"
#include "cpu/cpu_cache.h"
#include "cpu/cpu_exceptions.h"
#include "cpu/cpu_instructions.h"
#include "interconnect.h"
#include "gte.h"
#include "log.h"
#include <string.h>
#include <stdio.h>
```

**All functions are PUBLIC** (no static):
- `cpu_init()`
- `cpu_reg()`
- `cpu_set_reg()`
- `cpu_branch()`
- `cpu_run_next_instruction()`

### Step 4: Update Makefile (~5 minutes)

**File**: `Makefile`

**Find the `EMU_SRCS` variable** and replace:
```makefile
# OLD:
EMU_SRCS = src/main.c \
           src/cpu.c \
           src/interconnect.c \
           ...

# NEW:
EMU_SRCS = src/main.c \
           src/cpu/cpu_types.c \
           src/cpu/cpu_cache.c \
           src/cpu/cpu_exceptions.c \
           src/cpu/cpu_instructions.c \
           src/cpu/cpu_core.c \
           src/interconnect.c \
           ...
```

**Update include path** (if not already present):
```makefile
CFLAGS += -Iinclude
```

### Step 5: Update Main Entry Point (~2 minutes)

**File**: `include/cpu.h`

**At the top, add**:
```c
// New modular CPU architecture
#include "cpu/cpu_core.h"

// For compatibility, keep old includes
// (can be removed after testing)
```

**OR create a migration strategy**:
- Keep old `cpu.h` for now
- Update `main.c` to include `cpu/cpu_core.h` instead
- Gradually migrate other files

### Step 6: Backup and Remove Original (~2 minutes)

**Important**: Backup first!

```bash
# Backup original
cp src/cpu.c src/cpu.c.backup
cp include/cpu.h include/cpu.h.backup

# After testing, remove or archive
mv src/cpu.c.backup archive/cpu.c.original
```

### Step 7: Compile and Test (~10 minutes)

```bash
# Clean build
make clean

# Compile
make

# Should see:
#   CC  src/cpu/cpu_types.c
#   CC  src/cpu/cpu_cache.c
#   CC  src/cpu/cpu_exceptions.c
#   CC  src/cpu/cpu_instructions.c
#   CC  src/cpu/cpu_core.c
#   LD  myps1_emu

# Test run
./myps1_emu path/to/bios.bin

# Verify BIOS boots to menu
```

### Step 8: Regression Testing (~20 minutes)

**Test Cases**:
1. ✅ BIOS boots to Sony logo
2. ✅ BIOS reaches menu screen
3. ✅ No crashes or exceptions
4. ✅ Log output matches original
5. ✅ Instruction count matches
6. ✅ Memory state matches

**Verification Script** (optional):
```bash
#!/bin/bash
# Test original vs refactored

echo "Testing original cpu.c..."
make clean && make CPU_SRC=src/cpu.c
./myps1_emu bios.bin > output_original.log 2>&1

echo "Testing refactored CPU..."
make clean && make CPU_SRC=src/cpu/cpu_*.c
./myps1_emu bios.bin > output_refactored.log 2>&1

echo "Comparing outputs..."
diff output_original.log output_refactored.log
```

## Common Issues and Solutions

### Issue 1: Undefined Reference Errors

**Symptom**: `undefined reference to 'cpu_exception'`

**Solution**: Check function visibility (static vs public)
- Public functions: Declared in header, implemented without `static`
- Private helpers: Implemented with `static`, not in header

### Issue 2: Circular Dependencies

**Symptom**: Compilation fails with circular include errors

**Solution**: Use forward declarations
```c
// In header file
typedef struct Cpu Cpu;  // Forward declaration
```

### Issue 3: Missing Includes

**Symptom**: `unknown type name 'uint32_t'`

**Solution**: Add required includes
```c
#include <stdint.h>
#include <stdbool.h>
```

### Issue 4: Function Not Found

**Symptom**: Linker can't find `get_bios_a_function_name`

**Solution**: These are `static` helper functions, must be in same file as caller
- Move BIOS function name helpers to `cpu_exceptions.c` if called from there
- OR expose them in `cpu_types.h` if used by multiple modules

## Automated Extraction Script

For faster extraction, you can use this Python script:

```python
#!/usr/bin/env python3
"""
Extract functions from cpu.c to modular files
"""

def extract_function(source, func_name, start_line, end_line):
    """Extract a function by line numbers"""
    lines = source.split('\n')
    return '\n'.join(lines[start_line:end_line])

# Read original
with open('src/cpu.c', 'r') as f:
    original = f.read()

# Extract exceptions
exceptions = []
exceptions.append(extract_function(original, 'log_exception_details', 390, 410))
exceptions.append(extract_function(original, 'update_status_register', 412, 425))
# ... etc

# Write to file
with open('src/cpu/cpu_exceptions.c', 'w') as f:
    f.write('#include "cpu/cpu_exceptions.h"\n')
    f.write('#include "cpu/cpu_core.h"\n\n')
    f.write('\n\n'.join(exceptions))
```

## Verification Checklist

After refactoring, verify:

- [ ] All files compile without errors
- [ ] All files link without undefined references
- [ ] BIOS boots successfully
- [ ] No segfaults or crashes
- [ ] Log output unchanged
- [ ] Instruction count matches original
- [ ] Performance is equivalent

## Timeline Estimate

| Task | Estimated Time | Cumulative |
|------|----------------|------------|
| Create cpu_exceptions.c | 30 min | 30 min |
| Create cpu_instructions.c | 60 min | 90 min |
| Create cpu_core.c | 45 min | 135 min |
| Update Makefile | 5 min | 140 min |
| Update includes | 5 min | 145 min |
| First compilation | 10 min | 155 min |
| Fix compile errors | 20 min | 175 min |
| Regression testing | 20 min | 195 min |
| Documentation update | 15 min | 210 min |

**Total Estimated Time**: ~3.5 hours

## Success Criteria

The refactoring is complete when:

1. ✅ All 5 source files created
2. ✅ All 5 header files working
3. ✅ Makefile updated and working
4. ✅ Clean compilation (no warnings)
5. ✅ Clean linking (no undefined refs)
6. ✅ BIOS boots to menu
7. ✅ No functional regressions
8. ✅ Code is more maintainable
9. ✅ Documentation updated
10. ✅ Original files backed up

## Next Steps After Completion

Once refactoring is complete:

1. **Code Review**: Review all modules for correctness
2. **Performance Test**: Benchmark against original
3. **Documentation**: Add detailed comments to each module
4. **Unit Tests**: Write tests for each module
5. **Enhancement Planning**: Plan cycle counting, JIT, etc.

## Quick Reference: File Locations

```
ZonistationOne/
├── include/
│   ├── cpu.h (legacy, keep for compatibility)
│   └── cpu/
│       ├── cpu_types.h       ✅ DONE
│       ├── cpu_cache.h       ✅ DONE
│       ├── cpu_exceptions.h  ✅ DONE
│       ├── cpu_instructions.h✅ DONE
│       └── cpu_core.h        ✅ DONE
├── src/
│   ├── cpu.c (backup after refactoring)
│   └── cpu/
│       ├── cpu_types.c       ✅ DONE
│       ├── cpu_cache.c       ✅ DONE
│       ├── cpu_exceptions.c  ⏳ TODO
│       ├── cpu_instructions.c⏳ TODO
│       └── cpu_core.c        ⏳ TODO
└── docs/
    ├── CPU_REFACTORING_PLAN.md
    ├── CPU_MODULAR_PROGRESS.md
    └── CPU_REFACTORING_COMPLETION.md (this file)
```

---

**Last Updated**: January 7, 2026
**Status**: ✅ COMPLETE - All modules extracted, compiled, and tested
**Completion**: 100% (all 5 modules + wrapper + Makefile)
