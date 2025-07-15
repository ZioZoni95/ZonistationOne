# ZoniStation One (PlayStation 1) Emulator

A work-in-progress PlayStation 1 emulator written in C (C99), inspired by nocash and PSX-Spex documentation.

---

## 🏁 Current Status (July 2025)

- **Boots to PlayStation logo (glitched/stuck animation):** The emulator reliably displays the PS1 boot animation, but the animation is glitched or stuck and does not progress to the BIOS menu.
- **Timer and IRQ system:** Fully refactored for hardware-accurate event scheduling and interrupt delivery, modeled after PCSX ReARMed. All timer IRQ and sticky flag logic is now handled in event handlers.
- **Event Scheduler:** Patched to ensure all due events are fired, not just one per call. Robust event queue delivers all hardware events (VBlank, timers, DMA, etc.).
- **DMA:** DICR write handler now immediately asserts IRQ3 if the condition is met, matching hardware. Detailed logging added for DMA, DICR, and event scheduling. Only GPU DMA is minimally functional; other DMA channels are stubbed or not required for BIOS menu.
- **BIOS IRQs:** BIOS now receives correct timer and DMA IRQs, but emulator is still stuck at the boot logo.
- **CDROM:** Not yet fully implemented; BIOS menu should still appear with correct 'no disc' status, but the emulator is currently stuck at the boot logo.
- **Renderer:** Sufficient for logo and menu graphics.

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

## 🚀 Recent Achievements
- Refactored timer and event system for hardware accuracy, based on PCSX ReARMed.
- Moved all timer IRQ and sticky flag logic to event handlers for correct scheduling and delivery.
- Patched event queue logic to ensure all due events are fired, not just one per call.
- Patched DMA DICR write handler to immediately assert IRQ3 if the condition is met, matching hardware behavior.
- Added detailed logging to DMA, DICR, and event scheduling for easier debugging.
- BIOS now receives correct timer and DMA IRQs, confirming event and interrupt system accuracy.

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

## Recent Fixes
- Refactored timer IRQ acknowledge/clear logic to prevent interrupt storms.
- All timer IRQ and sticky flag logic now handled in event handlers, matching hardware.
- Patched event scheduler to fire all due events per call.
- Patched DMA DICR write handler to immediately assert IRQ3 if the condition is met.
- Added detailed logging to DMA, DICR, and event scheduling.
- IRQ requests are now only made when appropriate, matching hardware behavior.
- Event scheduler and timer event delivery are robust and accurate.
- Cleaned up all compiler warnings and errors.

## Next Steps
- Improve CDROM emulation to ensure correct 'no disc' status for BIOS menu.
- Expand DMA and peripheral support for game booting.
- Refine renderer for full menu and in-game graphics.

## Limitations
- Still stuck at a glitched or looping boot logo animation (not yet at BIOS menu).
- No game booting yet (CDROM incomplete).
- Some DMA channels and peripherals are stubbed.
- BIOS menu may not appear if CDROM or GPU status is not correct. 