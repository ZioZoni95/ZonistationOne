# ZoniStationOne 🎮

A PlayStation One emulator written in C, designed for accuracy and performance.

## 🎯 **Current Status: BIOS EXECUTION WORKING - HARDWARE EMULATION IN PROGRESS** ✅

**Version**: 0.1.2  
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
- **BIOS Execution**: Working BIOS execution with 500K cycle timeout
- **Hardware Register Access**: Extended hardware register read/write support
- **COP0 Instructions**: Coprocessor 0 support (MFC0, MTC0, RFE)
- **Cache Control**: Cache control register support (0x1FFE0130)
- **Extended I/O Range**: Full hardware register range (0x1F801000-0x1F801FFF)
- **GPU Status**: Correct PlayStation GPU status values (0x10802000)
- **Development Logging**: Clean, focused logging for development and debugging

### 🔄 **Recently Fixed (Major Breakthroughs)**
- **BIOS Infinite Loop**: Fixed BIOS getting stuck at cache control register
- **Hardware Register Support**: Extended I/O range to include missing registers
- **GPU Status Compatibility**: Updated GPU status to match PCSX ReARMed
- **Cache Control**: BIOS can now properly configure cache control registers
- **Memory Routing**: Extended memory system to handle full hardware register space

### ⚠️ **Current Issue Being Worked On**
- **BIOS RAM Clearing Loop**: BIOS is stuck in RAM clearing phase waiting for hardware response
- **Missing Hardware Response**: BIOS waiting for timer interrupt or hardware signal to proceed

### ❌ **Missing Features**
- **Advanced MIPS Instructions**: MULT, DIV, SLT, SLTU, MFHI/MFLO, MTHI/MTLO, etc.
- **Advanced Memory Operations**: LWL/LWR, SWL/SWR
- **Complete Branch Instructions**: BLEZ, BGTZ, BLTZ, BGEZ
- **Additional Exception Types**: ADEL, ADES, RI, ERET instruction
- **GPU System**: Graphics Processing Unit for display output
- **SPU System**: Sound Processing Unit for audio
- **CD-ROM System**: CD-ROM drive emulation
- **Controller System**: Input device emulation
- **Timer System**: Hardware timer interrupts and responses

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
│   │   ├── zoni_gpu.c      # GPU emulation
│   │   ├── zoni_bus.c      # Hardware bus system
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
- **BIOS Execution**: Executes BIOS code with 500K cycle timeout
- **Memory Management**: Complete PlayStation memory map
- **CPU Instructions**: Core MIPS instruction set
- **Hardware Access**: Extended hardware register read/write
- **Cache Control**: BIOS can configure cache control registers
- **GPU Status**: Returns correct PlayStation GPU status values
- **Development Tools**: Clean, focused logging and debugging

### 🔧 **Development Features**
- **Controlled Execution**: 500K cycle timeout for development
- **Hardware Register Support**: Full range of PlayStation hardware registers
- **Clean Logging**: Focused on essential information
- **Error Handling**: Graceful error handling and reporting

## 📈 **Progress Tracking**

### Phase 1: Core Foundation ✅
- [x] Memory system implementation
- [x] CPU instruction set (core)
- [x] BIOS loading and execution
- [x] Hardware register access
- [x] Development tools and logging

### Phase 2: Hardware Emulation 🔄
- [x] Basic hardware register framework
- [x] Cache control register support
- [x] Extended I/O range support
- [x] GPU status compatibility
- [ ] Timer interrupt system
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

- **BIOS RAM Clearing Loop**: BIOS stuck waiting for hardware response
- **Missing Timer System**: No hardware timer interrupts implemented
- **Limited Instruction Set**: Only core MIPS instructions implemented
- **No Graphics Output**: GPU not yet fully implemented
- **No Audio Output**: SPU not yet implemented
- **No Input Handling**: Controller not yet implemented

## 🔍 **Current Investigation**

### **BIOS Analysis Results:**
- **Infinite Loop Location**: 0xBFC00230 → 0xBFC00270 → 0xBFC00250 → 0xBFC00264
- **Loop Type**: RAM clearing loop with cache control register writes
- **Missing Component**: Hardware response (likely timer interrupt) to exit loop
- **Next Step**: Implement missing hardware response based on PCSX ReARMed reference

## 🤝 **Contributing**

This is a development project. Contributions are welcome! Please see the development documentation for current priorities and guidelines.

## 📄 **License**

This project is for educational and development purposes.

---

**Status**: 🟡 **Development Phase** - Core systems working, hardware emulation in progress, BIOS loop issue identified and being resolved 