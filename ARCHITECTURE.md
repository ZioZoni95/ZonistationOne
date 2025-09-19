# ZonistationOne Technical Architecture 🏗️

> **Version**: 1.0  
> **Status**: Active Development  
> **Last Updated**: September 2025

This document provides a comprehensive overview of ZonistationOne's technical architecture, design decisions, and implementation details.

---

## 🎯 Architecture Overview

ZonistationOne follows a **modular, component-based architecture** inspired by PCSX-Redux but implemented with modern C++20 practices. The design prioritizes **accuracy**, **maintainability**, and **performance**.

```
┌─────────────────────────────────────────────────────────┐
│                    Application Layer                    │
├─────────────────────────────────────────────────────────┤
│  Main Loop  │  CLI Interface  │  Configuration Manager  │
├─────────────────────────────────────────────────────────┤
│                   Emulator Core                         │
├─────────────┬─────────────┬─────────────┬───────────────┤
│    CPU      │   Memory    │     GPU     │      SPU      │
│  R3000A     │  Manager    │   Engine    │   Processor   │
├─────────────┼─────────────┼─────────────┼───────────────┤
│   CDROM     │   Debug     │   Logger    │   Utils       │
│ Controller  │  Framework  │   System    │   Library     │
└─────────────────────────────────────────────────────────┘
```

---

## 🧠 Core Components

### 1. CPU Subsystem (MIPS R3000A)

#### Design Philosophy
- **Interpreter-first approach**: Prioritize accuracy over speed initially
- **Instruction-level emulation**: Bit-perfect MIPS R3000A behavior
- **PCSX-Redux patterns**: Proven dispatch table architecture
- **Modern C++**: Type-safe enums, RAII, smart pointers

#### Key Classes

**`CPU`** - Main processor class
```cpp
class CPU {
private:
    // Register file and state
    uint32_t m_registers[32];        // General purpose registers
    uint32_t m_pc, m_nextPC;         // Program counters
    uint32_t m_hi, m_lo;             // Multiply/divide results
    uint32_t m_cop0_registers[32];   // Coprocessor 0 registers
    
    // Instruction dispatch system
    typedef void (CPU::*InstructionHandler)(const InstructionInfo& info);
    static const InstructionHandler s_primaryHandlers[64];
    static const InstructionHandler s_specialHandlers[64];
    
    // Core execution
    void executeInstruction(uint32_t instruction);
    uint32_t fetchInstruction();
};
```

**`InstructionInfo`** - Decoded instruction data
```cpp
struct InstructionInfo {
    uint32_t code;                   // Raw instruction word
    Opcode opcode;                   // Primary opcode
    InstructionFormat format;        // R/I/J type
    uint32_t rs, rt, rd, sa;        // Register fields
    int32_t imm;                    // Sign-extended immediate
    uint32_t immU;                  // Zero-extended immediate
    uint32_t target;                // Jump target
    SpecialFunct specialFunct;      // SPECIAL function code
    RegimmRt regimmRt;              // REGIMM type field
};
```

#### Instruction Dispatch Architecture

**Primary Dispatch Table** (64 entries)
```cpp
const CPU::InstructionHandler s_primaryHandlers[64] = {
    &CPU::handleSPECIAL,    // 0x00 - SPECIAL functions
    &CPU::handleREGIMM,     // 0x01 - Branch/trap functions  
    &CPU::handleJ,          // 0x02 - Jump
    &CPU::handleJAL,        // 0x03 - Jump and link
    // ... remaining 60 primary opcodes
};
```

**SPECIAL Dispatch Table** (64 entries)
```cpp
const CPU::InstructionHandler s_specialHandlers[64] = {
    &CPU::handleSLL,        // 0x00 - Shift left logical
    nullptr,                // 0x01 - Reserved
    &CPU::handleSRL,        // 0x02 - Shift right logical
    &CPU::handleSRA,        // 0x03 - Shift right arithmetic
    // ... remaining 60 special functions
};
```

#### Instruction Implementation Pattern
```cpp
void CPU::handleLUI(const InstructionInfo& info) {
    // 1. Validate operation (register 0 check, etc.)
    if (info.rt == 0) return; // Can't write to register 0
    
    // 2. Perform operation
    uint32_t value = _ImmLU_(info.code); // Shift immediate to upper 16 bits
    
    // 3. Log operation (if enabled)
    ZONI_LOG_CPU_INSTRUCTION("LUI R%d, 0x%04x (result: 0x%08x)", 
                             info.rt, info.immU, value);
    
    // 4. Update state
    setRegister(info.rt, value);
}
```

### 2. Memory Subsystem

#### PlayStation Memory Map
```
┌─────────────────┬────────────────┬─────────────────────┐
│   Address Range │      Size      │     Description     │
├─────────────────┼────────────────┼─────────────────────┤
│ 0x00000000      │     2MB        │ Main RAM            │
│ 0x1F800000      │     8KB        │ Scratchpad RAM      │
│ 0x1F801000      │     8KB        │ Hardware Registers  │
│ 0x1F802000      │     8KB        │ Expansion Region 1  │  
│ 0x1FA00000      │    512KB       │ Expansion Region 2  │
│ 0x1FC00000      │    512KB       │ BIOS ROM            │
│ 0xFFFE0000      │    512KB       │ I/O Ports           │
└─────────────────┴────────────────┴─────────────────────┘
```

#### Memory Interface Design
```cpp
class Memory {
private:
    // Physical memory regions
    std::array<uint8_t, RAM_SIZE> m_ram;           // 2MB main RAM
    std::array<uint8_t, VRAM_SIZE> m_vram;         // 1MB video RAM  
    std::array<uint8_t, BIOS_SIZE> m_bios;         // 512KB BIOS
    std::array<uint8_t, SCRATCHPAD_SIZE> m_scratchpad; // 1KB scratchpad
    
public:
    // Memory access interface
    uint32_t read32(uint32_t address);
    uint16_t read16(uint32_t address);  
    uint8_t read8(uint32_t address);
    
    void write32(uint32_t address, uint32_t value);
    void write16(uint32_t address, uint16_t value);
    void write8(uint32_t address, uint8_t value);
    
private:
    // Address translation
    uint32_t translateAddress(uint32_t virtual_addr);
    bool isValidAddress(uint32_t address);
};
```

### 3. Logging Subsystem

#### Multi-Level Logging Architecture
```cpp
enum class LogLevel : int {
    TRACE = 0,      // Very verbose, instruction-level
    DEBUG = 1,      // Debug information
    INFO = 2,       // General information
    WARN = 3,       // Warnings
    ERROR = 4,      // Errors
    CRITICAL = 5    // Critical errors
};

enum class LogCategory : int {
    SYSTEM = 0,     // System-level messages
    CORE = 1,       // Emulator core
    CPU = 2,        // MIPS processor
    MEMORY = 3,     // Memory subsystem
    GPU = 4,        // Graphics
    SPU = 5,        // Audio
    CDROM = 6,      // Storage
    DEBUG = 7,      // Debug framework  
    BIOS = 8        // BIOS operations
};
```

#### Compile-Time Log Control
```cpp
// High-performance categorical logging
using CPU_DEBUG_LOG = Logger::CategoryLogger<LogCategory::CPU, LogLevel::DEBUG_LEVEL, false>;
using MEMORY_TRACE_LOG = Logger::CategoryLogger<LogCategory::MEMORY, LogLevel::TRACE, false>;

// Specialized instruction logging (can be disabled)
#ifdef ENABLE_CPU_INSTRUCTION_LOGGING
#define ZONI_LOG_CPU_INSTRUCTION(...) ZONI_LOG_DEBUG(CPU, __VA_ARGS__)
#define ZONI_LOG_CPU_FETCH(...) ZONI_LOG_TRACE(CPU, __VA_ARGS__)
#else
#define ZONI_LOG_CPU_INSTRUCTION(...) do {} while(0)
#define ZONI_LOG_CPU_FETCH(...) do {} while(0)
#endif
```

#### Thread-Safe Implementation
```cpp
class Logger {
private:
    std::mutex m_mutex;              // Thread safety
    std::ofstream m_file;            // File output
    LogLevel m_level;                // Current log level
    
public:
    template<typename... Args>
    void logf(LogLevel level, LogCategory category, const char* format, Args... args) {
        if (level < m_level) return;
        
        std::lock_guard<std::mutex> lock(m_mutex);
        // Thread-safe logging implementation
    }
};
```

---

## 🔧 Build System Architecture

### Modern Makefile Design
```makefile
# Compiler and flags
CXX := g++
CXXFLAGS := -std=c++20 -Wall -Wextra -MMD
DEBUG_FLAGS := -g3 -O0 -DDEBUG
RELEASE_FLAGS := -O3 -DNDEBUG -march=native

# Directory structure  
SRC_DIR := src
INC_DIR := include
BUILD_DIR := build
OBJ_DIR := $(BUILD_DIR)/obj

# Auto-discovery of source files
SOURCES := $(shell find $(SRC_DIR) -name '*.cpp')
OBJECTS := $(SOURCES:$(SRC_DIR)/%.cpp=$(OBJ_DIR)/%.o)
DEPENDS := $(OBJECTS:.o=.d)

# Targets with proper dependency tracking
$(BUILD_DIR)/zonistation-one: $(OBJECTS)
    $(CXX) $(OBJECTS) -o $@

# Include dependency files for proper incremental builds
-include $(DEPENDS)
```

### Build Targets
- **`make debug`**: Development build with symbols and assertions
- **`make release`**: Optimized build for performance
- **`make test-debugger`**: Run debugger functionality tests
- **`make clean`**: Clean all build artifacts
- **`make info`**: Display build configuration

---

## 🎮 Emulation Strategy

### Accuracy vs Performance Trade-offs

**Current Approach: Accuracy First**
1. **Interpreter Implementation**: Easier to debug and verify
2. **Instruction-Level Accuracy**: Exact MIPS behavior
3. **Cycle-Accurate Timing**: Eventually for hardware compatibility
4. **Bit-Perfect Operations**: Exact register and memory behavior

**Future Optimization Path**
1. **Profile Critical Paths**: Identify performance bottlenecks  
2. **Optimize Hot Instructions**: Inline common operations
3. **Block Translation**: Translate frequently executed code blocks
4. **JIT Compilation**: Dynamic recompilation for speed (far future)

### Emulation Loop Design
```cpp
void Emulator::executeFrame() {
    const uint32_t CYCLES_PER_FRAME = 33868800 / 60; // ~564,480 cycles
    
    uint32_t totalCycles = 0;
    while (totalCycles < CYCLES_PER_FRAME && !m_cpu->isHalted()) {
        // Execute one CPU instruction
        uint32_t cycles = m_cpu->step();
        totalCycles += cycles;
        
        // Update other components
        m_gpu->update(cycles);
        m_spu->update(cycles);
        m_cdrom->update(cycles);
        
        // Handle interrupts
        if (m_cpu->checkInterrupts()) {
            m_cpu->processInterrupts();
        }
    }
}
```

---

## 🧪 Testing Architecture

### Multi-Level Testing Strategy

**Unit Tests** - Individual instruction verification
```cpp
TEST(CPUInstructions, LUI_LoadsUpperImmediate) {
    CPU cpu(&memory);
    cpu.reset();
    
    // LUI R1, 0x1234 -> should load 0x12340000 into R1
    uint32_t instruction = 0x3C011234; // LUI R1, 0x1234
    cpu.executeInstruction(instruction);
    
    EXPECT_EQ(cpu.getRegister(1), 0x12340000);
}
```

**Integration Tests** - Component interaction
```cpp
TEST(CPUMemory, StoreWordUpdatesMemory) {
    Memory memory;
    CPU cpu(&memory);
    
    cpu.setRegister(1, 0x80000000);  // Base address
    cpu.setRegister(2, 0xDEADBEEF);  // Value to store
    
    // SW R2, 0(R1) - Store R2 at address R1+0
    uint32_t instruction = 0xAC220000;
    cpu.executeInstruction(instruction);
    
    EXPECT_EQ(memory.read32(0x80000000), 0xDEADBEEF);
}
```

**BIOS Validation Tests** - Real-world verification
```cpp
TEST(BIOSCompatibility, ExecutesFirstTenInstructions) {
    Emulator emulator;
    emulator.loadBIOS("test_bios.bin");
    
    // Execute first 10 instructions
    for (int i = 0; i < 10; i++) {
        emulator.step();
        ASSERT_FALSE(emulator.getCPU().isHalted());
    }
    
    // Verify expected CPU state after BIOS initialization
    EXPECT_EQ(emulator.getCPU().getPC(), 0xBFC00028);
}
```

---

## 📊 Performance Considerations

### Current Performance Characteristics
- **Instruction Dispatch**: ~3-5 cycles per MIPS instruction
- **Memory Access**: Direct array access (very fast)
- **Logging Overhead**: Minimal when disabled (compile-time elimination)
- **Debug Builds**: ~10x slower due to extensive logging

### Optimization Opportunities (Future)
1. **Instruction Caching**: Cache decoded instruction info
2. **Branch Prediction**: Predict common branch patterns
3. **Memory Prefetching**: Anticipate memory access patterns
4. **Vectorization**: Use SIMD for bulk operations
5. **Multi-threading**: Separate CPU/GPU/Audio threads

---

## 🔍 Debugging & Development Tools

### Built-in Debugger
```cpp
class Debugger {
private:
    std::map<uint32_t, bool> m_breakpoints;
    bool m_singleStep;
    
public:
    void setBreakpoint(uint32_t address);
    void clearBreakpoint(uint32_t address);
    bool checkBreakpoint(uint32_t address);
    void dumpCPUState();
    void dumpMemoryRange(uint32_t start, uint32_t size);
};
```

### Runtime Analysis Tools
- **Instruction Frequency Counter**: Track most-used instructions
- **Memory Access Profiler**: Monitor memory usage patterns  
- **Performance Counter**: Measure emulation speed
- **State Inspector**: Real-time CPU/Memory state viewing

---

## 🎯 Design Principles

### Code Quality Standards
1. **RAII Everywhere**: Automatic resource management
2. **Type Safety**: Strong typing with enum classes
3. **Const Correctness**: Immutable data where possible
4. **Exception Safety**: Proper error handling
5. **Documentation**: Self-documenting code with comments

### PlayStation Accuracy Principles  
1. **Hardware Fidelity**: Match original hardware behavior exactly
2. **Timing Accuracy**: Respect PlayStation timing constraints
3. **Memory Layout**: Exact memory map implementation
4. **Instruction Semantics**: Bit-perfect MIPS R3000A emulation
5. **Peripheral Behavior**: Authentic device responses

### Maintainability Principles
1. **Modular Design**: Clear component boundaries
2. **Single Responsibility**: Each class has one job
3. **Dependency Injection**: Testable component interfaces
4. **Configuration Management**: Runtime behavior control
5. **Extensible Architecture**: Easy to add features

---

**ZonistationOne Technical Team**  
*Architecture designed for accuracy, built for the future* 🏗️✨

---

> **Next Update**: This document will be updated as we implement Phase 3 (Extended CPU) with detailed information about branch/jump instruction handling and delay slot implementation.