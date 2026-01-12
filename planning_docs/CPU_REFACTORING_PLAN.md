# CPU Refactoring Plan

## Overview
Refactoring monolithic `cpu.c` (2108 lines) into a modular DuckStation-inspired architecture.

## Directory Structure
```
include/cpu/
  ├── cpu_types.h      - Type definitions, enums, instruction helpers [DONE]
  ├── cpu_cache.h      - I-cache interface [DONE]
  ├── cpu_exceptions.h - Exception handling interface [DONE]
  ├── cpu_instructions.h - Instruction handler prototypes [DONE]
  └── cpu_core.h       - Main CPU state and core operations [DONE]

src/cpu/
  ├── cpu_types.c      - Disassembler implementation
  ├── cpu_cache.c      - I-cache fetch logic
  ├── cpu_exceptions.c - Exception handling, BIOS syscalls
  ├── cpu_instructions.c - All 60+ instruction handlers
  └── cpu_core.c       - Main execution loop, init, register access
```

## Code Split Breakdown

### cpu_types.c (~250 lines)
- `disassemble_mips()` function (lines 15-200 of current cpu.c)
- `get_bios_a_function_name()` (lines 290-310)
- `get_bios_b_function_name()` (lines 315-335)
- `get_bios_c_function_name()` (lines 340-360)

### cpu_cache.c (~100 lines)
- `cpu_icache_fetch()` function (lines 1010-1110 of current cpu.c)
- `cpu_icache_clear()` helper (new function)

### cpu_exceptions.c (~200 lines)
- `cpu_exception()` function (lines 425-500)
- `handle_bios_syscall()` (lines 365-420)
- Helper functions:
  - `log_exception_details()` (lines 390-410)
  - `update_status_register()` (lines 412-425)
  - `update_cause_and_epc()` (lines 427-450)
  - `acknowledge_interrupts()` (lines 452-480)
  - `get_exception_vector()` (lines 482-485)

### cpu_instructions.c (~1200 lines)
All `op_xxx()` instruction handlers (lines 1150-2175):
- Arithmetic: `op_add`, `op_addu`, `op_sub`, `op_subu`, `op_addi`, `op_addiu`
- Logical: `op_and`, `op_or`, `op_xor`, `op_nor`, `op_andi`, `op_ori`, `op_xori`
- Shifts: `op_sll`, `op_srl`, `op_sra`, `op_sllv`, `op_srlv`, `op_srav`
- Comparisons: `op_slt`, `op_sltu`, `op_slti`, `op_sltiu`
- Branches: `op_beq`, `op_bne`, `op_blez`, `op_bgtz`, `op_bxx`
- Jumps: `op_j`, `op_jal`, `op_jr`, `op_jalr`
- Loads: `op_lb`, `op_lh`, `op_lw`, `op_lbu`, `op_lhu`, `op_lwl`, `op_lwr`
- Stores: `op_sb`, `op_sh`, `op_sw`, `op_swl`, `op_swr`
- Multiply/Divide: `op_mult`, `op_multu`, `op_div`, `op_divu`
- HI/LO: `op_mfhi`, `op_mflo`, `op_mthi`, `op_mtlo`
- Coprocessor: `op_cop0`, `op_cop1`, `op_cop2`, `op_cop3`
- COP0 ops: `op_mfc0`, `op_mtc0`, `op_rfe`
- COP load/store: `op_lwc0-3`, `op_swc0-3`
- System: `op_syscall`, `op_break`, `op_illegal`
- Decoder: `decode_and_execute()` function (lines 1110-1150)

### cpu_core.c (~400 lines)
- `cpu_init()` (lines 210-250)
- `cpu_run_next_instruction()` - main execution loop (lines 505-1010)
- `cpu_reg()` (lines 260-270)
- `cpu_set_reg()` (lines 275-285)
- `cpu_branch()` (lines 290-295)

## Migration Strategy

1. ✅ Create all header files with proper declarations
2. Create source files in dependency order:
   - `cpu_types.c` (no dependencies)
   - `cpu_cache.c` (depends on types, interconnect)
   - `cpu_exceptions.c` (depends on types, cache)
   - `cpu_instructions.c` (depends on types, cache, exceptions)
   - `cpu_core.c` (depends on all above)
3. Update `Makefile` to include new source files
4. Update `include/cpu.h` to include modular headers
5. Test compilation
6. Test BIOS boot to verify functionality preserved

## Benefits
- **Maintainability**: Each module has clear responsibility
- **Scalability**: Easy to add features like JIT compilation
- **Readability**: Smaller files easier to navigate
- **Testing**: Can unit test individual modules
- **Collaboration**: Multiple developers can work on different modules

## Compatibility Notes
- All existing code preserved (no functional changes)
- Same instruction implementations
- Same exception handling logic
- Same cache behavior
- Binary compatibility maintained

## Next Steps for Enhancement (Post-Refactor)
Once refactored, we can enhance each module:
1. **cpu_core.c**: Add cycle counting (downcount/pending_ticks)
2. **cpu_instructions.c**: Add MULT/DIV/GTE stall tracking
3. **cpu_cache.c**: Add cache statistics, improve accuracy
4. **cpu_exceptions.c**: Add dual load delay slots
5. New file: **cpu_jit.c** for JIT compilation (future)
