# ZonistationOne - PlayStation 1 Emulator

A PlayStation 1 emulator written in modern C++, inspired by PCSX-Redux but built from scratch with a focus on accuracy, performance, and clean architecture.

## Project Status

🚧 **Early Development** - This emulator is in the very early stages of development. Currently implemented:

- [x] Basic project structure and build system
- [x] Core emulator framework with component separation
- [x] Memory management system with PS1 memory map
- [x] MIPS R3000A CPU core (basic stub)
- [x] GPU subsystem (basic stub)
- [x] SPU audio subsystem (basic stub)  
- [x] CD-ROM drive emulation (basic stub)
- [x] BIOS loading support

### Next Steps

- [ ] Complete MIPS R3000A instruction set implementation
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