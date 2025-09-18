# ZonistationOne PlayStation 1 Emulator

A personal PlayStation 1 emulator written in C, based on the excellent [PCSX-Redux](https://github.com/grumpycoders/pcsx-redux) reference implementation. This project is designed for educational purposes and learning about emulation development.

![Emulator Version](https://img.shields.io/badge/version-0.1.0-blue)
![Platform](https://img.shields.io/badge/platform-Linux-lightgray)
![Language](https://img.shields.io/badge/language-C-green)
![License](https://img.shields.io/badge/license-Educational-yellow)

## 🎮 Features

### Currently Implemented
- **MIPS R3000A CPU Core**: Basic instruction decoding and execution
- **Memory System**: PlayStation 1 memory mapping with 2MB RAM, 512KB BIOS, 1KB scratchpad
- **BIOS Loading**: Support for standard PlayStation BIOS files (SCPH1001.BIN)
- **Instruction Set**: LUI, SW, SLL, J (Jump), NOP, and basic arithmetic operations
- **Debug Features**: Instruction disassembly, register dumps, memory dumps, single-step execution
- **Professional Build System**: Debug and release configurations with sanitizers

### Emulation Status
- ✅ CPU initialization and reset
- ✅ BIOS loading and execution start
- ✅ Memory address translation
- ✅ Basic instruction execution (~54 instructions from BIOS)
- ⏳ I/O register handling (placeholder warnings)
- ❌ Graphics Processing Unit (GPU)
- ❌ Sound Processing Unit (SPU)
- ❌ CD-ROM drive
- ❌ Game loading

## 🚀 Quick Start

### Prerequisites
- GCC compiler with C11 support
- Make
- PlayStation BIOS file (e.g., SCPH1001.BIN)

### Building
```bash
# Clone the repository
git clone https://github.com/ZioZoni95/ZonistationOne.git
cd ZonistationOne

# Build debug version (recommended for development)
make debug

# Or build optimized release version
make release
```

### Running
```bash
# Run with your BIOS file
./build/myps1_emu_debug --bios roms/SCPH1001.BIN --verbose

# Enable debug mode for single-step execution
./build/myps1_emu_debug --bios roms/SCPH1001.BIN --debug

# Show help
./build/myps1_emu_debug --help
```

## 📁 Project Structure

```
ZonistationOne/
├── src/                    # Source files
│   ├── main.c             # Entry point and CLI
│   ├── emulator.c         # Main emulator loop and control
│   ├── cpu.c              # MIPS R3000A CPU emulation
│   └── memory.c           # Memory system and BIOS loading
├── include/               # Header files
│   ├── psx_types.h        # PlayStation data types and constants
│   ├── emulator.h         # Emulator interface
│   ├── cpu.h              # CPU function declarations
│   └── memory.h           # Memory system interface
├── build/                 # Build artifacts
├── roms/                  # PlayStation BIOS files
│   └── SCPH1001.BIN      # PlayStation BIOS (user-provided)
└── Makefile              # Build system
```

## 🔧 Build Targets

```bash
make help           # Show available targets
make debug          # Build with debug symbols and sanitizers
make release        # Build optimized release version
make clean          # Clean build artifacts
make run-debug      # Build and run debug version
make run            # Build and run release version
```

## 🎯 Current Capabilities

The emulator can currently:
1. **Load PlayStation BIOS** (512KB) into memory
2. **Initialize CPU** to proper reset state (PC=0xBFC00000)
3. **Execute MIPS instructions** from BIOS code
4. **Handle memory access** with proper address translation
5. **Display debug information** including instruction disassembly
6. **Run approximately 54 BIOS instructions** before encountering unimplemented features

### Sample Output
```
=== ZonistationOne PlayStation 1 Emulator v0.1.0 ===
[Memory] Initialized 2048KB RAM, 512KB BIOS, 1024B scratchpad
[CPU] Reset to PC=0xBFC00000, SP=0x801FFF00
[Memory] Loaded BIOS: roms/SCPH1001.BIN (512KB)
[Memory] BIOS header: 13 00 08 3C 3F 24 08 35

=== Starting Emulation ===
[CPU] 0xBFC00000: 0x3C080013  lui $t0, 0x0013
[CPU] 0xBFC00004: 0x3508243F  ori $t0, $t0, 0x243F
[CPU] 0xBFC00008: 0x3C011F80  lui $at, 0x1F80
...
```

## 🛠️ Development

### Architecture
The emulator follows a modular design inspired by PCSX-Redux:
- **CPU Module**: MIPS R3000A instruction decoding and execution
- **Memory Module**: Address translation and hardware memory mapping  
- **Emulator Module**: Main loop, initialization, and control
- **Main Module**: Command-line interface and configuration

### Adding Instructions
To add new MIPS instructions:
1. Add opcode constants in `include/cpu.h`
2. Implement decoding logic in `cpu_execute_instruction()`
3. Add disassembly support in `cpu_print_instruction()`

### Memory Map
```
0x00000000 - 0x001FFFFF: Main RAM (2MB)
0x1F000000 - 0x1F7FFFFF: Expansion Region 1
0x1F800000 - 0x1F8003FF: Scratchpad (1KB)
0x1F801000 - 0x1F802FFF: I/O Ports
0x1FC00000 - 0x1FC7FFFF: BIOS ROM (512KB)
```

## 🎓 Learning Resources

This project is educational. Key resources for PlayStation 1 emulation:
- [PCSX-Redux](https://github.com/grumpycoders/pcsx-redux) - Modern PSX emulator
- [No$PSX Specifications](https://problemkaputt.de/psx-spx.htm) - Detailed hardware documentation
- [PSX.Dev Discord](https://discord.gg/QByKPpH) - PlayStation development community
- [MIPS R3000A Manual](https://www.cs.cmu.edu/afs/cs/academic/class/15740-f97/public/doc/mips-isa.pdf) - CPU reference

## ⚠️ Legal Notice

- This emulator is for **educational purposes only**
- You must own original PlayStation BIOS files to use them
- No copyrighted BIOS or game files are included
- Respect intellectual property rights

## 🚧 Roadmap

### Phase 1 - CPU Foundation ✅
- [x] Basic MIPS instruction set
- [x] Memory system
- [x] BIOS loading
- [x] Debug features

### Phase 2 - Extended CPU 🔄
- [ ] Complete MIPS R3000A instruction set
- [ ] Coprocessor 0 (system control)
- [ ] Exception handling
- [ ] Interrupt system

### Phase 3 - Hardware
- [ ] Graphics Processing Unit (GPU)
- [ ] Sound Processing Unit (SPU)  
- [ ] DMA controllers
- [ ] Timers and I/O ports
- [ ] CD-ROM drive

### Phase 4 - Games
- [ ] Game loading (CD-ROM images)
- [ ] Save states
- [ ] Controller input
- [ ] Audio output
- [ ] Video output

## 🤝 Contributing

This is a personal learning project, but feedback and suggestions are welcome!

1. Fork the repository
2. Create a feature branch
3. Make your changes
4. Test thoroughly
5. Submit a pull request

## 📜 License

This project is for educational purposes. Code is inspired by and references the PCSX-Redux project.

## 🙏 Acknowledgments

- **PCSX-Redux Team** - For the excellent reference implementation and documentation
- **Nicolas "Pixel" Noble** - Lead developer of PCSX-Redux
- **PlayStation Community** - For preservation and reverse engineering efforts
- **No$PSX Documentation** - Comprehensive hardware specifications

---

*Built with ❤️ for learning and preservation of gaming history*