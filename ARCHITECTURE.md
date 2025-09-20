# ZonistationOne Technical Architecture 🏗️

> **Version**: 2.0  
> **Status**: Active Development - Hardware Implementation Phase  
> **Last Updated**: September 2025

This document provides a comprehensive overview of ZonistationOne's technical architecture, design decisions, and implementation progress through our systematic BIOS compatibility development.

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
│    CPU      │   Memory    │  Hardware   │    Debug      │
│  R3000A     │  Manager    │ Modules     │  Framework    │
├─────────────┼─────────────┼─────────────┼───────────────┤
│   GPU       │    SPU      │   CDROM     │   Logger      │
│  Engine     │ Processor   │ Controller  │   System      │
└─────────────────────────────────────────────────────────┘
```

---

## 📈 Development Progress Summary

### Phase 1: Foundation (✅ Complete)
- **Core emulator framework**: Modular architecture with PCSX-Redux patterns
- **MIPS R3000A CPU**: Complete instruction set implementation
- **Memory management**: Address translation and region handling
- **Exception system**: Proper Status/Cause/EPC register handling
- **Basic I/O framework**: Hardware register arrays and access patterns

### Phase 2: BIOS Compatibility (✅ Complete)
- **Cache control (BIU)**: Implemented 0xfffe0130 register with proper invalidation
- **BIOS loading**: 512KB BIOS ROM integration at 0x1FC00000
- **Execution stability**: Eliminated crashes, achieved 5+ second stable execution
- **Diagnostic framework**: Verbose logging for hardware access analysis

### Phase 3: Hardware Modules (🔄 In Progress)
- **Modular architecture**: Redux-style component separation
- **Interrupt Controller**: 0x1f801070 (IREG) and 0x1f801074 (IMASK) registers
- **Timer Module**: Complete 3-timer implementation (0x1f801100-0x1f801128)
- **Memory Controller**: Identified as next critical component (0x1f801010, 0x1f801060)

---

## 🧠 Core Components

### 1. CPU Subsystem (MIPS R3000A) ✅

#### Design Philosophy
- **Interpreter-first approach**: Prioritize accuracy over speed initially
- **Instruction-level emulation**: Bit-perfect MIPS R3000A behavior
- **PCSX-Redux patterns**: Proven dispatch table architecture
- **Modern C++**: Type-safe enums, RAII, smart pointers

#### Implementation Status
- ✅ **Complete instruction set**: All 56+ MIPS instructions implemented
- ✅ **Exception handling**: Proper interrupt and exception processing
- ✅ **Cache control**: BIU register implementation working perfectly
- ✅ **Coprocessor 0**: Status, Cause, EPC, and other system registers
- ✅ **Load delay slots**: Accurate pipeline behavior emulation

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
---

## 🚀 BIOS Compatibility Journey

### The Challenge
PlayStation 1 BIOS (SCPH1001.BIN) contains sophisticated initialization routines that probe and configure hardware components. Our goal was to achieve stable BIOS execution by implementing the minimal required hardware.

### Timeline of Achievements

#### Stage 1: Initial BIOS Loading (Week 1)
- **Problem**: BIOS loaded but immediately crashed with exception loops
- **Analysis**: Missing cache control and basic memory management
- **Solution**: Implemented BIU (Bus Interface Unit) register at 0xfffe0130

#### Stage 2: Cache Control Implementation (Week 2)  
- **Problem**: BIOS accessing unknown cache control register
- **Analysis**: Traced BIOS writes to 0xfffe0130 with values 0x804→0x800→0x1e988
- **Solution**: Full cache control implementation with invalidation logic
- **Result**: ✅ BIOS successfully completes cache initialization phase

#### Stage 3: Hardware Register Access (Week 3)
- **Problem**: BIOS stuck in infinite exception loop after cache setup
- **Analysis**: Verbose logging revealed hardware register access patterns
- **Discovery**: BIOS accessing 0x1f801010 (memory control) and 0x1f801060 (RAM size)
- **Solution**: Implemented modular hardware architecture following PCSX-Redux patterns

#### Stage 4: Modular Hardware Components (Current)
- **Implemented**: Interrupt Controller (0x1f801070-0x1f801074)
- **Implemented**: Timer Module with 3 timers (0x1f801100-0x1f801128)  
- **Status**: BIOS now runs stably for 5+ seconds without crashes
- **Next**: Memory controller implementation for complete BIOS compatibility

### Hardware Register Implementation Strategy

```cpp
// Memory Map Structure (Redux Architecture)
class Memory {
    // Modular hardware components
    std::unique_ptr<InterruptController> m_interruptController;
    std::unique_ptr<TimerModule> m_timerModule;
    std::unique_ptr<MemoryController> m_memoryController; // Next phase
    
    // Register delegation pattern
    uint32_t read32(uint32_t address) {
        // Interrupt Controller (0x1f801070-0x1f801074)
        if (address == IREG || address == IMASK) {
            return m_interruptController->readRegister(address);
        }
        
        // Timer/Counter registers (0x1f801100-0x1f801128)
        if (address >= TIMER0_COUNT && address <= TIMER2_TARGET) {
            return m_timerModule->readRegister(address);
        }
        
        // Memory controller (to be implemented)
        if (address >= 0x1f801000 && address <= 0x1f801020) {
            return m_memoryController->readRegister(address);
        }
    }
};
```

### BIOS Analysis Results

**Successfully Implemented:**
- ✅ **Cache Control (0xfffe0130)**: BIU register with proper invalidation  
- ✅ **Interrupt Controller**: IREG (0x1f801070) and IMASK (0x1f801074)
- ✅ **Timer System**: Complete 3-timer implementation with mode/count/target
- ✅ **Stable Execution**: 5+ seconds runtime without crashes

**BIOS Access Patterns Discovered:**
```
0xfffe0130 = 0x00000804  # Cache invalidation request
0xfffe0130 = 0x00000800  # Cache invalidation confirm  
0xfffe0130 = 0x0001e988  # Cache configuration set

0x1f801010 = 0x0013243f  # Memory control register
0x1f801060 = 0x00000b88  # RAM size register
```

---

## 🔧 Hardware Modules (Redux Architecture)

### 2. Memory Subsystem ✅

#### Implementation Status
- ✅ **Address translation**: Cached/uncached region handling
- ✅ **Memory regions**: RAM (2MB), VRAM (1MB), BIOS (512KB), Scratchpad (1KB)
- ✅ **Hardware registers**: Modular component delegation pattern
- ✅ **Cache control**: BIU register with PlayStation-accurate behavior
- 🔄 **Memory controller**: Next implementation target

#### Memory Map
```cpp
// PlayStation 1 Memory Layout
0x00000000 - 0x001FFFFF  // Main RAM (2MB, mirrored to 8MB)
0x1F000000 - 0x1F7FFFFF  // EXP1 (Expansion region 1)  
0x1F800000 - 0x1F8003FF  // Scratchpad (1KB)
0x1F801000 - 0x1F802FFF  // Hardware registers (8KB)
0x1F801070 - 0x1F801074  // Interrupt controller ✅
0x1F801100 - 0x1F801128  // Timer/Counter ✅  
0x1F801010 - 0x1F801020  // Memory controller 🔄
0x1FC00000 - 0x1FC7FFFF  // BIOS ROM (512KB)
0xFFFE0130              // BIU Cache control ✅
```

### 3. Interrupt Controller ✅

Redux-style modular implementation:
```cpp
class InterruptController {
private:
    uint32_t m_status = 0;  // IREG (0x1f801070) 
    uint32_t m_mask = 0;    // IMASK (0x1f801074)
    
public:
    uint32_t readRegister(uint32_t address);
    void writeRegister(uint32_t address, uint32_t value);
    void triggerInterrupt(uint32_t mask);
    bool isPending() const { return (m_status & m_mask) != 0; }
};
```

### 4. Timer Module ✅  

Complete 3-timer system implementation:
```cpp
struct Timer {
    uint32_t count;   // Current counter value
    uint32_t mode;    // Mode register with control bits
    uint32_t target;  // Target comparison value
};

class TimerModule {
private:
    Timer m_timers[3];  // Timer 0, 1, 2
    
public:
    // Register access (0x1f801100-0x1f801128)
    uint32_t readRegister(uint32_t address);
    void writeRegister(uint32_t address, uint32_t value);
    void update(uint32_t cycles);
};
```

---

## 🔍 Debugging & Development Tools

### BIOS Diagnostic Framework

**Verbose Analysis Mode:**
```bash
# Run with full hardware register tracing
./zonistation-one -v bios_files/SCPH1001.BIN

# Filter for hardware access patterns  
timeout 5s ./zonistation-one -v bios.bin | grep "0x1f80" | head -20

# Extract register access frequency
timeout 3s ./zonistation-one -v bios.bin | grep "Hardware register" | sort | uniq -c
```

**Key Diagnostic Outputs:**
```log
[0.006536] [INFO ] [MEMORY] BIU cache control write: 0xfffe0130 = 0x00000804
[0.006538] [INFO ] [MEMORY] BIU: Cache invalidation requested (0x00000804)
[0.004061] [DEBUG] [CPU   ] SW R8, 4112(R1) [0x1f801010] = 0x0013243f
[0.004066] [DEBUG] [CPU   ] SW R8, 4192(R1) [0x1f801060] = 0x00000b88
```

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
- **Hardware Register Tracer**: Log all I/O register access
- **BIOS Progress Monitor**: Track initialization phases

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

### BIOS Compatibility Strategy
1. **Incremental Implementation**: Add hardware as BIOS requires it
2. **Access Pattern Analysis**: Use verbose logging to understand needs
3. **Modular Architecture**: Redux-style component separation
4. **Minimal Compliance**: Implement only what BIOS actually uses
5. **Regression Testing**: Ensure changes don't break previous functionality

### Maintainability Principles
1. **Modular Design**: Clear component boundaries
2. **Single Responsibility**: Each class has one job
3. **Dependency Injection**: Testable component interfaces
4. **Configuration Management**: Runtime behavior control
5. **Extensible Architecture**: Easy to add features

---

## 🎯 Next Steps & Roadmap

### Immediate Priority (Phase 3 Continuation)
1. **Memory Controller Implementation** 🔄
   - Implement 0x1f801010 (memory control register)
   - Implement 0x1f801060 (RAM size register) 
   - Add memory timing control features

2. **DMA Controller Module** ⏳
   - Basic DMA register framework
   - GPU/SPU/CDROM DMA channels
   - Proper DMA transfer emulation

3. **Serial Interface Module** ⏳
   - 0x1f801050 (SIO control)
   - Basic controller/memory card interface

### Medium Term (Phase 4)
1. **GPU Initialization Support**
   - Basic display configuration registers
   - VRAM access patterns
   - Primitive GPU command processing

2. **Enhanced BIOS Analysis**
   - Complete BIOS initialization sequence tracing
   - Hardware compatibility matrix
   - Automated regression testing

### Long Term (Phase 5+)
1. **Game Loading Support**
   - CDROM system implementation  
   - ISO/BIN file format support
   - Game executable loading

2. **Performance Optimization**
   - JIT compiler for CPU core
   - Graphics acceleration
   - Audio system optimization

---

**ZonistationOne Development Team**  
*From BIOS compatibility to full PlayStation 1 emulation* 🏗️✨

---

> **Next Update**: This document will be updated after Memory Controller implementation with complete BIOS compatibility results and next phase planning.