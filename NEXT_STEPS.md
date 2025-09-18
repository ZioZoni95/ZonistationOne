# ZonistationOne - Next Steps Quick Reference

## 🚀 Immediate Next Actions (Phase 1 Start)

### Step 1: Set Up CPU Instruction Framework
Create the instruction decoding and execution system:

```c
// In src/core/cpu.c - Add instruction decoding
typedef struct {
    uint32_t opcode;
    uint32_t rs, rt, rd;
    uint32_t immediate;
    uint32_t address;
} mips_instruction_t;

// Add instruction decoding function
mips_instruction_t decode_instruction(uint32_t word);

// Add instruction execution dispatch
int execute_instruction(psx_cpu_t *cpu, mips_instruction_t instr);
```

### Step 2: Implement Basic Load/Store Instructions
Start with the most critical instructions:

1. **LW (Load Word)** - `lw $rt, offset($rs)`
2. **SW (Store Word)** - `sw $rt, offset($rs)`
3. **ADDIU (Add Immediate Unsigned)** - `addiu $rt, $rs, immediate`
4. **BEQ (Branch Equal)** - `beq $rs, $rt, offset`
5. **J (Jump)** - `j address`

### Step 3: Test with Simple BIOS Boot
Verify CPU can execute BIOS initialization code.

## 📋 Development Priorities

### Week 1-2: Core Instructions
- [ ] Instruction fetch from memory
- [ ] Decode 32-bit MIPS instructions
- [ ] Implement load/store instructions (LW, SW, LB, LBU, LH, LHU, SB, SH)
- [ ] Basic arithmetic (ADD, ADDIU, SUB, SUBU)
- [ ] Simple branches (BEQ, BNE)

### Week 3-4: Control Flow
- [ ] Jump instructions (J, JAL, JR, JALR)
- [ ] Branch delay slots
- [ ] All branch instructions
- [ ] Exception handling basics

### Week 5-6: Advanced Instructions
- [ ] Multiply/divide (MULT, DIV, MFHI, MFLO)
- [ ] Logical operations (AND, OR, XOR, shifts)
- [ ] Coprocessor 0 basics
- [ ] System calls and exceptions

## 🔧 Key Files to Modify

### Primary Files
- `src/core/cpu.c` - Main CPU implementation
- `src/core/cpu.h` - CPU interface updates
- `src/core/memory.c` - Memory access optimizations

### Test Files to Create
- `tests/cpu_tests.c` - Unit tests for CPU instructions
- `tests/integration_tests.c` - Full system tests

## 📚 Reference Materials

### MIPS R3000A Documentation
- MIPS R3000 RISC Processor User's Manual
- PlayStation Technical Reference Manual
- PCSX Redux CPU implementation (`src/core/psxinterpreter.cc`)

### Instruction Format Reference
```
R-Type: [op:6][rs:5][rt:5][rd:5][shamt:5][funct:6]
I-Type: [op:6][rs:5][rt:5][immediate:16]
J-Type: [op:6][address:26]
```

### Essential Opcodes
- `0x23` - LW (Load Word)
- `0x2B` - SW (Store Word)  
- `0x09` - ADDIU (Add Immediate Unsigned)
- `0x04` - BEQ (Branch Equal)
- `0x02` - J (Jump)

## 🎯 Success Criteria Phase 1

### Minimum Viable CPU
- [ ] Can fetch instructions from memory
- [ ] Executes basic arithmetic operations
- [ ] Handles simple control flow
- [ ] Can boot BIOS to first instruction sequence

### Testing Goals
- [ ] Unit tests pass for all implemented instructions
- [ ] BIOS boots without crashing
- [ ] PC advances correctly through code
- [ ] Register values update properly

## 🚨 Common Pitfalls to Avoid

1. **Endianness Issues**: PlayStation is little-endian
2. **Delay Slots**: Branches/jumps execute next instruction first
3. **Address Alignment**: Memory accesses must be properly aligned
4. **Sign Extension**: Be careful with immediate values and shifts
5. **Register 0**: Always reads as zero, writes are ignored

## 💡 Development Tips

### Debugging Strategy
- Log every instruction execution initially
- Compare register states with known good emulator
- Test with simple homebrew programs first
- Use hardware documentation extensively

### Code Organization
- Keep instruction implementations in separate functions
- Use lookup tables for opcode dispatch
- Maintain clear separation between fetch/decode/execute
- Add comprehensive error checking

### Performance Considerations
- Don't optimize prematurely
- Focus on correctness first
- Profile after basic functionality works
- Consider instruction caching later

---

## 🎮 Ready to Start!

Your foundation is solid - the modular architecture, memory system, and logging infrastructure provide everything needed to implement the CPU. Start with the basic instruction set and build up gradually. The roadmap provides the long-term vision, but focus on getting those first instructions working correctly.

**First goal**: Make the CPU execute a simple sequence of LW, ADDIU, SW instructions successfully!

Good luck with your PlayStation One emulator development! 🚀