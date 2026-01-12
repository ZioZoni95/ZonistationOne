# CPU Refactoring Checklist

## ✅ Phase 1: Architecture Design (COMPLETE)

- [x] Design modular structure (5 modules)
- [x] Define module responsibilities
- [x] Document dependency graph
- [x] Create architecture diagrams
- [x] Plan file organization

## ✅ Phase 2: Header Files (COMPLETE - 100%)

- [x] Create `include/cpu/cpu_types.h` (73 lines)
  - [x] Type definitions (RegisterIndex, ICacheLine, Cpu)
  - [x] Enums (BootStage, ExceptionCause)
  - [x] Instruction decoding helpers (10 inline functions)
  - [x] Disassembly function prototype

- [x] Create `include/cpu/cpu_cache.h` (40 lines)
  - [x] Cache constants (ICACHE_NUM_LINES, etc.)
  - [x] ICacheLine struct definition
  - [x] Function prototypes (fetch, clear)

- [x] Create `include/cpu/cpu_exceptions.h` (26 lines)
  - [x] Exception handling prototype (cpu_exception)
  - [x] BIOS syscall handler prototype

- [x] Create `include/cpu/cpu_instructions.h` (83 lines)
  - [x] Instruction decoder prototype
  - [x] All 64+ instruction handler prototypes
  - [x] Organized by category (arithmetic, branches, etc.)

- [x] Create `include/cpu/cpu_core.h` (77 lines)
  - [x] Complete Cpu struct definition (60+ fields)
  - [x] Core operation prototypes (init, run, reg access)
  - [x] Include all required dependencies

- [x] Create `include/cpu_modular.h` (9 lines)
  - [x] Convenience wrapper including all CPU headers

## ✅ Phase 3: Implementation - Foundation (COMPLETE - 40%)

- [x] Create `src/cpu/cpu_types.c` (225 lines)
  - [x] BIOS A-function name lookup (52 functions)
  - [x] BIOS B-function name lookup (20 functions)
  - [x] BIOS C-function name lookup (17 functions)
  - [x] Full MIPS disassembler (60+ opcodes)
  - [x] Register name table
  - [x] Format strings for all instruction types

- [x] Create `src/cpu/cpu_cache.c` (101 lines)
  - [x] cpu_icache_fetch() - Main fetch function
    - [x] KSEG1 bypass check
    - [x] Physical address calculation
    - [x] Tag/line/word extraction
    - [x] Cache hit path
    - [x] Cache miss path (fetch words N-3)
    - [x] Partial line invalidation
  - [x] cpu_icache_clear() - Cache invalidation

## ⏳ Phase 4: Implementation - Core Logic (PENDING - 60%)

### cpu_exceptions.c (~200 lines) - Priority: HIGH

- [ ] Create file with proper includes
- [ ] Extract helper functions (mark as `static`):
  - [ ] `log_exception_details()` - Exception diagnostics
  - [ ] `update_status_register()` - SR mode stack push
  - [ ] `update_cause_and_epc()` - Cause register and EPC calculation
  - [ ] `acknowledge_interrupts()` - IRQ acknowledgment
  - [ ] `get_exception_vector()` - Vector address calculation
- [ ] Extract public functions:
  - [ ] `cpu_exception()` - Main exception entry point
  - [ ] `handle_bios_syscall()` - BIOS A/B/C-function dispatch
- [ ] Verify all includes (interconnect.h, timers.h, log.h, spu.h)
- [ ] Test compilation
- [ ] Verify exception handling works

### cpu_instructions.c (~1200 lines) - Priority: HIGH

- [ ] Create file with proper includes
- [ ] Extract decoder:
  - [ ] `decode_and_execute()` - Primary/secondary opcode dispatch
- [ ] Extract instruction handlers (mark as `static`):
  - [ ] Basic instructions (20 handlers):
    - [ ] `op_lui` - Load upper immediate
    - [ ] `op_ori` - Or immediate
    - [ ] `op_sw` - Store word
    - [ ] `op_sll` - Shift left logical
    - [ ] `op_addiu` - Add immediate unsigned
    - [ ] `op_j` - Jump
    - [ ] `op_or` - Bitwise or
    - [ ] `op_cop0` - Coprocessor 0 dispatcher
    - [ ] `op_mtc0` - Move to COP0
    - [ ] `op_rfe` - Return from exception
    - [ ] `op_bne` - Branch not equal
    - [ ] `op_addi` - Add immediate (signed)
    - [ ] `op_lw` - Load word
    - [ ] `op_sltu` - Set less than unsigned
    - [ ] `op_addu` - Add unsigned
    - [ ] `op_sh` - Store halfword
    - [ ] `op_jal` - Jump and link
    - [ ] `op_andi` - And immediate
    - [ ] `op_sb` - Store byte
    - [ ] `op_jr` - Jump register
  - [ ] Load instructions (7 handlers):
    - [ ] `op_lb` - Load byte signed
    - [ ] `op_lbu` - Load byte unsigned
    - [ ] `op_lh` - Load halfword signed
    - [ ] `op_lhu` - Load halfword unsigned
    - [ ] `op_lwl` - Load word left
    - [ ] `op_lwr` - Load word right
  - [ ] Store instructions (2 handlers):
    - [ ] `op_swl` - Store word left
    - [ ] `op_swr` - Store word right
  - [ ] Arithmetic instructions (4 handlers):
    - [ ] `op_add` - Add (signed, overflow check)
    - [ ] `op_sub` - Subtract (signed, overflow check)
    - [ ] `op_mult` - Multiply signed
    - [ ] `op_multu` - Multiply unsigned
  - [ ] Divide instructions (2 handlers):
    - [ ] `op_div` - Divide signed
    - [ ] `op_divu` - Divide unsigned
  - [ ] Shift instructions (5 handlers):
    - [ ] `op_srl` - Shift right logical
    - [ ] `op_sra` - Shift right arithmetic
    - [ ] `op_sllv` - Shift left logical variable
    - [ ] `op_srlv` - Shift right logical variable
    - [ ] `op_srav` - Shift right arithmetic variable
  - [ ] Comparison instructions (3 handlers):
    - [ ] `op_slt` - Set less than signed
    - [ ] `op_slti` - Set less than immediate
    - [ ] `op_sltiu` - Set less than immediate unsigned
  - [ ] Branch instructions (4 handlers):
    - [ ] `op_beq` - Branch if equal
    - [ ] `op_bgtz` - Branch if greater than zero
    - [ ] `op_blez` - Branch if less than or equal zero
    - [ ] `op_bxx` - BLTZ/BGEZ/BLTZAL/BGEZAL
  - [ ] Jump instructions (1 handler):
    - [ ] `op_jalr` - Jump and link register
  - [ ] HI/LO instructions (4 handlers):
    - [ ] `op_mfhi` - Move from HI
    - [ ] `op_mflo` - Move from LO
    - [ ] `op_mthi` - Move to HI
    - [ ] `op_mtlo` - Move to LO
  - [ ] Coprocessor instructions (11 handlers):
    - [ ] `op_mfc0` - Move from COP0
    - [ ] `op_cop1` - COP1 (triggers exception)
    - [ ] `op_cop2` - COP2 (GTE)
    - [ ] `op_cop3` - COP3 (triggers exception)
    - [ ] `op_lwc0` - Load word COP0 (exception)
    - [ ] `op_lwc1` - Load word COP1 (exception)
    - [ ] `op_lwc2` - Load word COP2 (GTE)
    - [ ] `op_lwc3` - Load word COP3 (exception)
    - [ ] `op_swc0` - Store word COP0 (exception)
    - [ ] `op_swc1` - Store word COP1 (exception)
    - [ ] `op_swc2` - Store word COP2 (GTE)
    - [ ] `op_swc3` - Store word COP3 (exception)
  - [ ] System instructions (3 handlers):
    - [ ] `op_syscall` - System call
    - [ ] `op_break` - Breakpoint
    - [ ] `op_illegal` - Illegal instruction handler
  - [ ] Logical instructions (3 handlers):
    - [ ] `op_and` - Bitwise and
    - [ ] `op_xor` - Bitwise xor
    - [ ] `op_xori` - Xor immediate
    - [ ] `op_nor` - Bitwise nor
- [ ] Verify all includes (gte.h, spu.h)
- [ ] Test compilation
- [ ] Verify instruction execution works

### cpu_core.c (~600 lines) - Priority: CRITICAL

- [ ] Create file with proper includes
- [ ] Extract initialization:
  - [ ] `cpu_init()` - Power-on state initialization
- [ ] Extract register access:
  - [ ] `cpu_reg()` - Read GPR
  - [ ] `cpu_set_reg()` - Write GPR (handles R0)
  - [ ] `cpu_branch()` - Calculate branch target
- [ ] Extract main loop:
  - [ ] `cpu_run_next_instruction()` - Main execution cycle
    - [ ] Instruction counter & diagnostics
    - [ ] BIOS region tracking
    - [ ] Boot stage detection
    - [ ] Patch verification bypass
    - [ ] IRQ checking
    - [ ] Load delay handling
    - [ ] Instruction fetch (via cpu_icache_fetch)
    - [ ] Delay slot state management
    - [ ] PC advancement
    - [ ] Register commit
    - [ ] Instruction decode (via decode_and_execute)
    - [ ] Event system integration
- [ ] Verify all includes (all CPU headers, interconnect.h, gte.h)
- [ ] Test compilation
- [ ] Verify execution loop works

## 🔧 Phase 5: Integration (PENDING)

- [ ] Update Makefile
  - [ ] Remove `src/cpu.c` from EMU_SRCS
  - [ ] Add `src/cpu/cpu_types.c`
  - [ ] Add `src/cpu/cpu_cache.c`
  - [ ] Add `src/cpu/cpu_exceptions.c`
  - [ ] Add `src/cpu/cpu_instructions.c`
  - [ ] Add `src/cpu/cpu_core.c`
  - [ ] Verify include paths (-Iinclude)

- [ ] Update include strategy
  - [ ] Option A: Keep `include/cpu.h` as compatibility wrapper
  - [ ] Option B: Update `src/main.c` to use `cpu/cpu_core.h`
  - [ ] Decide on migration approach

- [ ] Backup original files
  - [ ] Copy `src/cpu.c` to `src/cpu.c.backup`
  - [ ] Copy `include/cpu.h` to `include/cpu.h.backup`

## 🧪 Phase 6: Testing (PENDING)

### Compilation Testing
- [ ] Clean build: `make clean`
- [ ] Compile: `make`
- [ ] Verify no warnings
- [ ] Verify no errors
- [ ] Check all modules compiled:
  - [ ] cpu_types.o
  - [ ] cpu_cache.o
  - [ ] cpu_exceptions.o
  - [ ] cpu_instructions.o
  - [ ] cpu_core.o
- [ ] Check linking successful
- [ ] Verify executable created

### Functional Testing
- [ ] Run emulator with BIOS
- [ ] Verify no segfaults
- [ ] Check BIOS boots to Sony logo
- [ ] Check logo animation plays
- [ ] Check BIOS reaches menu
- [ ] Compare log output with original
- [ ] Verify boot stages detected correctly
- [ ] Check interrupt handling works
- [ ] Verify exception handling works

### Regression Testing
- [ ] Compare instruction count with original
- [ ] Compare memory state at key points
- [ ] Verify cache hit/miss behavior unchanged
- [ ] Check register values match
- [ ] Verify COP0 state identical
- [ ] Test all instruction types execute correctly
- [ ] Verify exception handling identical

### Performance Testing
- [ ] Benchmark execution speed
- [ ] Compare with original cpu.c performance
- [ ] Profile hotspots
- [ ] Verify no performance regression

## 📖 Phase 7: Documentation (PARTIAL)

### Completed Documentation
- [x] CPU_REFACTORING_PLAN.md - Overall strategy
- [x] CPU_MODULAR_PROGRESS.md - Status tracking
- [x] CPU_REFACTORING_COMPLETION.md - Step-by-step guide
- [x] CPU_REFACTORING_SUMMARY.md - High-level overview
- [x] CPU_ARCHITECTURE_DIAGRAM.md - Visual diagrams
- [x] CPU_REFACTORING_CHECKLIST.md (this file)

### Remaining Documentation
- [ ] Add Doxygen comments to all functions
- [ ] Document each module's public API
- [ ] Create module interaction examples
- [ ] Write testing guide
- [ ] Document debugging tips
- [ ] Create troubleshooting guide

## 🎯 Phase 8: Validation (PENDING)

- [ ] Code review each module
- [ ] Verify coding style consistency
- [ ] Check for memory leaks
- [ ] Verify all resources freed
- [ ] Check error handling complete
- [ ] Verify no undefined behavior
- [ ] Static analysis (cppcheck, clang-analyzer)
- [ ] Dynamic analysis (valgrind)

## 🚀 Phase 9: Deployment (PENDING)

- [ ] Tag release in git
- [ ] Update CHANGELOG.md
- [ ] Update README.md with new structure
- [ ] Archive original cpu.c
- [ ] Clean up backup files
- [ ] Update build documentation
- [ ] Announce refactoring completion

## 📊 Progress Summary

### Overall Progress: 47% Complete

| Phase | Status | Progress |
|-------|--------|----------|
| 1. Architecture Design | ✅ Complete | 100% |
| 2. Header Files | ✅ Complete | 100% |
| 3. Implementation (Foundation) | ✅ Complete | 40% (2/5 files) |
| 4. Implementation (Core) | ⏳ Pending | 0% (3/5 files) |
| 5. Integration | ⏳ Pending | 0% |
| 6. Testing | ⏳ Pending | 0% |
| 7. Documentation | ⏳ Partial | 60% |
| 8. Validation | ⏳ Pending | 0% |
| 9. Deployment | ⏳ Pending | 0% |

### File Progress: 7/10 Complete (70%)

| File | Lines | Status |
|------|-------|--------|
| cpu_types.h | 73 | ✅ Complete |
| cpu_cache.h | 40 | ✅ Complete |
| cpu_exceptions.h | 26 | ✅ Complete |
| cpu_instructions.h | 83 | ✅ Complete |
| cpu_core.h | 77 | ✅ Complete |
| cpu_types.c | 225 | ✅ Complete |
| cpu_cache.c | 101 | ✅ Complete |
| cpu_exceptions.c | ~200 | ⏳ Pending |
| cpu_instructions.c | ~1200 | ⏳ Pending |
| cpu_core.c | ~600 | ⏳ Pending |

### Code Progress: ~625/~2625 Lines (24%)

- Headers: 299/299 lines (100%) ✅
- Source: 326/2326 lines (14%) ⏳
  - cpu_types.c: 225/225 (100%) ✅
  - cpu_cache.c: 101/101 (100%) ✅
  - cpu_exceptions.c: 0/200 (0%) ⏳
  - cpu_instructions.c: 0/1200 (0%) ⏳
  - cpu_core.c: 0/600 (0%) ⏳

## 🎯 Next Immediate Actions

### Priority 1: Complete cpu_exceptions.c (Est: 30 minutes)
1. Create src/cpu/cpu_exceptions.c
2. Add header includes
3. Extract helper functions from cpu.c lines 390-485
4. Extract cpu_exception() from cpu.c lines 487-510
5. Extract handle_bios_syscall() from cpu.c lines 365-420
6. Mark helpers as static
7. Test compilation

### Priority 2: Complete cpu_instructions.c (Est: 60 minutes)
1. Create src/cpu/cpu_instructions.c
2. Add header includes
3. Extract decode_and_execute() from cpu.c lines 1110-1290
4. Extract all 64+ op_xxx() handlers from cpu.c lines 1290-2175
5. Mark handlers as static (except decode_and_execute)
6. Test compilation

### Priority 3: Complete cpu_core.c (Est: 45 minutes)
1. Create src/cpu/cpu_core.c
2. Add header includes
3. Extract cpu_init() from cpu.c lines 210-255
4. Extract cpu_reg/cpu_set_reg/cpu_branch from cpu.c lines 260-295
5. Extract cpu_run_next_instruction() from cpu.c lines 505-1000
6. All functions public (no static)
7. Test compilation

### Priority 4: Update Build System (Est: 10 minutes)
1. Update Makefile EMU_SRCS
2. Remove src/cpu.c
3. Add 5 new src/cpu/*.c files
4. Test make clean && make

### Priority 5: Testing (Est: 30 minutes)
1. Run emulator
2. Verify BIOS boots
3. Compare with original behavior
4. Fix any issues

## 📝 Notes

### Important Reminders
- Keep original cpu.c backed up until testing complete
- Mark helper functions as `static`
- Mark public API functions without `static`
- Include only what each module needs
- Test after each file is created
- Commit frequently

### Common Pitfalls to Avoid
- ❌ Forgetting to mark helpers as static
- ❌ Missing includes in new files
- ❌ Circular dependencies between modules
- ❌ Forgetting to update Makefile
- ❌ Not testing incrementally
- ❌ Deleting original files before testing

### Success Criteria
- ✅ Clean compilation (no warnings)
- ✅ Clean linking (no undefined refs)
- ✅ BIOS boots to menu
- ✅ No functional changes
- ✅ Easier to maintain
- ✅ Ready for future enhancements

---

**Status**: Phase 3 complete (47%), Phase 4 in progress
**Next**: Extract cpu_exceptions.c (Priority 1)
**Goal**: 100% completion with fully functional modular CPU
