# newcore PlayStation 1 Emulator (Refactor)

## Project Status (July 2025)

This project is a modern, modular refactor of a PlayStation 1 emulator, inspired by the structure and best practices of PCSX ReARMed. The emulator now has a fully functional CPU execution loop, comprehensive memory mapping, and sophisticated hardware register emulation.

### **✅ Fully Implemented & Working**
- **CPU Core**: Complete MIPS R3000A instruction set with table-driven dispatch
  - R-type, I-type, and J-type instruction handlers implemented
  - Proper instruction decode and execution pipeline
  - Exception handling, delay slots, and load delay logic
  - BIOS instruction stream execution verified
- **Memory Map & Interconnect**: Comprehensive 20-region memory mapping
  - RAM, BIOS, and hardware register regions fully mapped
  - Memory aliases for KSEG0/KSEG1 and cached/uncached access
  - Scratchpad (1KB data cache RAM) implementation
  - All major PS1 address spaces covered (0x00000000-0xFFFFFFFF)
- **Hardware Register Emulation**: Sophisticated register handlers for all subsystems
  - Timer registers with simulated time progression
  - Interrupt controller (I_STAT, I_MASK) with proper state management
  - SIO, CDROM, SPU, and memory control register stubs
  - GPU and VRAM integration with proper memory routing
- **Event System**: Hardware event scheduling and dispatch
  - VBlank and DMA event handlers
  - Cycle-accurate timing simulation
  - Proper event queue management

### **🔧 Core Infrastructure**
- **Logging**: Modular, multi-level logging system with detailed execution tracing
- **RAM & VRAM**: Full read/write implementation with bounds checking
- **BIOS**: File loading and memory mapping with proper 32/16-bit access
- **DMA**: Channel setup and event integration
- **GPU**: VRAM integration and basic command processing
- **Event Scheduler**: Hardware event timing and dispatch system

### **🏗️ Architecture Highlights**
- **Highly modular**: Each subsystem is in its own directory/file with clear APIs
- **Table-driven design**: CPU instruction dispatch uses opcode and function tables for efficiency
- **Memory-mapped I/O**: Hardware registers accessed through unified memory interface
- **Event-driven timing**: Hardware events scheduled and dispatched through event queue
- **Extensible design**: Ready for plugin-based renderer, input, and audio systems

### **🚀 Current Capabilities**
- **BIOS Execution**: Successfully loads and executes PlayStation BIOS instructions
- **Memory Access**: All memory regions properly routed and handled
- **Hardware Simulation**: Realistic register values and timing simulation
- **Debugging**: Comprehensive logging and execution tracing
- **Modularity**: Clean separation of concerns with well-defined interfaces

### **Build & Run**
```bash
cd newcore
make clean && make
./psxemu
```

The emulator runs successfully, executing BIOS instructions and demonstrating the complete memory map and hardware register emulation.

### **📋 Implementation Status**
- **✅ Core Loop & CPU Execution**: Complete with full instruction set support
- **✅ Memory Map & Interconnect**: 20-region comprehensive mapping
- **✅ Hardware Register Emulation**: Sophisticated register handlers implemented
- **🔄 GPU Command Processing**: Basic integration, ready for expansion
- **🔄 DMA Integration**: Framework in place, ready for detailed implementation
- **🔄 Peripheral Integration**: SPU, CDROM, SIO frameworks ready

### **🎯 Next Development Phase**
- Expand GPU command processing and rendering pipeline
- Implement detailed DMA transfer logic and channel management
- Add more sophisticated peripheral emulation (SPU audio, CDROM drive)
- Integrate input handling and user interface
- Add save state and debugging features

---

**This project demonstrates a solid foundation for PlayStation 1 emulation with a clean, maintainable architecture ready for further development and feature expansion.**
