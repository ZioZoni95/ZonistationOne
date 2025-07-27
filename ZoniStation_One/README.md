# ZoniStation One - PlayStation One Emulator

A modern, high-performance PlayStation One emulator written in C/C++.

## Features

- **High Accuracy**: Cycle-accurate MIPS R3000A CPU emulation
- **Modern Architecture**: Clean, modular codebase with modern C++ features
- **Cross-Platform**: Support for Windows, Linux, and macOS
- **Plugin System**: Extensible architecture for graphics, audio, and input
- **HLE BIOS**: High-Level Emulation BIOS for maximum compatibility
- **Dynamic Recompilation**: Fast CPU emulation with JIT compilation
- **Memory Card Support**: Full memory card emulation
- **CD-ROM Support**: Multiple disc image formats (.bin/.cue, .iso, .chd)

## Project Structure

```
ZoniStation_One/
├── src/                    # Source code
│   ├── core/              # Core emulation engine
│   ├── cpu/               # CPU emulation (MIPS R3000A)
│   ├── memory/            # Memory management
│   ├── hardware/          # Hardware components
│   ├── bios/              # BIOS emulation
│   ├── plugins/           # Plugin system
│   └── utils/             # Utility functions
├── include/               # Header files
├── tests/                 # Unit tests
├── docs/                  # Documentation
├── scripts/               # Build scripts
└── third_party/           # Third-party dependencies
```

## Building

### Prerequisites

- CMake 3.15 or higher
- C++17 compatible compiler (GCC 7+, Clang 6+, MSVC 2017+)
- SDL2 development libraries
- OpenGL development libraries

### Build Instructions

```bash
mkdir build
cd build
cmake ..
make -j$(nproc)
```

## Usage

```bash
./zonistation_one [options] <game_file>
```

### Options

- `--bios <file>`: Specify BIOS file
- `--memcard <file>`: Specify memory card file
- `--gpu <plugin>`: Select GPU plugin
- `--spu <plugin>`: Select SPU plugin
- `--fullscreen`: Start in fullscreen mode
- `--help`: Show help information

## License

This project is licensed under the MIT License - see the LICENSE file for details.

## Acknowledgments

- Based on research of various PlayStation emulation projects
- Inspired by the accuracy and performance of existing emulators
- Built with modern software engineering practices 