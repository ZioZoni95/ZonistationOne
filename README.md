# ZoniStation One (PlayStation 1) Emulator

A work-in-progress PlayStation 1 emulator written in C (C99), inspired by nocash and PSX-Spex documentation.

---

## 🏁 Current Status (July 2025)

- The emulator displays the PlayStation boot logo, but the animation is stuck and does not progress.
- All core systems (CPU, GPU, VRAM, RAM, DMA, Renderer, CDROM, Interconnect, Timers) are working well enough to reach this point.
- **Blocker:** The event/IRQ system is incomplete—BIOS is not receiving VBlank/timer IRQs as expected, so the animation cannot continue.

### Component Status

| Component   | Status     | Boot Critical | Notes/Action Needed                |
|-------------|------------|---------------|------------------------------------|
| CPU         | EXCELLENT  | No            | Fully working                      |
| RAM         | EXCELLENT  | No            | Fully working                      |
| VRAM        | EXCELLENT  | No            | Fully working                      |
| Renderer    | EXCELLENT  | No            | Fully working                      |
| CDROM       | EXCELLENT  | No            | Fully working                      |
| BIOS        | EXCELLENT  | No            | Fully working                      |
| DMA         | PARTIAL    | **YES**       | Add handlers, event/IRQ, timing    |
| Timers      | PARTIAL    | **YES**       | Add cycle/rate, event/IRQ, timing  |
| Events      | PARTIAL    | **YES**       | Ensure all event types, integration|
| GPU         | EXCELLENT* | Maybe         | Check status bits/timing           |
| GTE         | STUBS      | No            | Needed for 3D games                |
| MDEC        | MISSING    | No            | Needed for FMVs                    |
| SIO         | MISSING    | No            | Needed for input/memcard           |
| SPU         | MISSING    | No            | Needed for sound                   |

---

## 🚨 What's Blocking Boot?
- **Event/IRQ System:** BIOS is not receiving VBlank/timer IRQs in the way it expects, so the animation is stuck.
- **DMA/Timers:** Need channel handlers, event scheduling, and IRQ delivery for correct operation.
- **Event Loop Integration:** The main emulation loop must check and dispatch events as needed.

---

## What's Next (Critical Path)
1. Implement DMA channel handlers, event scheduling, and IRQs.
2. Add cycle-accurate timer/event scheduling and IRQ delivery.
3. Ensure the event system is robust and fully integrated.
4. Double-check GPU status bits/timing for BIOS compatibility.

**Once these are fixed, the BIOS will progress past the logo and boot games.**

---

## Features
- MIPS R3000A CPU emulation (with complete exception/interrupt support)
- GPU command parsing and OpenGL-based renderer
- DMA controller (partial)
- CDROM, RAM, VRAM, Timers, GTE (partial)
- BIOS syscall and kernel support
- Per-component logging and debug system
- Modular codebase (src/ for sources, include/ for headers)

## Building
```sh
make
```
Requires: gcc, SDL2, OpenGL, GLEW

## Running
```sh
./myps1_emu [options] <BIOS_PATH>
```
Default BIOS path: `roms/SCPH1001.BIN`

### Command Line Options
| Option | Description |
|--------|-------------|
| `--debug` | Set log level to DEBUG (verbose output) |
| `--trace` | Set log level to TRACE (ultra-verbose, per-instruction/cycle) |
| `--quiet` | Set log level to WARN (minimal output) |
| `--log-rate-limit=N` | Only log first N debug/trace messages per component, then every Nth |
| `--log-single-file` | Log everything to `emulator_log.txt` |
| `--help` or `-h` | Show help message |

## Debugging
- Per-component logs in `logs/` directory (cpu.txt, gpu.txt, bios.txt, etc.)
- Log levels: FATAL, ERROR, WARN, INFO, DEBUG, TRACE
- Rate limiting and single-file logging supported
- GDB debugging supported (see code for breakpoints and variable inspection)

## Development Philosophy
- One component at a time: implement, test, and verify each independently
- PSX-Spex compliance first
- Minimal dependencies
- Comprehensive testing

## License
This project is for educational purposes only. PlayStation is a trademark of Sony Interactive Entertainment.

## References
- [PSX-Spex Documentation](https://psx-spx.consoledev.net/)
- [nocash PSX Documentation](http://problemkaputt.de/psx.htm)
- [MIPS R3000A Architecture](https://en.wikipedia.org/wiki/MIPS_architecture)
- [PCSX ReARMed](https://github.com/notaz/pcsx_rearmed) is used for learning and cross-checking only. No code is copied.


---

*PCSX ReARMed is used as a reference for hardware behavior only. No code is copied; all code is original.* 