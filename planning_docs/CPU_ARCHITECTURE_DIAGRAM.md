# CPU Modular Architecture Diagram

## High-Level Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                      ZonistationOne                          │
│                     PS1 Emulator Core                        │
└─────────────────────────────────────────────────────────────┘
                            │
                ┌───────────┴───────────┐
                │                       │
┌───────────────▼──────────────┐  ┌────▼────────────────┐
│     Interconnect             │  │   Main Loop         │
│  (Memory Bus/IO)             │  │  (Event System)     │
└──────────┬───────────────────┘  └─────────────────────┘
           │
    ┌──────┴──────────────────────────────┐
    │         CPU Module                  │
    │      (Modular Architecture)         │
    └──────┬──────────────────────────────┘
           │
           │
    ┏━━━━━┷━━━━━━━━━━━━━━━━━━━━━━━━━━━━┓
    ┃     CPU MODULAR STRUCTURE          ┃
    ┃                                    ┃
    ┃  ┌────────────────────────────┐   ┃
    ┃  │    cpu_types.h/c           │   ┃  ✅ DONE
    ┃  │  (Type Definitions)        │   ┃
    ┃  │  - Enums (BootStage, etc)  │   ┃
    ┃  │  - Instruction helpers     │   ┃
    ┃  │  - Disassembler            │   ┃
    ┃  │  - BIOS function names     │   ┃
    ┃  └────────────────────────────┘   ┃
    ┃           │                        ┃
    ┃           ▼                        ┃
    ┃  ┌────────────────────────────┐   ┃
    ┃  │    cpu_cache.h/c           │   ┃  ✅ DONE
    ┃  │  (I-Cache Management)      │   ┃
    ┃  │  - Cache fetch logic       │   ┃
    ┃  │  - Hit/miss handling       │   ┃
    ┃  │  - KSEG1 bypass            │   ┃
    ┃  └────────────────────────────┘   ┃
    ┃           │                        ┃
    ┃           ▼                        ┃
    ┃  ┌────────────────────────────┐   ┃
    ┃  │  cpu_exceptions.h/c        │   ┃  ⏳ PENDING
    ┃  │  (Exception Handling)      │   ┃
    ┃  │  - Exception entry         │   ┃
    ┃  │  - BIOS syscalls           │   ┃
    ┃  │  - SR/Cause/EPC updates    │   ┃
    ┃  └────────────────────────────┘   ┃
    ┃           │                        ┃
    ┃           ▼                        ┃
    ┃  ┌────────────────────────────┐   ┃
    ┃  │  cpu_instructions.h/c      │   ┃  ⏳ PENDING
    ┃  │  (Instruction Set)         │   ┃
    ┃  │  - Decoder                 │   ┃
    ┃  │  - 64+ op_xxx() handlers   │   ┃
    ┃  │  - Load/Store/Arith/Branch │   ┃
    ┃  └────────────────────────────┘   ┃
    ┃           │                        ┃
    ┃           ▼                        ┃
    ┃  ┌────────────────────────────┐   ┃
    ┃  │    cpu_core.h/c            │   ┃  ⏳ PENDING
    ┃  │  (Execution Engine)        │   ┃
    ┃  │  - Main loop               │   ┃
    ┃  │  - Initialization          │   ┃
    ┃  │  - Register access         │   ┃
    ┃  │  - Event integration       │   ┃
    ┃  └────────────────────────────┘   ┃
    ┃                                    ┃
    ┗━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┛
```

## Module Dependency Graph

```
┌─────────────────┐
│  cpu_types.c    │  ← Base types, no dependencies
│  (225 lines)    │
└────────┬────────┘
         │ uses
         ▼
┌─────────────────┐
│  cpu_cache.c    │  ← Needs: interconnect.h, cpu_types.h
│  (101 lines)    │
└────────┬────────┘
         │ uses
         ▼
┌─────────────────┐
│cpu_exceptions.c │  ← Needs: cpu_cache.h, interconnect.h, timers.h
│  (~200 lines)   │
└────────┬────────┘
         │ uses
         ▼
┌─────────────────┐
│cpu_instructions.│  ← Needs: cpu_exceptions.h, gte.h, spu.h
│c (~1200 lines)  │
└────────┬────────┘
         │ uses
         ▼
┌─────────────────┐
│  cpu_core.c     │  ← Needs: ALL above modules
│  (~600 lines)   │
└─────────────────┘
```

## Data Flow During Instruction Execution

```
cpu_run_next_instruction() in cpu_core.c
    │
    ├──► 1. Check interrupts (I_STAT & I_MASK)
    │       └─► cpu_exception() if IRQ pending
    │
    ├──► 2. Apply load delay from previous cycle
    │       └─► cpu_set_reg(load_reg_idx, load_value)
    │
    ├──► 3. Fetch instruction
    │       └─► cpu_icache_fetch(pc) in cpu_cache.c
    │               ├─► Check KSEG1 (bypass)
    │               ├─► Cache lookup (tag + valid)
    │               └─► Fetch on miss
    │
    ├──► 4. Update delay slot state
    │       └─► in_delay_slot = branch_taken
    │
    ├──► 5. Advance PC
    │       ├─► pc = next_pc
    │       └─► next_pc = pc + 4
    │
    ├──► 6. Commit registers
    │       └─► memcpy(regs, out_regs)
    │
    ├──► 7. Execute instruction
    │       └─► decode_and_execute(instruction) in cpu_instructions.c
    │               ├─► Primary opcode switch (bits 26-31)
    │               ├─► Secondary opcode switch (bits 0-5)
    │               └─► Call op_xxx() handler
    │                       ├─► op_lui, op_ori, op_sw, etc.
    │                       ├─► May call cpu_exception()
    │                       ├─► May set branch_taken = true
    │                       └─► May schedule load delay
    │
    └──► 8. Event system update
            └─► eventq_dispatch_due() if cycle reached
```

## Exception Flow

```
Instruction raises exception (e.g., SYSCALL, overflow)
    │
    ▼
cpu_exception(cause) in cpu_exceptions.c
    │
    ├──► log_exception_details()
    │       └─► Log PC, SR, EPC, Cause, BadVaddr
    │
    ├──► update_status_register()
    │       └─► Push mode stack (SR bits 0-5)
    │            bit0-1 → bit2-3
    │            bit2-3 → bit4-5
    │            Set EXL=1
    │
    ├──► update_cause_and_epc()
    │       ├─► Set exception code (bits 2-6)
    │       ├─► Set IP bit 10 if IRQ
    │       └─► Calculate EPC:
    │            if (in_delay_slot) {
    │                EPC = current_pc - 4
    │                Set BD bit (bit 31)
    │            } else {
    │                EPC = current_pc
    │            }
    │
    ├──► acknowledge_interrupts() [if IRQ]
    │       └─► Check I_STAT & I_MASK
    │            GTE interrupt quirk: skip GTE instruction
    │
    ├──► get_exception_vector()
    │       └─► BEV=1 → 0xbfc00180 (ROM)
    │            BEV=0 → 0x80000080 (RAM)
    │
    └──► Jump to handler
            pc = handler_addr
            next_pc = pc + 4
```

## Cache Architecture

```
I-Cache: 4KB total
├─► 256 lines (ICACHE_NUM_LINES)
│   └─► Each line: 4 words (ICACHE_LINE_WORDS)
│       └─► Each word: 4 bytes (16 bytes per line)
│
└─► Per line:
    ├─► tag (uint32_t) - Physical address bits 31:12
    ├─► valid[4] (bool) - Valid bit per word
    └─► data[4] (uint32_t) - Cached instruction words

Fetch Process:
    vaddr (e.g., 0x80001000)
    │
    ├─► Check KSEG1? (bits 31-29 = 101)
    │   └─► YES: Bypass cache, fetch from interconnect
    │   └─► NO: Continue to cache lookup
    │
    ├─► Convert to physical: paddr = mask_region(vaddr)
    │   (e.g., 0x80001000 → 0x00001000)
    │
    ├─► Extract components:
    │   ├─► tag = paddr >> 12 (bits 31:12)
    │   ├─► line_index = (paddr >> 4) & 0xFF (bits 11:4)
    │   └─► word_index = (paddr >> 2) & 0x3 (bits 3:2)
    │
    ├─► Lookup: icache[line_index]
    │   ├─► Check: line.tag == tag?
    │   └─► Check: line.valid[word_index]?
    │
    ├─► HIT: Return line.data[word_index]
    │
    └─► MISS:
        ├─► Update line.tag = tag
        ├─► Invalidate words 0..word_index-1
        └─► Fetch words word_index..3 from memory
            └─► Return line.data[word_index]
```

## Boot Stage State Machine

```
POWER_ON
    │ PC=0xBFC00000
    ▼
BIOS_INIT (ROM Kernel: 0xBFC00000-0xBFC18000)
    │ Decompress intro
    ▼
LOGO_ANIMATION (RAM Intro: 0x80030000-0x80040000)
    │ Show Sony logo
    ▼
PATCH_CHECK (0x80059DC0-0x80059E20)
    │ Verify game patches
    ▼
CDROM_CHECK (0x80059E20-0x80060000)
    │ Detect CD-ROM
    ▼
WAITING_INPUT (0x80060000-0x80070000)
    │ Idle loop
    ▼
BIOS_MENU (0x80030000-0x80040000)
    │ Memory Card / CD-ROM selection
    ▼
GAME_BOOT (0x80010000-0x80030000)
    │ Load game executable
    ▼
GAME_RUNNING (0x80100000-0x801F0000)
    │ Active gameplay
    ▼
```

## Register File Architecture

```
Load Delay Slot Mechanism:

Cycle N:
    regs[32]     - Input registers (read by instructions)
    out_regs[32] - Output registers (written by instructions)
    load_reg_idx - Target register of pending load
    load_value   - Value to be loaded

    Instruction executes:
    - Reads from regs[]
    - Writes to out_regs[] via cpu_set_reg()
    - Load instructions set load_reg_idx and load_value

Cycle N+1:
    1. Apply delayed load: out_regs[load_reg_idx] = load_value
    2. Commit registers: memcpy(regs, out_regs)
    3. Reset load delay: load_reg_idx = REG_ZERO
    4. Execute next instruction

Example:
    LW $2, 0($3)    # Cycle N: Load word into $2
                    # Sets load_reg_idx=2, load_value=mem[R3]
                    # $2 NOT updated yet
    
    ADD $4, $2, $5  # Cycle N+1: Add $2 + $5 → $4
                    # Reads OLD value of $2 (from regs[])
                    # At start of cycle, load applied: out_regs[2]=load_value
                    # At end of cycle, commit: regs[2]=out_regs[2]
    
    OR $6, $2, $7   # Cycle N+2: Or $2 | $7 → $6
                    # Now sees NEW value of $2 (from previous load)
```

## Future Extension Points

```
Current Modules:
├─► cpu_types.c     (foundation)
├─► cpu_cache.c     (memory interface)
├─► cpu_exceptions.c (exception handling)
├─► cpu_instructions.c (interpreter)
└─► cpu_core.c      (execution engine)

Future Additions:
├─► cpu_jit.c       (JIT recompiler)
│   ├─► Block translation
│   ├─► Register allocation
│   └─► x86_64/ARM64 codegen
│
├─► cpu_debugger.c  (debugging tools)
│   ├─► Breakpoints
│   ├─► Watchpoints
│   ├─► Step execution
│   └─► Register inspection
│
├─► cpu_recompiler.c (AOT compiler)
│   ├─► Profile-guided optimization
│   ├─► Hot path detection
│   └─► Cached block translation
│
└─► cpu_analysis.c  (code analysis)
    ├─► Control flow graph
    ├─► Dead code detection
    └─► Optimization hints
```

## Comparison: Monolithic vs Modular

```
BEFORE (Monolithic):
┌────────────────────────────────┐
│         cpu.c                  │
│       (2175 lines)             │
│                                │
│  ┌──────────────────────────┐ │
│  │ Disassembler (200 lines) │ │
│  ├──────────────────────────┤ │
│  │ I-Cache (100 lines)      │ │
│  ├──────────────────────────┤ │
│  │ Exceptions (200 lines)   │ │
│  ├──────────────────────────┤ │
│  │ Instructions (1200 lines)│ │
│  ├──────────────────────────┤ │
│  │ Core Loop (475 lines)    │ │
│  └──────────────────────────┘ │
│                                │
│  Everything tightly coupled!   │
└────────────────────────────────┘

AFTER (Modular):
┌─────────────────────────────────────────────┐
│  cpu_types.c    │  cpu_cache.c              │
│  (225 lines)    │  (101 lines)              │
│  ✅ Independent  │  ✅ Testable              │
└─────────────────────────────────────────────┘
┌─────────────────────────────────────────────┐
│  cpu_exceptions.c │ cpu_instructions.c      │
│  (~200 lines)     │ (~1200 lines)           │
│  ⏳ Pending       │ ⏳ Pending                │
└─────────────────────────────────────────────┘
┌─────────────────────────────────────────────┐
│  cpu_core.c                                 │
│  (~600 lines)                               │
│  ⏳ Pending                                  │
└─────────────────────────────────────────────┘
         Each module can evolve independently!
```

---

**Legend**:
- ✅ = Complete and tested
- ⏳ = Pending extraction from cpu.c
- 📦 = Future enhancement module

**Note**: This diagram shows the logical architecture. Physical files are in `include/cpu/` and `src/cpu/` directories.
