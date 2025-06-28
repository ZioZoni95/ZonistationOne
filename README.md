# ZoniStation One (PlayStation 1) Emulator

A work-in-progress PlayStation 1 emulator fully written in C (-std99) , inspired by nocash and PSX-Spex documentation.

## 🎯 **Current Status: CPU Exception Handling Complete**

**Latest Achievement:** ✅ **CPU Exception System Fully Implemented and Tested (June 2025)**
- Exception vector jumps (SYSCALL → 0x80000080) ✅
- ERET instruction recognition and execution ✅
- Register updates (EPC, Cause, Status) ✅
- Exception flow control with `exception_pending` flag ✅
- Strict compliance with PSX-Spex/nocash documentation ✅

## Features

- **MIPS R3000A CPU emulation** (with complete exception/interrupt support)
- **GPU command parsing** and OpenGL-based renderer
- **DMA controller** (partial)
- **CDROM, RAM, VRAM, Timers, GTE** (partial)
- **BIOS syscall and kernel support**
- **Per-component logging** and debug system
- **Modular codebase** (src/ for sources, include/ for headers)
- **Comprehensive testing suite** for component validation

## Building

```sh
make
```

Requires: gcc, SDL2, OpenGL, GLEW

## Testing

```sh
make test
```

Runs CPU exception handling tests to verify implementation.

## Running

### Basic Usage

```sh
./myps1_emu [options] <BIOS_PATH>
```

**Default BIOS path:** `roms/SCPH1001.BIN`

### Command Line Options

| Option | Description |
|--------|-------------|
| `--debug` | Set log level to DEBUG (verbose output) |
| `--trace` | Set log level to TRACE (ultra-verbose, per-instruction/cycle) (UNSTABLE! Cause Massive Logs use at own risk) |
| `--quiet` | Set log level to WARN (minimal output) |
| `--log-rate-limit=N` | Only log first N debug/trace messages per component, then every Nth |
| `--log-single-file` | Log everything to `emulator_log.txt` (disables per-component logs) |
| `--help` or `-h` | Show help message |

### Examples

```sh
# Run with default settings
./myps1_emu

# Run with debug logging
./myps1_emu --debug

# Run with ultra-verbose tracing (useful for debugging)
./myps1_emu --trace

# Run with rate-limited debug output (prevents log spam)
./myps1_emu --debug --log-rate-limit=1000

# Run with single log file
./myps1_emu --debug --log-single-file

# Run with custom BIOS
./myps1_emu --debug roms/SCPH5501.BIN
```

## Debugging

### Log System

The emulator features a comprehensive logging system with per-component logs:

#### Log Levels
- **FATAL** (0): Critical errors that cause program termination
- **ERROR** (1): Errors that don't terminate but indicate problems
- **WARN** (2): Warnings about potential issues
- **INFO** (3): General information (default level)
- **DEBUG** (4): Detailed debugging information
- **TRACE** (5): Ultra-verbose per-instruction/cycle logging

#### Log Output

**Default Mode:** Per-component logs in `logs/` directory
```
logs/
├── cpu.txt          # CPU execution logs
├── gpu.txt          # GPU command logs
├── bios.txt         # BIOS syscall logs
├── interconnect.txt # Memory/register access logs
├── cdrom.txt        # CD-ROM operation logs
├── dma.txt          # DMA transfer logs
├── timers.txt       # Timer operation logs
└── ...
```

**Single File Mode:** All logs to `emulator_log.txt`
- Automatically rotates when file exceeds 50MB
- Old log becomes `emulator_log.old.txt`

#### Rate Limiting

To prevent log spam during debugging:
```sh
# Log first 1000 debug messages, then every 1000th
./myps1_emu --debug --log-rate-limit=1000
```

### GDB Debugging

#### Compile with Debug Symbols

The Makefile already includes `-g` flag for debug symbols.

#### Basic GDB Usage

```sh
# Start emulator under GDB
gdb ./myps1_emu

# Set breakpoints on key functions
(gdb) break cpu_run_next_instruction
(gdb) break cpu_exception
(gdb) break interconnect_load32
(gdb) break gpu_gp0

# Run with arguments
(gdb) run --debug

# Continue execution
(gdb) continue

# Step through code
(gdb) next
(gdb) step

# Examine variables
(gdb) print cpu->pc
(gdb) print cpu->regs[4]
(gdb) print cpu->sr
```

#### Useful GDB Commands for PS1 Emulation

```sh
# Break on specific PC address
(gdb) break *0x80000080  # Exception vector

# Break on memory access
(gdb) watch -l cpu->pc

# Examine memory
(gdb) x/4x 0x80000000    # Examine 4 words at address
(gdb) x/16b cpu->pc      # Examine 16 bytes at PC

# Examine CPU state
(gdb) print *cpu
(gdb) print cpu->regs[0]@32  # All 32 registers

# Conditional breakpoints
(gdb) break cpu_exception if cause == 0x08  # Break on SYSCALL
(gdb) break interconnect_load32 if addr == 0x1f801810  # Break on GPU read
```

#### Debugging Common Issues

**CPU Stuck in Loop:**
```sh
(gdb) break cpu_run_next_instruction
(gdb) run --trace
(gdb) continue
# Check if PC is changing
(gdb) print cpu->pc
```

**Exception Handling:**
```sh
(gdb) break cpu_exception
(gdb) run --debug
# Examine exception cause and PC
(gdb) print cause
(gdb) print cpu->epc
```

**Timer Issues:**
```sh
(gdb) break timer_update
(gdb) run --debug
# Check timer state
(gdb) print *timer
```

## Project Status

### ✅ **Completed Components**
- **CPU Exception Handling**: Fully implemented and tested
- **Memory Management**: RAM and VRAM systems
- **Basic Interconnect**: Memory-mapped I/O routing
- **CDROM Controller**: Basic command handling
- **BIOS Interface**: Loading and syscall support

### 🎯 **Next Priority**
- **Timer 0 (VBlank)**: Implementation for proper interrupt timing
- **GPU Integration**: Complete graphics pipeline
- **DMA System**: Full DMA transfer implementation

### 🔄 **Future Work**
- **GTE Audit**: Geometry Transformation Engine verification
- **Full System Integration**: End-to-end testing
- **Game Compatibility**: Real game testing

## Development Philosophy

- **One component at a time** - Implement, test, and verify each component independently
- **PSX-Spex compliance first** - Follow documentation exactly
- **Minimal dependencies** - Each component should work with minimal assumptions
- **Comprehensive testing** - Each step includes specific test criteria

## Contributing

This is a personal project for learning PS1 emulation. The codebase follows PSX-Spex and nocash documentation for accuracy.

## License

This project is for educational purposes only. PlayStation is a trademark of Sony Interactive Entertainment.

## References

- [PSX-Spex Documentation](https://psx-spx.consoledev.net/)
- [nocash PSX Documentation](http://problemkaputt.de/psx.htm)
- [MIPS R3000A Architecture](https://en.wikipedia.org/wiki/MIPS_architecture) 