# ZonistationOne - PlayStation 1 Emulator

![License](https://img.shields.io/badge/license-GPL--3.0-blue)
![Build Status](https://img.shields.io/badge/build-passing-green)
![Platform](https://img.shields.io/badge/platform-Linux-lightgrey)

A PlayStation 1 emulator written in modern C++20, designed for accuracy, performance, and maintainability. It follows the architectural patterns established by PCSX-Redux while implementing a clean, modular design.

## 🎯 Current Status: **MAJOR MILESTONE ACHIEVED** ✨

**We're successfully executing real PlayStation BIOS instructions!**

### ✅ Successfully Implemented & Working:
- **MIPS R3000A CPU Core** with proper instruction decoding
- **LUI** (Load Upper Immediate) - ✅ Verified working with BIOS
- **ORI** (OR Immediate) - ✅ Verified working with BIOS  
- **SW** (Store Word) - ✅ Writing to memory addresses
- **ADDIU** (Add Immediate Unsigned) - ✅ Arithmetic operations working
- **Professional Logging System** with categories and levels
- **Memory Management System** (2MB RAM, 1MB VRAM, 512KB BIOS)
- **Instruction Framework** following PCSX-Redux patterns

### 🔍 Recent Test Results:
```
[DEBUG] [CPU   ] LUI R8, 0x0013 (result: 0x00130000)
[DEBUG] [CPU   ] ORI R8, R8, 0x243f (0x00130000 | 0x243f = 0x0013243f)
[DEBUG] [CPU   ] LUI R1, 0x1f80 (result: 0x1f800000)
[DEBUG] [CPU   ] SW R8, 4112(R1) [0x1f801010] = 0x0013243f
[DEBUG] [CPU   ] ADDIU R8, R0, 2952 (0x00000000 + 2952 = 0x00000b88)
```

## 🏗️ Architecture

ZonistationOne follows a modular, component-based architecture:

```
┌─────────────────────────────────────────┐
│                Main Loop                │
├─────────────────────────────────────────┤
│              Emulator Core              │
├──────────┬──────────┬──────────┬────────┤
│   CPU    │  Memory  │   GPU    │  SPU   │
│ R3000A   │ Manager  │          │        │
├──────────┼──────────┼──────────┼────────┤
│         Logger & Debugger               │
└─────────────────────────────────────────┘
```

### Components:
- **CPU**: MIPS R3000A interpreter with full instruction set
- **Memory**: PlayStation memory map (RAM/VRAM/BIOS/I/O)
- **GPU**: Graphics Processing Unit (planned)
- **SPU**: Sound Processing Unit (planned)
- **CDROM**: CD-ROM controller (planned)
- **Logger**: Professional logging with categories and levels
- **Debugger**: CPU state inspection and breakpoint support

## 🚀 Quick Start

### Prerequisites
- **Linux** (Ubuntu/Debian recommended)
- **GCC 10+** with C++20 support
- **Make** build system
- **PlayStation BIOS file** (SCPH1001.BIN or similar)

### Building
```bash
# Clone the repository
git clone <repository-url>
cd ZonistationOne-2

# Install dependencies (Ubuntu/Debian)
make install-deps

# Build debug version
make debug

# Or build optimized release
make release
```

### Usage
```bash
# Run with BIOS file
./build/zonistation-one bios_files/SCPH1001.BIN

# Enable verbose logging
./build/zonistation-one --verbose bios_files/SCPH1001.BIN

# View all options
./build/zonistation-one --help
```

### Command Line Options
```
Usage: ./build/zonistation-one [OPTIONS] [FILE]

Options:
  -h, --help           Show this help message
  -v, --verbose        Enable verbose (DEBUG) logging
  -q, --quiet          Enable quiet (WARN+) logging
  -t, --trace          Enable trace logging (very verbose)
  --log-file FILE      Enable file logging to FILE
  --log-level LEVEL    Set log level (TRACE|DEBUG|INFO|WARN|ERROR|CRITICAL)

FILE can be a BIOS file (.bin) or ISO image (.iso/.cue)
```

## 🎮 MIPS R3000A CPU Implementation

Our CPU implementation follows PCSX-Redux patterns with modern C++20 features:

### Instruction Decoding Framework
```cpp
// Clean instruction information structure
struct InstructionInfo {
    uint32_t code;
    Opcode opcode;
    InstructionFormat format;
    uint32_t rs, rt, rd, sa;
    int32_t imm;
    // ... additional fields
};

// Professional dispatch table system
const CPU::InstructionHandler s_primaryHandlers[64] = {
    &CPU::handleSPECIAL,    // 0x00 - SPECIAL
    &CPU::handleREGIMM,     // 0x01 - REGIMM  
    &CPU::handleJ,          // 0x02 - J
    &CPU::handleJAL,        // 0x03 - JAL
    // ... more handlers
};
```

### Implemented Instructions
| Instruction | Opcode | Status | Notes |
|-------------|---------|---------|-------|
| **NOP** | 0x00000000 | ✅ Working | No operation |
| **LUI** | 0x0F | ✅ Working | Load Upper Immediate |
| **ORI** | 0x0D | ✅ Working | OR Immediate |
| **ADDIU** | 0x09 | ✅ Working | Add Immediate Unsigned |
| **SW** | 0x2B | ✅ Working | Store Word |
| **LW** | 0x23 | ✅ Working | Load Word |
| **OR** | SPECIAL 0x25 | ✅ Working | Bitwise OR |
| **ADDU** | SPECIAL 0x21 | ✅ Working | Add Unsigned |

## 🔧 Development

### Build Targets
```bash
make all         # Default build
make debug       # Debug build with symbols
make release     # Optimized release build
make clean       # Clean build files
make test-debugger # Test debugger functionality
make info        # Show build configuration
```

### Logging System
ZonistationOne features a professional logging system with:
- **Categories**: CPU, MEMORY, GPU, SPU, CDROM, CORE, SYSTEM, DEBUG, BIOS
- **Levels**: TRACE, DEBUG, INFO, WARN, ERROR, CRITICAL
- **Compile-time Control**: Enable/disable specific log types
- **File + Console Output**: Dual logging streams
- **Thread Safety**: Mutex-protected concurrent logging

```cpp
// Example usage
ZONI_LOG_INFO(CPU, "Initializing MIPS R3000A core");
ZONI_LOG_DEBUG(CPU, "Executing instruction: 0x%08x", instruction);
ZONI_LOG_CPU_INSTRUCTION("LUI R%d, 0x%04x", rt, imm);
```

### CPU Instruction Logging
Special CPU instruction logging can be enabled/disabled:
```cpp
// In include/core/logger.h
#define ENABLE_CPU_INSTRUCTION_LOGGING  // Uncomment to enable
```

## 🏆 Achievements & Milestones

### Phase 1: Foundation ✅ **COMPLETED**
- ✅ Project structure and build system
- ✅ Professional logging architecture  
- ✅ Memory management system
- ✅ BIOS loading functionality
- ✅ Basic emulator framework

### Phase 2: CPU Core ✅ **COMPLETED** 
- ✅ MIPS R3000A instruction decoding framework
- ✅ Dispatch table system following PCSX-Redux
- ✅ Essential instruction implementations
- ✅ **MAJOR MILESTONE**: Successfully executing real BIOS instructions!

### Phase 3: Extended CPU 🔄 **IN PROGRESS**
- 🔄 Jump and branch instructions (J, JAL, BEQ, BNE)
- 🔄 Additional SPECIAL instructions
- ⏳ Exception handling
- ⏳ Coprocessor instructions

### Phase 4: System Components ⏳ **PLANNED**
- ⏳ GPU implementation
- ⏳ SPU implementation  
- ⏳ CDROM controller
- ⏳ Controller input
- ⏳ Game loading

## 🎯 Current Development Focus

We're currently implementing the next batch of critical MIPS instructions:

1. **Jump Instructions**: J, JAL for program flow control
2. **Branch Instructions**: BEQ, BNE for conditional execution
3. **SPECIAL Functions**: More register-to-register operations
4. **Exception Handling**: Proper MIPS exception processing
- [ ] Implement basic GPU rendering pipeline
- [ ] Add SPU audio processing
- [ ] CD-ROM ISO parsing and file system support
- [ ] Controller input handling
- [ ] Memory card support
- [ ] GUI interface
- [ ] Game compatibility testing

## Architecture

ZonistationOne follows a modular architecture with clear separation of concerns:

```
src/
├── core/        - Main emulator coordination and timing
├── cpu/         - MIPS R3000A CPU emulation
├── memory/      - Memory management and address translation
├── gpu/         - Graphics Processing Unit
├── spu/         - Sound Processing Unit  
├── cdrom/       - CD-ROM drive emulation
└── gui/         - User interface (planned)

include/         - Header files
build/           - Build artifacts
external/        - Third-party dependencies
docs/            - Documentation
```

## Building

### Prerequisites

- Modern C++ compiler with C++20 support (GCC 10+, Clang 12+, MSVC 2022+)
- Make or similar build system
- Development libraries (OpenGL, audio, etc. - to be added)

### Build Instructions

```bash
# Clone the repository
git clone <your-repo-url>
cd ZonistationOne-2

# Install dependencies (Ubuntu/Debian)
make install-deps

# Build the emulator
make

# Or build debug version
make debug

# Run with BIOS
make run-bios
```

### Build Targets

- `make all` - Build the emulator (default)
- `make debug` - Build with debug symbols and logging
- `make release` - Build optimized release version
- `make clean` - Clean build artifacts
- `make run` - Build and run emulator
- `make run-bios` - Build and run with BIOS file
- `make help` - Show all available targets

## Usage

```bash
# Run emulator (basic)
./build/zonistation-one

# Load BIOS file
./build/zonistation-one bios_files/SCPH1001.BIN

# Load game ISO (planned)
./build/zonistation-one game.iso
```

## BIOS Files

You need a PlayStation 1 BIOS file to run the emulator. Place your BIOS file (e.g., `SCPH1001.BIN`) in the `bios_files/` directory. 

**Note:** You must own a legal copy of the PlayStation 1 BIOS to use it with this emulator.

## Design Philosophy

ZonistationOne aims to:

1. **Accuracy First** - Prioritize correct emulation over speed hacks
2. **Modern C++** - Use contemporary C++ features and best practices
3. **Clean Architecture** - Maintain clear separation between components
4. **Extensibility** - Design for easy modification and improvement
5. **Documentation** - Keep code well-documented and understandable

## Inspiration

This project draws inspiration from:

- **PCSX-Redux** - Modern PS1 emulator with excellent debugging features
- **DuckStation** - High-accuracy PS1 emulator
- **Mednafen** - Multi-system emulator known for accuracy

## Contributing

Contributions are welcome! This is a learning project, so:

- Clean, documented code is preferred
- Test your changes thoroughly  
- Follow existing code style
- Add comments explaining complex emulation logic

## License

This project is licensed under the GNU General Public License v3.0. See [LICENSE](LICENSE) for details.

## Disclaimer

This emulator is for educational and preservation purposes. You must own the original PlayStation 1 BIOS and games to use them with this emulator.

---

**ZonistationOne** - Because even PlayStation deserves a fresh take on emulation! 🎮