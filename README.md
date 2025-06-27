# Gemini PS1 Emulator

A work-in-progress PlayStation 1 emulator, inspired by nocash and PSX-Spex documentation.

## Features

- MIPS R3000A CPU emulation (with exception/interrupt support)
- GPU command parsing and OpenGL-based renderer
- DMA controller (partial)
- CDROM, RAM, VRAM, Timers, GTE (partial)
- BIOS syscall and kernel support
- Per-component logging and debug plan
- Modular codebase (src/ for sources, include/ for headers)

## Building

```sh
make
```

Requires: gcc, SDL2, OpenGL, GLEW

## Running

```sh
./myps1_emu
```

Place a valid PS1 BIOS (e.g., `SCPH1001.BIN`) in the `roms/` directory.

## Directory Structure

- `src/` - C source files
- `include/` - Header files
- `logs/` - Log output
- `roms/` - BIOS and ROM images
- `games/` - Game images
- `DEBUG_PLAN.md` - Ongoing debug plan and progress

## Documentation

- [PSX-Spex](https://psx-spx.consoledev.net/)
- [nocash PSX specs](https://problemkaputt.de/psx-spx.htm)

## Contributing

Pull requests and issues are welcome! Please see the debug plan for current focus.

## License

This project is for educational purposes. See PSX-Spex/nocash for documentation licensing. 