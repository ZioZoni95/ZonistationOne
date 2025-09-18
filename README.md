# ZonistationOne - PlayStation One Emulator

A PlayStation One emulator written in C, inspired by the PCSX Redux architecture.

## Features

- **Modular Architecture**: Clean separation between CPU, GPU, SPU, memory, and I/O systems
- **MIPS R3000A CPU**: Interpreted execution of PlayStation One instructions
- **Memory Management**: Accurate PlayStation memory map emulation
- **Graphics Processing**: GPU command processing and VRAM management  
- **Audio Processing**: SPU sound synthesis and effects
- **CD-ROM Support**: Disc loading and audio playback
- **Debug Support**: Logging, tracing, and debugging capabilities

## Project Structure

```
src/
├── main.c              # Main entry point
└── core/               # Core emulation components
    ├── system.h        # System definitions and constants
    ├── emulator.h/c    # Main emulator orchestration
    ├── logger.h/c      # Logging system
    ├── memory.h/c      # Memory management
    ├── cpu.h/c         # MIPS R3000A CPU emulation
    ├── gpu.h/c         # Graphics processing unit
    ├── spu.h/c         # Sound processing unit
    └── cdrom.h/c       # CD-ROM controller
```

## Building

### Requirements

- GCC or compatible C compiler
- Make
- Standard C libraries
- Math library (libm)

### Compile

```bash
# Build release version
make

# Build debug version
make debug

# Clean build files
make clean
```

### Installation

```bash
# Install to system (requires sudo)
make install

# Uninstall from system
make uninstall
```

## Usage

### Basic Usage

```bash
# Run emulator
./build/zonistation-one

# Show help
./build/zonistation-one --help
```

### With BIOS

```bash
# Load BIOS file
./build/zonistation-one -b path/to/bios.bin

# Verbose logging
./build/zonistation-one -b path/to/bios.bin -v

# Debug mode
./build/zonistation-one -b path/to/bios.bin -v -d
```

### Command Line Options

- `-h, --help` - Show help message
- `-b, --bios <file>` - Specify BIOS file path
- `-v, --verbose` - Enable verbose logging
- `-d, --debug` - Enable debug mode
- `--version` - Show version information

## PlayStation One Hardware

### Technical Specifications

- **CPU**: MIPS R3000A 32-bit RISC @ 33.8688 MHz
- **Memory**: 2MB main RAM, 1MB video RAM, 512KB sound RAM
- **GPU**: Custom 2D/3D graphics synthesizer
- **SPU**: 24-channel ADPCM sound processor
- **Storage**: CD-ROM (1x speed, 150 KB/s)

### Memory Map

| Address Range | Size | Description |
|---------------|------|-------------|
| 0x00000000-0x001FFFFF | 2MB | Main RAM |
| 0x1F800000-0x1F8003FF | 1KB | Scratchpad |
| 0x1F801000-0x1F802FFF | 8KB | Hardware Registers |
| 0x1FC00000-0x1FC7FFFF | 512KB | BIOS ROM |

## BIOS Files

The emulator requires a PlayStation One BIOS file to function. Common BIOS files:

- **SCPH1001.BIN** - North America NTSC
- **SCPH1002.BIN** - Europe PAL  
- **SCPH1000.BIN** - Japan NTSC

Place BIOS files in the `bios_files/` directory or specify the path using the `-b` option.

## Development Status

### Implemented ✅
- Project structure and build system
- Logging and debugging infrastructure
- Memory management system
- Basic CPU framework
- GPU, SPU, CD-ROM stub implementations
- Command line interface

### TODO 🚧
- **CPU**: Complete MIPS R3000A instruction set implementation
- **GPU**: Command processing and rendering pipeline
- **SPU**: Audio synthesis and ADPCM decoding
- **CD-ROM**: Disc image loading and sector reading
- **Controllers**: Input handling and controller protocols
- **DMA**: Direct memory access between components
- **Timers**: System timing and interrupt generation

### Future Enhancements 🔮
- Save state support
- Memory cards
- Multi-threading
- Hardware acceleration
- GUI interface
- Debugger interface

## Architecture

The emulator follows a modular design inspired by PCSX Redux:

```
┌─────────────┐
│    Main     │ - Entry point, command line parsing
└──────┬──────┘
       │
┌──────▼──────┐
│  Emulator   │ - Central orchestration, timing
└──────┬──────┘
       │
   ┌───┴───┐
   │       │
┌──▼─┐  ┌─▼──┐
│CPU │  │GPU │  - Core components
└────┘  └────┘
   │       │
┌──▼─┐  ┌─▼──┐
│SPU │  │I/O │  - Audio and peripherals
└────┘  └────┘
   │
┌──▼──────┐
│ Memory  │ - Unified memory management
└─────────┘
```

## Contributing

1. Fork the repository
2. Create a feature branch
3. Make your changes
4. Test thoroughly
5. Submit a pull request

### Code Style

- Use C99 standard
- 4-space indentation
- Clear, descriptive naming
- Comprehensive error handling
- Detailed logging for debugging

## License

This project is open source. See LICENSE file for details.

## Acknowledgments

- **PCSX Redux** - Architectural inspiration and reference implementation
- **PlayStation Development Community** - Hardware documentation and research
- **MIPS Technologies** - Processor documentation

## Contact

- Author: ZioZoni95
- Project: ZonistationOne
- Repository: [GitHub Link]

---

*ZonistationOne - Bringing PlayStation One games back to life through accurate emulation.*