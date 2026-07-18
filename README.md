# ZoniStation One

PlayStation 1 emulator written in C99. SDL2 + OpenGL 3.3 Core (GLEW). Early development.

---

## Screenshots

### Ace Combat 2 (Europe) — correct SCEE boot logo (July 2026)
![SCEE boot logo](Screenshot%202026-07-14%20191115.png)

As of July 2026, `games/Ace Combat 2 (Europe).cue` boots past the license screen into the game itself — a real commercial disc, not just the BIOS menu.

### Ace Combat 2 (Europe) — past the Namco publisher screen, FMV blocker visible (July 2026)
![Produced by Namco](Screenshot%202026-07-18%20115008.png)
![Black screen, VRAM filling with asset data](Screenshot%202026-07-18%20115014.png)

Boot now reaches the "Presented by Namco" publisher screen and progresses further — the VRAM Viewer (right panel, second screenshot) shows real texture/asset data being written by the game immediately after, but the display itself goes black. This matches the current top blocker: the FMV intro that plays here needs a working MDEC pipeline that isn't wired into the event scheduler yet (see "Current Status" below).

### Ace Combat 2 (Europe) — real main menu + in-engine 3D rendering (July 2026)
![Ace Combat 2 main menu](Screenshot%202026-07-18%20125937.png)
![In-engine 3D cockpit view](Screenshot%202026-07-18%20125717.png)

Milestone: after fixing a stack-overflow crash, a CDROM IRQ edge-detection gap, and an MDEC input-FIFO overflow (all July 2026), boot now reaches `Ace Combat 2`'s real, fully-textured main menu — logo, background, "START GAME / LOAD / OPTION" — and a real in-engine 3D-rendered view (cockpit HUD, lit instrument panels) is reachable from there. The FMV intro that plays before this is still skipped/not working and GPU rendering accuracy is still rough in places (see `GPU_GAP_ANALYSIS_2026-07-15.md`) — this is progress on booting a real commercial game to its actual menu/engine, not a claim of finished gameplay.

### BIOS Menu + ImGui Debug IDE (May 2026)
![BIOS Menu with Debug UI](screenshots/Screenshot%202026-05-02%20212421.png)

### Sony Logo (Feb 2026)
![Sony Logo](screenshots/Screenshot%202026-02-22%20153306.png)

---

## Build & Run

```sh
make
./myps1_emu roms/SCPH1001.BIN                                          # BIOS menu
./myps1_emu roms/SCPH1001.BIN --game="games/Ace Combat 2 (Europe).cue"  # game
```

Requirements: `gcc`, `g++`, `SDL2`, `OpenGL`, `GLEW`

BIOS tested: SCPH-1001 (US), SCPH-7502 (PAL)

---

## Current Status (July 2026)

BIOS boots to the interactive menu, and real commercial discs now boot substantially further than ever before — `Ace Combat 2 (Europe)` reaches its real, fully-textured main menu and an in-engine 3D-rendered cockpit view, after a string of July 2026 fixes: a CDROM sector-buffer bug (Request Register resetting the read pointer on the wrong arm/disarm transition), a stack-overflow crash (multi-MB structs declared as stack locals in `main()`), a CDROM IRQ edge-detection gap, MEMCTRL registers gaining real backing storage instead of hardcoded readback values, and an MDEC input-FIFO overflow (channels 0/1 now interleave via the event scheduler instead of one channel blasting through synchronously and dropping data). **Still rough**: the FMV intro that plays before the main menu is skipped/not working (MDEC decodes real bitstream data but doesn't yet run a full frame to completion or reliably reach the display), GPU rendering accuracy has known gaps (see `GPU_GAP_ANALYSIS_2026-07-15.md`), and the emulator is not yet stable enough to call this reliable end-to-end gameplay. GPU, DMA, timers, CDROM, SIO, GTE, I-Cache, SPU, and a from-scratch MDEC (ported from DuckStation's reference algorithms) are all implemented. The renderer has a full ImGui IDE-style debug interface with CPU disassembler, breakpoint manager, execution tracer, and per-component log windows — including a BIOS/game TTY log with real `printf`-style argument substitution.

Outstanding: FMV/video playback doesn't reliably work yet (see above) — MDEC decodes real data but the pipeline isn't fully wired end-to-end. GPU mask-bit handling and VRAM readback have known gaps affecting some rendering techniques (see `GPU_GAP_ANALYSIS_2026-07-15.md`). SPU audio sync has minor timing issues. Overall stability is still fragile — this is early confirmation a real commercial disc *can* reach its menu/engine, not a claim of solid end-to-end play.

### Component Status

| Component         | Status       | Notes |
|-------------------|--------------|-------|
| CPU (MIPS R3000A) | Complete     | All instructions, COP0, exceptions, load delay, branch delay |
| I-Cache           | Complete     | 256-line 4-word with tag/valid bits |
| RAM               | Complete     | 2 MB main + 1 KB scratchpad |
| BIOS ROM          | Complete     | SCPH-1001/SCPH-7502; boot → menu → real game working |
| IRQ Controller    | Complete     | Edge-triggered I_STAT/I_MASK, CPU Cause.IP2 |
| Event Scheduler   | Complete     | DuckStation-style downcount; VBlank, timers, CDROM, DMA |
| DMA               | Good         | Ch2 (GPU linked-list + block), Ch3 (CDROM), Ch4 (SPU), Ch6 (OTC) complete; Ch0/1 (MDEC) wired, lightly tested |
| Timers 0/1/2      | Good         | Correct clock sources (sysclk, dotclock, hblank, sysclk/8), IRQ; sync-mode gating not yet implemented |
| CDROM             | Good         | Async event-driven; GetStat, SetMode, GetID, disc region detection, real-game file reads verified |
| SIO / Controller  | Good         | Digital pad protocol; keyboard→gamepad (WASD/SPACE/E/C/Z/X); memory card slots 1+2 |
| GTE               | Good         | All 22 opcodes, correct math/saturation/UNR-divide; per-op cycle cost not yet enforced on the CPU |
| GPU               | Good         | Polygons, rects, lines, textured, VRAM double-buffer, GP0 FIFO, CLUT; mask-bit + VRAM-readback gaps on rasterized primitives |
| Renderer          | Good         | OpenGL 3.3, threaded double-buffered submission, 4-mode semi-transparency, texture-window masking |
| Debugger / UI     | Good         | ImGui disassembler, breakpoints, exec trace, registers, per-category log windows |
| SPU               | Good         | 24 voices, XA-ADPCM, ADSR, reverb, DMA, IRQ, SDL audio output |
| MDEC              | Implemented  | IDCT/YUV-RGB/macroblock decode ported from DuckStation; not yet exercised by a real FMV game |
| PCDrv             | Implemented  | Host filesystem side-channel for homebrew/dev builds |

---

## Debug UI

The main SDL2 window is an ImGui DockSpace. All output goes through ImGui — no terminal logging.

- **PS1 Display** — FBO rendered into a dockable/floatable ImGui window
- **Disassembly** — Virtual 128-row list; PC highlight (yellow), breakpoint highlight (dark red), clickable BP dots, Go-To-Address, live execution trace with dump-to-file
- **CPU Registers** — PC / SR / Cause / EPC / HI / LO + 32 GPRs with MIPS names; non-zero highlighted
- **Breakpoints** — Add/remove/enable/disable breakpoints by address; click to jump disassembly
- **Per-category log windows** — One per hardware category (CPU, GPU, CDROM, BIOS, DMA, IRQ, SPU, …); individually dockable, live level selector
- **BIOS/game TTY** — Real printf-style output with argument substitution ($a1-$a3 + stack, MIPS o32 varargs)
- **Keyboard shortcuts** — F5 run/pause, F11 single step
- **Options menu** — Live log level selector (TRACE → SILENT)

Multi-viewport enabled: any window can be dragged outside the main SDL2 window.

---

## Architecture

```
src/main.c                — SDL loop, 33868800/60 cycles/frame, ImGui DockSpace host

src/cpu/
  cpu_execution.c          — main CPU loop (DuckStation-style downcount)
  cpu_instructions.c       — MIPS R3000A instruction execute
  cpu_decode.c              — instruction decode
  cpu_disasm.c              — disassembler
  cpu_init.c                — CPU init, register reset
  cpu_registers.c            — register file helpers
  cpu_exceptions.c          — EXCEPTION_* handler, EPC/SR/Cause
  cpu_bios.c                — A0/B0 syscall side-channel (LLE TTY capture + printf substitution)
  cpu_icache.c               — 256-line 4-word instruction cache

src/core/
  interconnect.c            — init, CDROM event scheduling, TTY buffer
  bus.c                      — memory routing: load/store 32/16/8
  bus_irq.c                  — IRQ edge-triggered controller (I_STAT / I_MASK)
  ram.c                      — 2 MB RAM
  bios.c                     — BIOS ROM load
  dma.c                      — DMA channels (GPU ch2, CDROM ch3, SPU ch4, OTC ch6, MDEC ch0/1)
  timers.c                   — PSX timers 0/1/2
  sio.c                      — SIO / JOY controller protocol
  controller.c               — keyboard → PSX gamepad mapping
  mdec.c                     — Motion DECoder (IDCT/YUV-RGB, ported from DuckStation)
  pcdrv.c                    — PC drive side-channel
  debugger.c                 — CPU/memory breakpoint + watchpoint debugger
  event_scheduler.c          — DuckStation-style event dispatch (VBlank, timers, CDROM)

src/gpu/
  gpu.c                      — GPU init/reset/GP1/GPUSTAT
  gpu_commands.c              — GP0 256-entry dispatch table, all draw commands
  gpu_helpers.c                — GP0 helper utilities
  renderer.c                   — OpenGL 3.3 renderer (VAO, shaders, VRAM blit)
  vram.c                       — 1024×512 VRAM buffer management

src/gte/
  gte.c                        — GTE register I/O, command dispatch
  gte_ops.c                     — all GTE operations

src/cdrom/
  cdrom.c                       — CDROM controller + command dispatch
  cdrom_commands.c              — all CDROM command handlers
  cdrom_disc.c                  — disc image read (CUE/BIN)
  cdrom_audio.c                 — CDROM audio (XA/CDDA)

src/spu/
  spu.c                         — SPU init/register I/O
  spu_voice.c                   — 24-voice management
  spu_adsr.c                    — ADSR envelope
  spu_mixing.c                  — stereo mix output
  spu_dma.c                     — SPU DMA transfers
  spu_irq.c                     — SPU IRQ logic

src/utils/
  log.c                         — 17-category / 6-level logger
  rxi_log.c                     — rxi log backend

src/debug_ui.cpp                — ImGui debug UI (C++)
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

17 categories: `SYSTEM CPU IRQ DMA GPU CDROM TIMER BIOS INTERCONNECT RENDERER EVENT GTE VRAM RAM DEBUG MDEC SPU`

6 levels: `SILENT ERROR WARN INFO DEBUG TRACE`

Macro pattern: `LOG_<CATEGORY>_<LEVEL>(fmt, ...)` — e.g. `LOG_GPU_DEBUG("gp0=0x%08x", v)`

---

## References

### Documentation
- [PSX-SPX (Nocash)](https://psx-spx.consoledev.net/) — primary hardware reference: CPU, GPU, DMA, timers, CDROM, SPU, memory map
- [Nocash PSX](http://problemkaputt.de/psx.htm) — alternate mirror + additional undocumented register notes

### Reference Emulators
- [DuckStation](https://github.com/stenzek/duckstation) — CPU cycle model (downcount), event scheduler architecture, MFHI/MFLO stall latencies, GPU command dispatch table pattern, MDEC algorithms, CDROM sector-buffer semantics
- [PCSX-Redux](https://github.com/grumpycoders/pcsx-redux) — ImGui IDE-style debug UI architecture, FBO display pattern, disassembler window design

### Guide
- *PlayStation Emulation Guide* by Lionel Flandrin (`guide.tex`, ~11K lines) — primary implementation reference used throughout: CPU, DMA, GPU commands, OpenGL renderer, debugger, I-cache. Original emulator written in Rust ([simias/psx-rs](https://github.com/simias/psx-rs)); ZonistationOne adapted to C.

*Reference emulators used for understanding hardware behavior only. All code is original.*

---

## License

Educational purposes only. PlayStation is a trademark of Sony Interactive Entertainment.
