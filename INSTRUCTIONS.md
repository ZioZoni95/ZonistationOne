# MIPS R3000A Instruction Reference 📖

> **ZonistationOne MIPS Implementation Guide**  
> This document details all MIPS R3000A instructions and their implementation status in ZonistationOne.

---

## 📋 Implementation Status Overview

| Status | Count | Percentage | Description |
|--------|-------|------------|-------------|
| ✅ **Implemented** | 8 | 13% | Working and verified with BIOS |
| 🔄 **In Progress** | 8 | 13% | Currently being implemented |
| ⏳ **Planned** | 29 | 48% | Planned for Phase 3-4 |
| 📋 **Future** | 15 | 25% | Advanced features (Phase 5+) |
| **Total** | **60** | **100%** | Core MIPS R3000A instruction set |

---

## ✅ Implemented Instructions (BIOS Verified)

### Immediate Operations
| Instruction | Opcode | Format | Status | BIOS Test |
|-------------|---------|---------|---------|-----------|
| **LUI** rt, immediate | 0x0F | I-Type | ✅ Working | ✅ Verified |
| **ORI** rt, rs, immediate | 0x0D | I-Type | ✅ Working | ✅ Verified |
| **ADDIU** rt, rs, immediate | 0x09 | I-Type | ✅ Working | ✅ Verified |

**LUI (Load Upper Immediate)** - Loads 16-bit immediate into upper half of register
```cpp
// BIOS Example: LUI R8, 0x0013 -> R8 = 0x00130000
void CPU::handleLUI(const InstructionInfo& info) {
    if (info.rt == 0) return; // Can't write to R0
    uint32_t value = _ImmLU_(info.code); // Shift to upper 16 bits
    setRegister(info.rt, value);
}
```

**ORI (OR Immediate)** - Bitwise OR with zero-extended immediate
```cpp
// BIOS Example: ORI R8, R8, 0x243f -> 0x00130000 | 0x243f = 0x0013243f  
void CPU::handleORI(const InstructionInfo& info) {
    if (info.rt == 0) return;
    uint32_t rsValue = getRegister(info.rs);
    uint32_t result = rsValue | info.immU;
    setRegister(info.rt, result);
}
```

**ADDIU (Add Immediate Unsigned)** - Add sign-extended immediate
```cpp
// BIOS Example: ADDIU R8, R0, 2952 -> 0x00000000 + 2952 = 0x00000b88
void CPU::handleADDIU(const InstructionInfo& info) {
    if (info.rt == 0) return;
    uint32_t rsValue = getRegister(info.rs);
    uint32_t result = rsValue + static_cast<uint32_t>(info.imm);
    setRegister(info.rt, result);
}
```

### Memory Operations
| Instruction | Opcode | Format | Status | BIOS Test |
|-------------|---------|---------|---------|-----------|
| **SW** rt, offset(rs) | 0x2B | I-Type | ✅ Working | ✅ Verified |
| **LW** rt, offset(rs) | 0x23 | I-Type | ✅ Working | ✅ Implemented |

**SW (Store Word)** - Store 32-bit word to memory
```cpp
// BIOS Example: SW R8, 4112(R1) [0x1f801010] = 0x0013243f
void CPU::handleSW(const InstructionInfo& info) {
    uint32_t address = getRegister(info.rs) + static_cast<uint32_t>(info.imm);
    uint32_t value = getRegister(info.rt);
    
    if (address & 0x3) { // Check 4-byte alignment
        ZONI_LOG_ERROR(CPU, "Unaligned store word access at 0x%08x", address);
        m_halted = true;
        return;
    }
    
    if (m_memory) {
        m_memory->write32(address, value);
    }
}
```

### Register Operations (SPECIAL)
| Instruction | Funct | Format | Status | Notes |
|-------------|-------|---------|---------|-------|
| **OR** rd, rs, rt | 0x25 | R-Type | ✅ Working | Bitwise OR |
| **ADDU** rd, rs, rt | 0x21 | R-Type | ✅ Working | Add unsigned |

**OR (Bitwise OR)** - Register-to-register OR operation
```cpp
void CPU::handleOR(const InstructionInfo& info) {
    if (info.rd == 0) return;
    uint32_t result = getRegister(info.rs) | getRegister(info.rt);
    setRegister(info.rd, result);
}
```

### Special Cases
| Instruction | Code | Status | Notes |
|-------------|------|---------|-------|
| **NOP** | 0x00000000 | ✅ Working | No operation (SLL R0, R0, 0) |

---

## 🔄 Currently Being Implemented

### Control Flow Instructions
| Instruction | Opcode | Format | Priority | Notes |
|-------------|---------|---------|----------|-------|
| **J** target | 0x02 | J-Type | 🔥 High | Unconditional jump |
| **JAL** target | 0x03 | J-Type | 🔥 High | Jump and link |
| **BEQ** rs, rt, offset | 0x04 | I-Type | 🔥 High | Branch if equal |
| **BNE** rs, rt, offset | 0x05 | I-Type | 🔥 High | Branch if not equal |
| **BGTZ** rs, offset | 0x07 | I-Type | 📋 Medium | Branch if greater than zero |
| **BLEZ** rs, offset | 0x06 | I-Type | 📋 Medium | Branch if less/equal zero |
| **JR** rs | S-0x08 | R-Type | 🔥 High | Jump register |
| **JALR** rd, rs | S-0x09 | R-Type | 📋 Medium | Jump and link register |

**Implementation Templates Ready:**

**J (Jump)** - Unconditional jump to target address
```cpp
void CPU::handleJ(const InstructionInfo& info) {
    uint32_t target = _JumpTarget_(info.code, m_pc);
    // TODO: Handle delay slot
    m_nextPC = target;
}
```

**BEQ (Branch if Equal)** - Conditional branch
```cpp
void CPU::handleBEQ(const InstructionInfo& info) {
    uint32_t rsValue = getRegister(info.rs);
    uint32_t rtValue = getRegister(info.rt);
    
    if (rsValue == rtValue) {
        uint32_t target = _BranchTarget_(info.code, m_pc);
        // TODO: Handle delay slot
        m_nextPC = target;
    }
}
```

---

## ⏳ Planned Instructions (Phase 3)

### Arithmetic & Logical Operations
| Instruction | Opcode/Funct | Format | Priority | Notes |
|-------------|---------------|---------|----------|-------|
| **ADDI** rt, rs, imm | 0x08 | I-Type | 📋 Medium | Add immediate (with overflow) |
| **ANDI** rt, rs, imm | 0x0C | I-Type | 📋 Medium | AND immediate |
| **XORI** rt, rs, imm | 0x0E | I-Type | 📋 Medium | XOR immediate |
| **SLTI** rt, rs, imm | 0x0A | I-Type | 📋 Medium | Set less than immediate |
| **SLTIU** rt, rs, imm | 0x0B | I-Type | 📋 Medium | Set less than immediate unsigned |

### Register Arithmetic (SPECIAL)
| Instruction | Funct | Format | Priority | Notes |
|-------------|--------|---------|----------|-------|
| **ADD** rd, rs, rt | 0x20 | R-Type | 📋 Medium | Add (with overflow exception) |
| **SUB** rd, rs, rt | 0x22 | R-Type | 📋 Medium | Subtract (with overflow) |
| **SUBU** rd, rs, rt | 0x23 | R-Type | 🔥 High | Subtract unsigned |
| **AND** rd, rs, rt | 0x24 | R-Type | 📋 Medium | Bitwise AND |
| **XOR** rd, rs, rt | 0x26 | R-Type | 📋 Medium | Bitwise XOR |
| **NOR** rd, rs, rt | 0x27 | R-Type | 📋 Medium | Bitwise NOR |
| **SLT** rd, rs, rt | 0x2A | R-Type | 📋 Medium | Set less than (signed) |
| **SLTU** rd, rs, rt | 0x2B | R-Type | 📋 Medium | Set less than unsigned |

### Shift Operations (SPECIAL)
| Instruction | Funct | Format | Priority | Notes |
|-------------|--------|---------|----------|-------|
| **SLL** rd, rt, sa | 0x00 | R-Type | 🔥 High | Shift left logical |
| **SRL** rd, rt, sa | 0x02 | R-Type | 📋 Medium | Shift right logical |
| **SRA** rd, rt, sa | 0x03 | R-Type | 📋 Medium | Shift right arithmetic |
| **SLLV** rd, rt, rs | 0x04 | R-Type | 📋 Low | Shift left logical variable |
| **SRLV** rd, rt, rs | 0x06 | R-Type | 📋 Low | Shift right logical variable |
| **SRAV** rd, rt, rs | 0x07 | R-Type | 📋 Low | Shift right arithmetic variable |

### Memory Operations
| Instruction | Opcode | Format | Priority | Notes |
|-------------|---------|---------|----------|-------|
| **LB** rt, offset(rs) | 0x20 | I-Type | 🔥 High | Load byte (sign-extended) |
| **LBU** rt, offset(rs) | 0x24 | I-Type | 🔥 High | Load byte unsigned |
| **LH** rt, offset(rs) | 0x21 | I-Type | 📋 Medium | Load halfword (sign-extended) |
| **LHU** rt, offset(rs) | 0x25 | I-Type | 📋 Medium | Load halfword unsigned |
| **SB** rt, offset(rs) | 0x28 | I-Type | 🔥 High | Store byte |
| **SH** rt, offset(rs) | 0x29 | I-Type | 📋 Medium | Store halfword |

---

## 📋 Advanced Instructions (Phase 4)

### Multiply/Divide Operations (SPECIAL)
| Instruction | Funct | Format | Notes |
|-------------|--------|---------|-------|
| **MULT** rs, rt | 0x18 | R-Type | Multiply (signed) |
| **MULTU** rs, rt | 0x19 | R-Type | Multiply unsigned |
| **DIV** rs, rt | 0x1A | R-Type | Divide (signed) |
| **DIVU** rs, rt | 0x1B | R-Type | Divide unsigned |
| **MFHI** rd | 0x10 | R-Type | Move from HI register |
| **MTHI** rs | 0x11 | R-Type | Move to HI register |
| **MFLO** rd | 0x12 | R-Type | Move from LO register |
| **MTLO** rs | 0x13 | R-Type | Move to LO register |

### System Instructions (SPECIAL)
| Instruction | Funct | Format | Notes |
|-------------|--------|---------|-------|
| **SYSCALL** | 0x0C | R-Type | System call exception |
| **BREAK** | 0x0D | R-Type | Breakpoint exception |

### Branch Instructions (REGIMM)
| Instruction | rt | Format | Notes |
|-------------|-----|---------|-------|
| **BLTZ** rs, offset | 0x00 | I-Type | Branch if less than zero |
| **BGEZ** rs, offset | 0x01 | I-Type | Branch if greater/equal zero |
| **BLTZAL** rs, offset | 0x10 | I-Type | Branch less than zero and link |
| **BGEZAL** rs, offset | 0x11 | I-Type | Branch greater/equal zero and link |

---

## 🎯 Implementation Priority Matrix

### Phase 3A: Essential Control Flow (Week 5)
```
Priority: 🔥 CRITICAL - Needed for BIOS boot sequence
- J (Jump) - 0x02
- JAL (Jump and Link) - 0x03  
- BEQ (Branch Equal) - 0x04
- BNE (Branch Not Equal) - 0x05
- JR (Jump Register) - S-0x08
```

### Phase 3B: Core Arithmetic (Week 5-6)
```
Priority: 🔥 HIGH - Common BIOS operations
- SUBU (Subtract Unsigned) - S-0x23
- AND (Bitwise AND) - S-0x24
- SLL (Shift Left Logical) - S-0x00
- LB (Load Byte) - 0x20
- SB (Store Byte) - 0x28
```

### Phase 3C: Extended Operations (Week 6)
```
Priority: 📋 MEDIUM - Additional functionality
- ADDI (Add Immediate) - 0x08
- ANDI (AND Immediate) - 0x0C
- SLTI (Set Less Than Immediate) - 0x0A
- SRL (Shift Right Logical) - S-0x02
- SRA (Shift Right Arithmetic) - S-0x03
```

---

## 🔧 Implementation Guidelines

### Instruction Handler Template
```cpp
void CPU::handleINSTRUCTION_NAME(const InstructionInfo& info) {
    // 1. Input validation
    if (info.rd == 0) return; // Can't write to register 0 (for R-type)
    
    // 2. Extract operands
    uint32_t rsValue = getRegister(info.rs);
    uint32_t rtValue = getRegister(info.rt);
    
    // 3. Perform operation
    uint32_t result = /* operation logic */;
    
    // 4. Handle special cases (overflow, alignment, etc.)
    if (/* error condition */) {
        // Generate appropriate exception
        return;
    }
    
    // 5. Log operation (if enabled)
    ZONI_LOG_CPU_INSTRUCTION("INSTRUCTION_NAME ...", /* args */);
    
    // 6. Update CPU state
    setRegister(info.rd, result);
    
    // 7. Handle timing/delays if needed
}
```

### Testing Procedure for Each Instruction
1. **Unit Test**: Verify instruction behavior in isolation
2. **BIOS Test**: Confirm instruction works in BIOS context
3. **Edge Cases**: Test boundary conditions and error cases
4. **Integration Test**: Verify with other instructions
5. **Performance Test**: Measure execution speed

### Error Handling Patterns
```cpp
// Alignment check for memory operations
if (address & 0x3) {
    ZONI_LOG_ERROR(CPU, "Unaligned access at 0x%08x", address);
    // TODO: Generate LoadAddressError exception
    m_halted = true;
    return;
}

// Overflow detection for arithmetic
if (/* overflow condition */) {
    ZONI_LOG_WARN(CPU, "Arithmetic overflow in instruction");
    // TODO: Generate ArithmeticOverflow exception
    return;
}

// Register 0 protection
if (targetRegister == 0) {
    return; // Register 0 is always zero, ignore writes
}
```

---

## 📊 BIOS Instruction Frequency Analysis

Based on BIOS execution traces, most frequently encountered instructions:

| Rank | Instruction | Frequency | Importance |
|------|-------------|-----------|------------|
| 1 | **LUI** | Very High | ✅ Implemented |
| 2 | **ORI** | Very High | ✅ Implemented |
| 3 | **SW** | Very High | ✅ Implemented |
| 4 | **ADDIU** | High | ✅ Implemented |
| 5 | **BEQ** | High | 🔄 In Progress |
| 6 | **J** | High | 🔄 In Progress |
| 7 | **JAL** | High | 🔄 In Progress |
| 8 | **LW** | High | ✅ Implemented |
| 9 | **SUBU** | Medium | ⏳ Planned |
| 10 | **SLL** | Medium | ⏳ Planned |

---

## 🎮 PlayStation-Specific Implementation Notes

### Register Usage Conventions
- **R0**: Always zero (hardwired)
- **R29 (SP)**: Stack pointer - requires special handling
- **R31 (RA)**: Return address - used by JAL/JALR

### Memory Access Patterns
- **I/O Registers**: 0x1F801000-0x1F801FFF require special handling
- **BIOS ROM**: 0x1FC00000-0x1FC7FFFF (read-only)
- **Main RAM**: 0x00000000-0x001FFFFF (mirrors at other addresses)

### Timing Considerations
- **Load Delay Slot**: LW/LH/LB have 1-cycle delay
- **Branch Delay Slot**: All branches have 1-instruction delay
- **Multiply/Divide**: Multi-cycle operations

---

**ZonistationOne MIPS Team**  
*One instruction at a time, one cycle at a time* ⚡✨

---

> **Next Update**: This document will be updated as Phase 3 instructions are implemented, with detailed implementation notes and BIOS verification results.