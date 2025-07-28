# ZoniStationOne Development Guidelines

## Project Overview

ZoniStationOne is a PlayStation 1 emulator focusing on accuracy and clean code architecture. The project prioritizes MIPS R3000A hardware accuracy over legacy compatibility.

## Architecture Principles

### **1. Hardware Accuracy First**
- **MIPS R3000A**: Implement according to actual hardware specifications
- **Little-Endian**: Use native little-endian mode for all operations
- **Memory Map**: Follow exact PlayStation memory layout
- **Timing**: Respect original hardware timing constraints

### **2. Clean Code Architecture**
- **Modular Design**: Separate components with clear interfaces
- **Error Handling**: Comprehensive error checking and reporting
- **Documentation**: Detailed comments and documentation
- **Testing**: Thorough testing of each component

### **3. Endianness Guidelines**

#### **✅ Correct Approach (ZoniStationOne)**
```c
// Always use little-endian for MIPS R3000A
u32 value = zoni_read_le32(&memory[address]);
zoni_write_le32(&memory[address], value);

// Use raw instruction data for decoding
u8 opcode = (instruction->raw >> 26) & 0x3F;
```

#### **❌ Avoid (PCSX-ReARMed Legacy)**
```c
// Don't use big-endian conversion for MIPS R3000A
u32 value = SWAPu32(*(u32*)memory);  // Wrong for MIPS
```

## Coding Standards

### **1. Memory Access**
```c
// ✅ Correct: Use endianness helper functions
zoni_error_t zoni_memory_read32(zoni_memory_t* memory, u32 address, u32* value) {
    // Validate address first
    if (!zoni_memory_validate_address(memory, address)) {
        return ZONI_ERROR_INVALID_ADDRESS;
    }
    
    // Use little-endian helper
    *value = zoni_read_le32(&reg->data[offset]);
    return ZONI_SUCCESS;
}
```

### **2. Instruction Decoding**
```c
// ✅ Correct: Use raw instruction data
zoni_error_t zoni_cpu_execute_instruction(zoni_cpu_regs_t* cpu, zoni_instruction_t* instruction) {
    // Extract opcode directly from raw instruction
    u8 opcode = (instruction->raw >> 26) & 0x3F;
    u8 funct = instruction->raw & 0x3F;
    
    // No endianness conversion needed
    switch (opcode) {
        case MIPS_OP_SW:
            return zoni_cpu_execute_sw(cpu, instruction);
    }
}
```

### **3. Field Extraction**
```c
// ✅ Correct: Manual bit extraction
zoni_error_t zoni_cpu_execute_sw(zoni_cpu_regs_t* cpu, zoni_instruction_t* instruction) {
    // Extract fields manually from raw instruction
    u8 rt = (instruction->raw >> 16) & 0x1F;        // bits 16-20
    u8 rs = (instruction->raw >> 21) & 0x1F;        // bits 21-25
    s16 offset = (s16)(instruction->raw & 0xFFFF);  // bits 0-15
    
    // Calculate address and execute
    u32 address = cpu->gpr.r[rs] + offset;
    return zoni_cpu_write32(cpu, address, cpu->gpr.r[rt]);
}
```

### **4. Error Handling**
```c
// ✅ Correct: Comprehensive error handling
zoni_error_t zoni_cpu_write32(zoni_cpu_regs_t* cpu, u32 address, u32 value) {
    if (!cpu || !g_memory) {
        return ZONI_ERROR_INVALID_PARAMETER;
    }
    
    zoni_error_t result = zoni_memory_write32(g_memory, address, value);
    if (result != ZONI_SUCCESS) {
        zoni_log(ZONI_LOG_ERROR, "Failed to write32 at address 0x%08X", address);
        return result;
    }
    
    return ZONI_SUCCESS;
}
```

### **5. Logging**
```c
// ✅ Correct: Clear, informative logging
zoni_log(ZONI_LOG_DEBUG, "SW Memory[0x%08X + %d] = Memory[0x%08X] = $%d = 0x%08X", 
         rs_val, offset, address, rt, value);
```

## Development Workflow

### **1. Component Development**
1. **Research**: Understand hardware specifications
2. **Design**: Plan clean interface and implementation
3. **Implement**: Write code with proper error handling
4. **Test**: Thorough testing with edge cases
5. **Document**: Update documentation and comments

### **2. Testing Strategy**
```c
// Test each component thoroughly
void test_memory_system() {
    // Test valid addresses
    assert(zoni_memory_read32(memory, 0x1000, &value) == ZONI_SUCCESS);
    
    // Test invalid addresses
    assert(zoni_memory_read32(memory, 0x56780200, &value) == ZONI_ERROR_INVALID_ADDRESS);
    
    // Test endianness
    zoni_memory_write32(memory, 0x1000, 0x12345678);
    zoni_memory_read32(memory, 0x1000, &value);
    assert(value == 0x12345678);  // Should be little-endian
}
```

### **3. Documentation Updates**
- Update `PROJECT_STATUS.md` with current status
- Document any endianness decisions in `docs/ENDIANNESS_ANALYSIS.md`
- Keep `DEVELOPMENT.md` current with best practices
- Update code comments for complex logic

## Current Status

### **✅ Completed**
- **Memory System**: Little-endian implementation with proper validation
- **CPU Core**: Basic MIPS R3000A instruction set
- **Endianness**: Correct little-endian implementation
- **Error Handling**: Comprehensive validation and reporting

### **📋 Next Stages**
1. **BIOS Loading**: Load and execute PlayStation BIOS
2. **GPU Emulation**: Basic graphics processing
3. **SPU Emulation**: Audio processing
4. **CD-ROM**: Disc emulation
5. **Input/Output**: Controller and memory card support

## Best Practices

### **1. Endianness**
- **Always use little-endian** for MIPS R3000A operations
- **Use helper functions** from `zoni_endian.h`
- **Avoid endianness conversion** unless absolutely necessary
- **Document endianness decisions** clearly

### **2. Memory Management**
- **Validate addresses** before access
- **Use proper error codes** for different failure types
- **Log errors clearly** for debugging
- **Follow PlayStation memory map** exactly

### **3. Instruction Implementation**
- **Use raw instruction data** for decoding
- **Manual bit extraction** for field decoding
- **Test with real instruction encodings**
- **Verify against MIPS specification**

### **4. Code Quality**
- **Comprehensive error handling**
- **Clear, informative logging**
- **Detailed code comments**
- **Consistent naming conventions**

## Common Pitfalls

### **❌ Avoid These Mistakes**

1. **Endianness Confusion**
   ```c
   // ❌ Wrong: Using big-endian for MIPS R3000A
   u32 value = (bytes[3] << 24) | (bytes[2] << 16) | (bytes[1] << 8) | bytes[0];
   ```

2. **Unsafe Memory Access**
   ```c
   // ❌ Wrong: No address validation
   u32 value = *(u32*)&memory[address];
   ```

3. **Poor Error Handling**
   ```c
   // ❌ Wrong: Silent failures
   memory_write(address, value);  // No error checking
   ```

4. **Inconsistent Logging**
   ```c
   // ❌ Wrong: Unclear debug messages
   printf("x=%x y=%x", x, y);  // What do x and y represent?
   ```

### **✅ Do This Instead**

1. **Correct Endianness**
   ```c
   // ✅ Right: Use little-endian helper
   u32 value = zoni_read_le32(&memory[address]);
   ```

2. **Safe Memory Access**
   ```c
   // ✅ Right: Validate address first
   if (!zoni_memory_validate_address(memory, address)) {
       return ZONI_ERROR_INVALID_ADDRESS;
   }
   u32 value = zoni_read_le32(&memory[address]);
   ```

3. **Comprehensive Error Handling**
   ```c
   // ✅ Right: Check all error conditions
   zoni_error_t result = zoni_memory_write32(memory, address, value);
   if (result != ZONI_SUCCESS) {
       zoni_log(ZONI_LOG_ERROR, "Memory write failed: %s", zoni_error_string(result));
       return result;
   }
   ```

4. **Clear Logging**
   ```c
   // ✅ Right: Descriptive debug messages
   zoni_log(ZONI_LOG_DEBUG, "SW $%d = Memory[0x%08X + %d] = 0x%08X", 
            rt, rs_val, offset, value);
   ```

## Conclusion

Follow these guidelines to maintain code quality and hardware accuracy. The endianness corrections have established a solid foundation for accurate PlayStation 1 emulation. Continue to prioritize MIPS R3000A accuracy and clean code architecture in all future development. 