# ZoniStationOne 🎮

A PlayStation One emulator written in C, designed for accuracy and performance.

## 🎯 **Current Status: BIOS EXECUTION COMPLETE** ✅

**Version**: 0.1.1  
**Last Updated**: December 2024

---

## 📊 **Project Overview**

ZoniStationOne is a PlayStation One emulator that aims to provide accurate emulation of the original PlayStation hardware. The project follows the structure of PCSX-ReARMed while implementing a clean, modern C codebase.

### ✅ **Completed Features**
- **Memory System**: Complete PlayStation memory map implementation
- **CPU Foundation**: Basic MIPS R3000A CPU with full register support
- **Instruction System**: Basic fetch, decode, and execute pipeline
- **Load Delay Slots**: Proper MIPS load delay slot handling
- **Cycle Counting**: Basic CPU cycle tracking
- **PC Management**: Correct program counter advancement
- **Byte Order Handling**: Fixed instruction decoding issues
- **Clean Runtime Output**: Professional, readable debug output
- **SYSCALL Instruction**: Complete implementation with exception handling
- **Exception Handling**: Improved system with proper vectors and state management
- **BIOS Loading**: Complete BIOS loading and validation system
- **BIOS Execution**: Working BIOS execution with development timeout
- **Hardware Register Access**: Basic hardware register read/write support
- **COP0 Instructions**: Coprocessor 0 support (MFC0, MTC0, RFE)
- **Development Logging**: Comprehensive logging for development and debugging

### ⚠️ **Partially Complete**
- **CPU Instruction Set**: Core instructions working, advanced instructions missing
- **Exception Handling**: SYSCALL and BREAK working, other exceptions need implementation
- **Hardware Emulation**: Basic framework in place, specific hardware needs implementation

### ❌ **Missing Features**
- **Advanced MIPS Instructions**: MULT, DIV, SLT, SLTU, MFHI/MFLO, MTHI/MTLO, etc.
- **Advanced Memory Operations**: LWL/LWR, SWL/SWR
- **Complete Branch Instructions**: BLEZ, BGTZ, BLTZ, BGEZ
- **Additional Exception Types**: ADEL, ADES, RI, ERET instruction
- **GPU System**: Graphics Processing Unit for display output
- **SPU System**: Sound Processing Unit for audio
- **CD-ROM System**: CD-ROM drive emulation
- **Controller System**: Input device emulation

### 📋 **Planned Features**
- **Graphics System**: GPU plugin development and display
- **Audio System**: SPU plugin development
- **Input System**: Controller emulation
- **CD-ROM System**: Disc drive emulation
- **Advanced CPU**: Complete MIPS instruction set
- **Performance Optimization**: Dynamic recompilation and optimization

## 🚀 **Quick Start**

### Prerequisites
- GCC compiler
- SDL2 development libraries
- OpenGL development libraries

### Building
```bash
cd ZoniStationOne
make clean
make
```

### Running
```bash
./bin/zonistationone
```

## 📁 **Project Structure**

```
ZoniStationOne/
├── src/
│   ├── core/           # Core emulation components
│   │   ├── zoni_cpu.c      # CPU emulation
│   │   ├── zoni_memory.c   # Memory management
│   │   ├── zoni_bios.c     # BIOS handling
│   │   └── zoni_hardware.c # Hardware emulation
│   ├── include/        # Header files
│   └── frontend/       # Frontend and main
├── bin/               # Compiled executables
├── obj/               # Object files
├── bios_files/        # BIOS files (not included)
└── docs/              # Documentation
```

## 🎮 **Current Capabilities**

### ✅ **Working Features**
- **BIOS Loading**: Loads and validates PlayStation BIOS files
- **BIOS Execution**: Executes BIOS code with proper timeout
- **Memory Management**: Complete PlayStation memory map
- **CPU Instructions**: Core MIPS instruction set
- **Hardware Access**: Basic hardware register read/write
- **Development Tools**: Comprehensive logging and debugging

### 🔧 **Development Features**
- **Controlled Execution**: 50K cycle timeout for development
- **Loop Detection**: Detects stuck BIOS execution
- **Detailed Logging**: Progress tracking and instruction logging
- **Error Handling**: Graceful error handling and reporting

## 📈 **Progress Tracking**

### Phase 1: Core Foundation ✅
- [x] Memory system implementation
- [x] CPU instruction set (core)
- [x] BIOS loading and execution
- [x] Hardware register access
- [x] Development tools and logging

### Phase 2: Hardware Emulation 🔄
- [ ] GPU implementation
- [ ] SPU implementation  
- [ ] CD-ROM emulation
- [ ] Controller input

### Phase 3: Advanced Features 📋
- [ ] Complete MIPS instruction set
- [ ] Performance optimization
- [ ] Game compatibility testing
- [ ] Advanced debugging tools

## 🐛 **Known Issues**

- **Limited Instruction Set**: Only core MIPS instructions implemented
- **No Graphics Output**: GPU not yet implemented
- **No Audio Output**: SPU not yet implemented
- **No Input Handling**: Controller not yet implemented

## 🤝 **Contributing**

This is a development project. Contributions are welcome! Please see the development documentation for current priorities and guidelines.

## 📄 **License**

This project is for educational and development purposes.

---

**Status**: 🟡 **Development Phase** - Core systems working, hardware emulation in progress 