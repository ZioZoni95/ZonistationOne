# ZoniStationOne

A PlayStation One emulator written in C, inspired by PCSX-ReARMed but designed as a modern, clean implementation.

## Overview

ZoniStationOne is a PlayStation One emulator that aims to provide accurate emulation of the original PlayStation hardware. The project is built from the ground up with modern C practices while learning from the excellent work done in PCSX-ReARMed.

## Current Status: CPU Foundation Complete ✅

The project has successfully implemented its foundational components and is ready for the next phase of development.

### ✅ Completed Features

- **Memory System**: Full PlayStation memory map with proper region management
- **CPU Foundation**: MIPS R3000A register structure and load delay system
- **Build System**: Automated build process with dependency detection
- **Testing**: All basic functionality tests passing
- **Documentation**: Comprehensive development guides and status tracking

### 🔄 In Progress

- **CPU Instruction Interpreter**: Implementing MIPS R3000A instruction set
- **BIOS Emulation**: PlayStation BIOS loading and HLE implementation

## Features (Planned)

- MIPS R3000A CPU emulation (interpreter and dynamic recompiler)
- PlayStation GPU emulation
- SPU (Sound Processing Unit) emulation
- CD-ROM emulation
- Memory card support
- Controller input emulation
- BIOS emulation (HLE and LLE support)
- Plugin architecture for GPU, SPU, and input devices

## Project Structure

```
ZoniStationOne/
├── src/
│   ├── core/           # Core emulation engine
│   │   ├── zoni_common.c    # Common utilities and logging
│   │   ├── zoni_memory.c    # Memory management
│   │   └── zoni_cpu.c       # CPU emulation (Foundation Complete)
│   ├── plugins/        # GPU, SPU, and input plugins
│   ├── frontend/       # User interface
│   └── include/        # Header files
├── docs/              # Documentation
├── tests/             # Unit tests
└── tools/             # Development tools
```

## Building

### Prerequisites

- GCC or Clang compiler
- Make
- SDL2 (for frontend)
- OpenGL (for GPU plugins)

### Build Instructions

```bash
# Clone the repository
git clone <repository-url>
cd ZoniStationOne

# Configure and build
./configure
make

# Run the emulator
./bin/zonistationone
```

### Build Targets

- `make` - Build the emulator
- `make debug` - Build with debug symbols
- `make release` - Build optimized release version
- `make clean` - Remove build artifacts
- `make help` - Show available targets

## Development Status

This project has completed its foundation phase and is now implementing the CPU instruction interpreter. The goal is to create a clean, well-documented PlayStation One emulator that can serve as both a learning tool and a functional emulator.

### Technical Achievements

#### CPU Architecture
- **Register Layout**: Follows PCSX-ReARMed structure with proper byte ordering
- **Load Delay**: Accurate MIPS load delay slot emulation with dual-slot system
- **Exception Handling**: Framework ready for instruction-level exceptions
- **Memory Integration**: CPU properly connected to memory system
- **Debug Support**: Register dump and instruction disassembly ready

#### Memory System
- **Complete Memory Map**: 8MB RAM, 512KB BIOS, hardware registers, etc.
- **Region Management**: Proper memory region mapping and access control
- **Statistics**: Memory access tracking and debugging utilities
- **Error Handling**: Comprehensive error handling for memory operations

## Testing

The project includes comprehensive tests for all implemented components:

```bash
# Run the emulator (includes built-in tests)
./bin/zonistationone
```

All tests are currently passing:
- ✅ Memory system initialization and operations
- ✅ CPU register access and load delay slots
- ✅ CPU-memory integration
- ✅ Error handling and validation

## Documentation

- [Development Guide](DEVELOPMENT.md) - Comprehensive development guidelines
- [Project Status](PROJECT_STATUS.md) - Current implementation status and roadmap

## License

This project is licensed under the GNU General Public License v2 or later.

## Acknowledgments

- PCSX-ReARMed team for the excellent reference implementation
- All contributors to the original PCSX project
- The PlayStation emulation community

## Contributing

Contributions are welcome! Please read the [Development Guide](DEVELOPMENT.md) before submitting pull requests.

## Roadmap

### Phase 1: CPU Interpreter (Current)
- [x] CPU foundation with register system
- [ ] MIPS instruction decoding
- [ ] Basic arithmetic and logical instructions
- [ ] Load/store and branch instructions

### Phase 2: BIOS and Boot
- [ ] BIOS loading and emulation
- [ ] System initialization
- [ ] Basic boot sequence

### Phase 3: Graphics and Audio
- [ ] GPU plugin system
- [ ] SPU plugin system
- [ ] Display and audio output

### Phase 4: Input and Storage
- [ ] Controller emulation
- [ ] CD-ROM emulation
- [ ] Memory card support

The project is well-structured and follows modern C development practices while maintaining compatibility with the PCSX-ReARMed reference structure. 