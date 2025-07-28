# ZoniStationOne Development Guide

## Project Overview

ZoniStationOne is a PlayStation One emulator written in C, designed to be clean, well-documented, and educational. The project is inspired by PCSX-ReARMed but built from the ground up with modern practices.

## Project Structure

```
ZoniStationOne/
├── src/
│   ├── core/           # Core emulation engine
│   │   ├── zoni_common.c    # Common utilities and logging
│   │   ├── zoni_memory.c    # Memory management
│   │   ├── zoni_cpu.c       # CPU emulation (Foundation Complete)
│   │   └── zoni_emulator.c  # Main emulator logic (TODO)
│   ├── plugins/        # GPU, SPU, and input plugins
│   │   ├── gpu/        # Graphics plugins
│   │   ├── spu/        # Sound plugins
│   │   └── input/      # Input plugins
│   ├── frontend/       # User interface
│   │   └── main.c      # Main entry point
│   └── include/        # Header files
│       ├── zoni_common.h     # Common definitions
│       ├── zoni_cpu.h        # CPU interface (Foundation Complete)
│       ├── zoni_memory.h     # Memory interface
│       └── zoni_emulator.h   # Main emulator interface
├── docs/              # Documentation
├── tests/             # Unit tests
├── tools/             # Development tools
├── Makefile          # Build system
├── configure         # Configuration script
└── README.md         # Project overview
```

## Building the Project

### Prerequisites

- GCC or Clang compiler
- Make
- SDL2 (optional, for frontend)
- OpenGL (optional, for GPU plugins)

### Build Instructions

```bash
# Configure the project
./configure

# Build the emulator
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

## Development Guidelines

### Code Style

- Use C99 standard
- Follow consistent naming conventions:
  - Functions: `zoni_module_function_name`
  - Types: `zoni_type_name_t`
  - Constants: `ZONI_CONSTANT_NAME`
  - Macros: `ZONI_MACRO_NAME`
- Use meaningful variable and function names
- Add comments for complex logic
- Use header guards: `#ifndef ZONI_MODULE_H`

### Error Handling

- Use the `zoni_error_t` enum for return values
- Check return values from functions
- Use `zoni_log()` for error reporting
- Provide meaningful error messages

### Memory Management

- Use `zoni_malloc()`, `zoni_calloc()`, `zoni_realloc()`, `zoni_free()`
- Always check for NULL returns
- Clean up resources in shutdown functions

### Logging

Use the logging system for debugging and information:

```c
zoni_log(ZONI_LOG_INFO, "Initializing module");
zoni_log(ZONI_LOG_DEBUG, "Debug information: %d", value);
zoni_log(ZONI_LOG_WARNING, "Warning message");
zoni_log(ZONI_LOG_ERROR, "Error occurred: %s", error_string);
```

## Architecture Overview

### Core Components

1. **Memory System** (`zoni_memory.h/c`)
   - Manages PlayStation memory map
   - Handles memory access and protection
   - Provides debugging utilities

2. **CPU System** (`zoni_cpu.h/c`)
   - MIPS R3000A emulation
   - Interpreter and dynamic recompiler modes
   - Exception handling

3. **Emulator Core** (`zoni_emulator.h/c`)
   - Coordinates all components
   - Manages timing and synchronization
   - Handles plugin loading

### Plugin System

The emulator uses a plugin architecture for:
- **GPU Plugins**: Graphics rendering
- **SPU Plugins**: Sound processing
- **Input Plugins**: Controller input
- **CDROM Plugins**: CD-ROM emulation

### Memory Map

```
0x00000000 - 0x01FFFFFF: RAM (32MB, mirrored)
0x1F800000 - 0x1F8003FF: Scratchpad (1KB)
0x1F801000 - 0x1F802FFF: Hardware registers
0x1F801800 - 0x1F801803: CDROM controller
0x1F801C00 - 0x1F801FFF: SPU registers
0x1F000000 - 0x1F7FFFFF: Expansion
0x1FC00000 - 0x1FFFFFFF: BIOS (512KB)
0xFFFE0000 - 0xFFFEFFFF: Cache control
```

## Testing

### Unit Tests

Tests are located in the `tests/` directory. Run tests with:

```bash
make test
```

### Manual Testing

The main program includes comprehensive tests for:
- Memory system initialization and operations
- CPU register access and load delay slots
- CPU-memory integration
- Error handling and validation
- All tests passing with clean compilation

## Contributing

### Adding New Features

1. Create header file in `src/include/`
2. Implement in `src/core/` or appropriate plugin directory
3. Add to Makefile if needed
4. Update documentation
5. Add tests

### Code Review Checklist

- [ ] Code compiles without warnings
- [ ] All functions have proper error handling
- [ ] Memory is properly managed
- [ ] Logging is appropriate
- [ ] Documentation is updated
- [ ] Tests pass

## Debugging

### Debug Build

```bash
make debug
```

### Logging Levels

Set log level with environment variable:
```bash
export ZONI_LOG_LEVEL=DEBUG
./bin/zonistationone
```

### Memory Debugging

Use memory dump functions:
```c
zoni_memory_dump_region(memory, PSX_MEM_RAM, 0x1000, 64);
zoni_memory_dump_stats(memory);
```

### CPU Debugging

Use CPU debug functions:
```c
zoni_cpu_dump_registers(&cpu);
zoni_cpu_disassemble_instruction(&cpu, address, buffer, buffer_size);
```

## Current Implementation Status

### CPU Foundation & Instruction System (Complete ✅)

The CPU foundation and instruction fetch/decode system are now complete and follow the PCSX-ReARMed structure:

#### ✅ Implemented Components
- **Register Structure**: Complete MIPS R3000A register layout with proper byte ordering
- **Load Delay System**: Accurate MIPS load delay slot emulation with dual-slot system
- **Exception Handling**: Framework for CPU exceptions and interrupts
- **Memory Integration**: CPU properly connected to memory system
- **Debug Support**: Register dump and instruction disassembly ready
- **Instruction Fetch**: Reading 32-bit instructions from memory at program counter
- **Instruction Decode**: MIPS instruction format parsing (R-type, I-type, J-type)
- **Disassembly**: Basic instruction disassembly with proper byte order handling
- **Memory Byte Order**: Little-endian memory access for PlayStation compatibility

#### 🔄 Next Steps
- **Instruction Execution**: Execute decoded MIPS instructions
- **Register Updates**: Update register file based on instruction results
- **Memory Operations**: Implement load/store instruction execution
- **Branch/Jump Handling**: Implement control flow instructions

### Memory System (Complete ✅)
- Full PlayStation memory map implementation
- Memory region management and access control
- Statistics and debugging utilities

### Common Utilities (Complete ✅)
- Comprehensive logging system
- Error handling and memory management
- File, time, and math utilities

## Performance Considerations

- Use appropriate data structures
- Minimize memory allocations
- Profile critical paths
- Consider cache-friendly layouts
- Use SIMD instructions where possible

## Future Development

### Planned Features

- [x] CPU foundation (registers, load delay slots, memory integration)
- [x] CPU instruction fetch and decode system
- [ ] CPU instruction execution engine
- [ ] Dynamic recompiler
- [ ] GPU plugin system
- [ ] SPU plugin system
- [ ] Input plugin system
- [ ] CD-ROM emulation
- [ ] BIOS emulation (HLE)
- [ ] Save state support
- [ ] Debugger interface

### Architecture Improvements

- [ ] Plugin hot-swapping
- [ ] Multi-threading support
- [ ] JIT compilation
- [ ] Hardware acceleration
- [ ] Network multiplayer

## Resources

- [PlayStation Technical Reference](https://problemkaputt.de/psx-spx.htm)
- [MIPS R3000A Documentation](https://en.wikibooks.org/wiki/MIPS_Assembly/MIPS_Details)
- [PCSX-ReARMed Reference](https://github.com/notaz/pcsx_rearmed)

## License

This project is licensed under the GNU General Public License v2 or later. 