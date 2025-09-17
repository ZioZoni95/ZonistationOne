# PlayStation 1 Emulator (ZonistationOne-2)

A complete PlayStation 1 emulator implementation written in C, following strict PSX-SPX documentation compliance and a methodical step-by-step development approach.

## 📋 Table of Contents

- [Project Overview](#project-overview)
- [Architecture](#architecture)
- [Current Status](#current-status)
- [Build Instructions](#build-instructions)
- [Development Tools](#development-tools)
- [Implementation Roadmap](#implementation-roadmap)
- [Development Guidelines](#development-guidelines)
- [Hardware Components](#hardware-components)
- [File Structure](#file-structure)
- [Contributing](#contributing)

## 🎮 Project Overview

This PlayStation 1 emulator is being developed following a complete architectural rewrite approach, prioritizing:

- **PSX-SPX Compliance**: All hardware implementations follow official PlayStation hardware specifications
- **Component Isolation**: Clean separation between hardware subsystems for easier debugging
- **Incremental Implementation**: Following guide.tex methodology for step-by-step development
- **Analysis-Driven Development**: Using comprehensive tools to guide implementation priorities

### Key Features

- ✅ Complete hardware architecture skeleton (9 major components)
- ✅ MIPS R3000A CPU with instruction decoding framework
- ✅ Full PlayStation memory map implementation
- ✅ PSX-SPX compliant register definitions
- ✅ Comprehensive development tools suite
- ✅ Clean build system with automatic dependency management

## 🏗️ Architecture

The emulator follows a component-based architecture with clear separation between hardware subsystems:

```
PlayStation System
├── CPU (MIPS R3000A) - Central processing unit with instruction execution
├── Memory - RAM, BIOS, Scratchpad, Hardware routing
├── GPU - Graphics processing and display output
├── DMA - Direct Memory Access controller (7 channels)
├── Timer - 3 programmable timers with IRQ generation
├── IRQ - Interrupt controller (11 IRQ sources)
├── SPU - Sound Processing Unit
├── CDROM - CD-ROM controller and drive emulation
├── SIO - Serial I/O for controllers and memory cards
└── MDEC - Motion decoder for FMV video
```

## 📊 Current Status

### Completed Components ✅

| Component | Status | Description |
|-----------|--------|-------------|
| **CPU** | 🟡 Skeleton | MIPS R3000A with instruction decoding framework |
| **Memory** | 🟢 Complete | Full PSX memory map with hardware routing |
| **GPU** | 🟡 Skeleton | GP0/GP1 commands, GPUSTAT register |
| **DMA** | 🟡 Skeleton | 7-channel DMA controller |
| **Timer** | 🟡 Skeleton | 3 timers with IRQ generation |
| **IRQ** | 🟡 Skeleton | Interrupt controller |
| **SPU** | 🟡 Skeleton | Sound processing unit |
| **CDROM** | 🟡 Skeleton | CD-ROM controller |
| **SIO** | 🟡 Skeleton | Serial I/O controller |
| **MDEC** | 🟡 Skeleton | Motion decoder |

### Implementation Status
- 🟢 **Architecture**: 100% complete - All components integrated
- 🟡 **CPU Instructions**: 10% - Basic framework with select instructions
- 🔴 **Hardware Timing**: 0% - Awaiting CPU completion
- 🔴 **Game Compatibility**: 0% - Future milestone

## 🛠️ Build Instructions

### Prerequisites
- GCC compiler
- Make build system
- Python 3 (for development tools)

### Building the Emulator

```bash
# Clean build
make clean

# Build debug version
make

# Or use VS Code task
# Ctrl+Shift+P -> "Tasks: Run Task" -> "build-debug"
```

### Running the Emulator

```bash
# Run with BIOS
./myps1_emu

# The emulator will automatically:
# 1. Initialize all hardware components  
# 2. Load BIOS from roms/SCPH1001.BIN
# 3. Start CPU execution from BIOS entry point
# 4. Display execution progress
```

## 🔧 Development Tools

Our comprehensive analysis tools help guide efficient implementation:

### 1. BIOS Disassembler (`tools/bios_disasm.py`)

Disassembles MIPS instructions from BIOS ROM for analysis.

```bash
# Disassemble first 100 instructions
python3 tools/bios_disasm.py --count 100

# Disassemble from specific offset  
python3 tools/bios_disasm.py --offset 0x1000 --count 50

# Export to file
python3 tools/bios_disasm.py --output bios_analysis.txt
```

**Features:**
- Complete MIPS instruction decoding
- Register name resolution
- Memory address calculation
- Jump target analysis

### 2. Trace Analyzer (`tools/trace_analyzer.py`)

Analyzes CPU execution traces and instruction patterns.

```bash
# Analyze execution trace
python3 tools/trace_analyzer.py trace.log

# Focus on specific instruction types
python3 tools/trace_analyzer.py trace.log --filter-type branch

# Export statistics
python3 tools/trace_analyzer.py trace.log --export-stats
```

**Features:**
- Instruction frequency counting
- Register usage tracking
- Branch prediction analysis
- Performance hotspot identification

### 3. Memory Dump Analyzer (`tools/memory_dump.py`)

Analyzes binary memory dumps and BIOS structure.

```bash
# Analyze BIOS structure
python3 tools/memory_dump.py roms/SCPH1001.BIN

# Dump specific memory range
python3 tools/memory_dump.py memory.bin --offset 0x80000000 --size 0x1000

# Compare memory dumps
python3 tools/memory_dump.py dump1.bin dump2.bin --compare
```

**Features:**
- Hex dump with ASCII representation
- String extraction and analysis
- Binary structure detection
- Memory dump comparison

### 4. Instruction Statistics (`tools/instruction_stats.py`)

Analyzes instruction implementation priority based on BIOS usage.

```bash
# Analyze BIOS instruction usage
python3 tools/instruction_stats.py

# Extended analysis (more instructions)
python3 tools/instruction_stats.py --instructions 5000

# Custom BIOS file
python3 tools/instruction_stats.py --bios path/to/bios.bin
```

**Output:**
- Implementation priority analysis
- BIOS usage frequency statistics  
- Suggested implementation roadmap
- Quick wins identification

## 🗺️ Implementation Roadmap

### Phase 1: Critical CPU Instructions (Next Step)

Following the instruction statistics analysis, implement in this order:

1. **LUI** - Load Upper Immediate (Most critical)
2. **LW/SW** - Load/Store Word (Memory access)
3. **BEQ/BNE** - Branch Equal/Not Equal (Control flow)
4. **J/JAL** - Jump/Jump and Link (Function calls)
5. **ADDIU/ADDI** - Add Immediate operations

### Phase 2: Extended CPU Functionality

6. **Arithmetic**: ADD, ADDU, SUB, SUBU
7. **Logical**: AND, OR, XOR, NOR, ANDI, ORI
8. **Shifts**: SLL, SRL, SRA and variants
9. **Comparisons**: SLT, SLTU, SLTI, SLTIU
10. **Multiply/Divide**: MULT, MULTU, DIV, DIVU + HI/LO

### Phase 3: System Integration

11. **Coprocessor**: MFC0, MTC0 (System control)
12. **Exception Handling**: SYSCALL, BREAK, RFE
13. **Memory Operations**: LB, LBU, LH, LHU, SB, SH
14. **Advanced Loads**: LWL, LWR, SWL, SWR

### Phase 4: Hardware Components

15. **Timer System**: Implement proper timing and IRQs
16. **DMA Controller**: Memory transfer automation
17. **GPU Basics**: Command processing and framebuffer
18. **SPU Integration**: Basic audio functionality

## 📏 Development Guidelines

### Code Style
- Follow existing code formatting
- Use descriptive variable names
- Add comments for complex hardware behavior
- Include PSX-SPX references for hardware implementations

### Testing Approach
1. **BIOS Testing**: Ensure each instruction works with BIOS code
2. **Unit Testing**: Test individual instruction implementations
3. **Integration Testing**: Verify component interactions
4. **Game Testing**: Test with actual PlayStation games

### Debugging Strategy
- Use comprehensive logging for CPU execution
- Implement instruction tracing for problematic areas
- Compare behavior against known-good emulators
- Use development tools for analysis

## 🖥️ Hardware Components

### CPU (psx_cpu.h/c)
- **MIPS R3000A** with 32 general-purpose registers
- **Instruction decoding** framework with opcode dispatch
- **Exception handling** for illegal instructions and system calls
- **Coprocessor 0** for system control

### Memory (psx_memory.h/c)
- **Complete PSX memory map** (2MB RAM, 512KB BIOS, etc.)
- **Hardware register routing** to appropriate components
- **Scratchpad memory** for fast local storage
- **Memory-mapped I/O** handling

### GPU (psx_gpu.h/c)
- **GP0/GP1 command processing** framework
- **GPUSTAT register** implementation
- **Framebuffer management** placeholder
- **Video output** system skeleton

### Other Components
Each component (DMA, Timer, IRQ, SPU, CDROM, SIO, MDEC) includes:
- PSX-SPX compliant register definitions
- Initialization and reset functionality
- Basic I/O operation frameworks
- Integration points for main system

## 📁 File Structure

```
ZonistationOne-2/
├── main.c                 # Main emulator entry point
├── Makefile              # Build system configuration
├── guide.tex             # Development guide (step-by-step)
├── myps1_emu            # Compiled emulator executable
│
├── include/              # Header files
│   ├── psx_types.h      # Common type definitions
│   ├── psx_cpu.h        # CPU component interface
│   ├── psx_memory.h     # Memory system interface
│   ├── psx_gpu.h        # GPU component interface
│   └── [other components]
│
├── src/                  # Source code implementation
│   ├── psx_cpu.c        # CPU instruction execution
│   ├── psx_memory.c     # Memory management
│   ├── psx_gpu.c        # Graphics processing
│   └── [other components]
│
├── tools/                # Development and analysis tools
│   ├── bios_disasm.py   # BIOS disassembler
│   ├── trace_analyzer.py # Execution trace analysis
│   ├── memory_dump.py   # Memory dump analysis
│   └── instruction_stats.py # Implementation priority analysis
│
├── roms/                 # BIOS files
│   └── SCPH1001.BIN     # PlayStation BIOS ROM
│
├── games/                # Game disc images
│   └── [game files]
│
└── obj/                  # Build artifacts (auto-generated)
    └── [compiled objects]
```

## 🤝 Contributing

### Getting Started
1. Read through this README completely
2. Examine the guide.tex for development methodology
3. Run the instruction statistics tool to understand priorities
4. Start with Phase 1 CPU instruction implementation

### Development Process
1. **Choose Next Instruction**: Use `instruction_stats.py` output
2. **Implement Instruction**: Add to CPU instruction decoder
3. **Test with BIOS**: Verify against BIOS execution
4. **Update Documentation**: Document any special behaviors

### Before Submitting
- Ensure code builds cleanly (`make clean && make`)
- Test BIOS execution doesn't regress
- Update relevant documentation
- Follow existing code style

## 📚 References

- **PSX-SPX**: PlayStation hardware documentation
- **guide.tex**: Project-specific development guide
- **MIPS R3000A**: Processor manual and instruction set
- **PlayStation Development**: Official Sony documentation

---

**Ready to implement a PlayStation 1 emulator step by step!** 🎮

The foundation is complete - time to bring the CPU instructions to life following the analysis-driven approach.