# ZoniStation One

PlayStation 1 emulator written in C11. SDL2 + OpenGL 3.3 Core (GLEW). Early development.

---

## Screenshots

### Sony Logo (Feb 2026)
![Sony Logo](screenshots/Screenshot%202026-02-22%20153306.png)

### BIOS Menu + ImGui Debug IDE (May 2026)
![BIOS Menu with Debug UI](screenshots/Screenshot%202026-05-02%20212421.png)

---

## Build & Run

```sh
make
./myps1_emu roms/SCPH1001.BIN                              # BIOS menu
./myps1_emu roms/SCPH1001.BIN "games/Ace Combat 2 (Europe).cue"  # game
```

Requirements: `gcc`, `g++`, `SDL2`, `OpenGL`, `GLEW`

BIOS used: SCPH-1001 (US)

---

## Current Status (May 2026)

BIOS boots to the interactive menu. GPU, DMA, timers, CDROM, SIO, GTE, I-Cache, and SPU all implemented. The renderer has a full ImGui IDE-style debug interface with CPU disassembler, breakpoint manager, and per-component log windows.

Outstanding: GPU rendering has remaining accuracy gaps (some BIOS menu sprites missing, text artifacts). SPU audio sync has minor timing issues. MDEC not implemented.

### Component Status

| Component         | Status       | Notes |
|-------------------|--------------|-------|
| CPU (MIPS R3000A) | Complete     | All instructions, COP0, exceptions, load delay, branch delay |
| I-Cache           | Complete     | 256-line 4-word with tag/valid bits |
| RAM               | Complete     | 2 MB main + 1 KB scratchpad |
| BIOS ROM          | Complete     | SCPH-1001 US; boot → menu working |
| IRQ Controller    | Complete     | Edge-triggered I_STAT/I_MASK, CPU Cause.IP2 |
| Event Scheduler   | Complete     | DuckStation-style downcount; VBlank, timers, CDROM |
| DMA               | Good         | Ch2 (GPU linked-list + block), Ch6 (OTC); Ch3/0/1 stubs |
| Timers 0/1/2      | Complete     | Correct clock sources (sysclk, dotclock, hblank, sysclk/8), IRQ |
| CDROM             | Good         | Async event-driven; GetStat, SetMode, GetID, disc read |
| SIO / Controller  | Good         | Digital pad protocol; keyboard→gamepad (WASD/SPACE/E/C/Z/X) |
| GTE               | Good         | Geometry transforms, load delay slots |
| GPU               | Complete     | Polygons, rects, lines, textured, VRAM double-buffer, GP0 FIFO, CLUT |
| Renderer          | Partial      | OpenGL 3.3, partial VRAM upload, ivec4 tex window |
| Debugger / UI     | Good         | ImGui disassembler, breakpoints, registers, 16 log windows |
| SPU               | Good         | 24 voices, XA-ADPCM, ADSR, reverb, DMA, IRQ, SDL audio output |
| MDEC              | Not started  | Needed for FMV |

---

## Debug UI

The main SDL2 window is an ImGui DockSpace. All output goes through ImGui — no terminal logging.

- **PS1 Display** — FBO rendered into a dockable/floatable ImGui window
- **Disassembly** — Virtual 128-row list; PC highlight (yellow), breakpoint highlight (dark red), clickable BP dots, Go-To-Address
- **CPU Registers** — PC / SR / Cause / EPC / HI / LO + 32 GPRs with MIPS names; non-zero highlighted
- **Breakpoints** — Add/remove/enable/disable breakpoints by address; click to jump disassembly
- **16 log windows** — One per hardware category (CPU, GPU, CDROM, BIOS, DMA, IRQ, SPU, …); individually dockable
- **Keyboard shortcuts** — F5 run/pause, F11 single step
- **Options menu** — Live log level selector (TRACE → SILENT)

Multi-viewport enabled: any window can be dragged outside the main SDL2 window.

---

## Architecture

```
src/main.c               — SDL loop, 33868800/60 cycles/frame
src/cpu/
  cpu_execution.c        — main CPU loop (DuckStation-style downcount)
  cpu_instructions.c     — MIPS R3000A decode/execute
  cpu_init.c             — CPU init, register reset
  cpu_exceptions.c       — EXCEPTION_* handler, EPC/SR/Cause
  cpu_bios.c             — A0/B0 syscall side-channel (LLE TTY capture)
  cpu_icache.c           — 256-line 4-word instruction cache
src/bus.c                — memory routing: load/store 32/16/8, DMA logic, BIOS helpers
src/bus_irq.c            — IRQ edge-triggered controller (I_STAT / I_MASK)
src/interconnect.c       — init, CDROM event scheduling, TTY buffer
src/gpu.c                — GPU init/reset/GP1/GPUSTAT
src/gpu_commands.c       — GP0 256-entry dispatch table, all draw commands
src/renderer.c           — OpenGL 3.3 renderer (VAO, VBOs, shaders, partial VRAM upload)
src/cdrom.c              — CDROM controller + command dispatch
src/cdrom_commands.c     — all CDROM command handlers
src/cdrom_disc.c         — disc image read (CUE/BIN)
src/timers.c             — PSX timers 0/1/2
src/sio.c                — SIO / JOY controller protocol
src/controller.c         — keyboard → PSX gamepad mapping
src/event_scheduler.c    — DuckStation-style event dispatch (VBlank, timers, CDROM)
src/gte.c                — Geometry Transformation Engine
src/spu.c                — SPU init, step, register map
src/spu_voice.c          — 24-voice XA-ADPCM decoder, Gaussian interpolation
src/spu_adsr.c           — ADSR envelope (Attack/Decay/Sustain/Release)
src/spu_mixing.c         — stereo mix, reverb (IIR/comb/allpass), noise
src/spu_dma.c            — DMA transfer (manual/DMA read/write), IRQ9
src/spu_irq.c            — SPU IRQ address boundary detection
src/debugger.c           — breakpoints, watchpoints, step/pause logic
src/debug_ui.cpp         — ImGui debug interface (C++)
src/log.c                — 16 categories × 6 levels, per-category filter
```

### Memory Map (physical addresses)

| Region | Range |
|--------|-------|
| RAM | 0x00000000 – 0x001FFFFF |
| Scratchpad | 0x1F800000 – 0x1F8003FF |
| SIO / JOY | 0x1F801040 – 0x1F80104F |
| IRQ regs | 0x1F801070 – 0x1F801077 |
| DMA | 0x1F801080 – 0x1F8010FF |
| Timers | 0x1F801100 – 0x1F80112F |
| CDROM | 0x1F801800 – 0x1F801803 |
| GPU | 0x1F801810 – 0x1F801817 |
| SPU | 0x1F801C00 – 0x1F801E7F |
| EXP2 (BIOS TTY) | 0x1F802000 – 0x1F803FFF |
| BIOS ROM | 0x1FC00000 – 0x1FC7FFFF |

---

## Logging

16 categories: `SYSTEM CPU IRQ DMA GPU CDROM TIMER BIOS INTERCONNECT RENDERER EVENT GTE VRAM RAM SPU DEBUG`

6 levels: `SILENT ERROR WARN INFO DEBUG TRACE`

Macro pattern: `LOG_<CATEGORY>_<LEVEL>(fmt, ...)` — e.g. `LOG_GPU_DEBUG("gp0=0x%08x", v)`

---

## References

### Documentation
- [PSX-SPX (Nocash)](https://psx-spx.consoledev.net/) — primary hardware reference: CPU, GPU, DMA, timers, CDROM, SPU, memory map
- [Nocash PSX](http://problemkaputt.de/psx.htm) — alternate mirror + additional undocumented register notes

### Reference Emulators
- [DuckStation](https://github.com/stenzek/duckstation) — CPU cycle model (downcount), event scheduler architecture, MFHI/MFLO stall latencies, GPU command dispatch table pattern
- [PCSX-Redux](https://github.com/grumpycoders/pcsx-redux) — ImGui IDE-style debug UI architecture, FBO display pattern, disassembler window design

### Guide
- *PlayStation Emulation Guide* by Lionel Flandrin (`guide.tex`, ~11K lines) — primary implementation reference used throughout: CPU, DMA, GPU commands, OpenGL renderer, debugger, I-cache. Original emulator written in Rust ([simias/psx-rs](https://github.com/simias/psx-rs)); ZonistationOne adapted to C.

*Reference emulators used for understanding hardware behavior only. All code is original.*

---

## License

Educational purposes only. PlayStation is a trademark of Sony Interactive Entertainment.
