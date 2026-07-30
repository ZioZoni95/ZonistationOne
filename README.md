# ZoniStation One

A PlayStation 1 emulator written from scratch in C99, with an OpenGL 3.3 renderer and a built-in
debugger. Low-level: the real BIOS runs as-is, no syscall is faked, and games boot the way hardware
boots them.

SDL2 + OpenGL 3.3 Core (GLEW), ImGui for the debug interface. Early development, but real commercial
discs boot, play their FMV intros and reach their menus and 3D engines.

---

## Screenshots

### New debug interface — the pipeline view during Ace Combat 2 in-engine 3D (July 30 2026)
![Reorganised debug UI: machine bar, mode rail, stage and the CD→XA→MDEC→DMA→VRAM pipeline](Screenshot%202026-07-30%20192724.png)

The debug interface was rebuilt around what the emulator is actually used for: watching data cross
subsystem boundaries. A **machine bar** carries the live vitals (real-time %, frame ms, audio-queue
depth, SPU drift) that used to need a Lua script; a **mode rail** (Pipeline / Display / Frame / Code /
Memory / Audio / VRAM / Script, F1–F8) replaces the scattered floating panels; the emulated **screen
stays on the stage** in every mode. The **Pipeline** view puts CD → XA → MDEC → DMA → VRAM on one row
with live rates read from real counters, so a stage that stops feeding the next one is visible instead
of deduced. Design and rationale in `docs/ui/`. The two accents encode the data path — cyan is video,
rose is audio.

### Ace Combat 2 (Europe) — FMV intro playing on the main display (July 27 2026)
![FMV intro playing on the PS1 display](Screenshot%202026-07-27%20215006.png)

The full chain works end to end: CD-XA sector delivery, the game's own software bitstream decode, the
MDEC, the output DMA, the CPU→VRAM upload, and 24bpp scanout. Two defects stood between a correct
frame in VRAM and a correct frame on screen, and both were found by reading the GPU-side texture back
and comparing it against what the CPU had staged for it:

- 13 of the 20 macroblock columns per frame were being uploaded into the debug VRAM-Viewer texture
  instead of VRAM, because a reused per-frame command record kept a stale flag from an earlier frame.
- Everything the rasterizer drew came back out of VRAM with bit 15 set, because the fragment shader
  wrote a constant alpha — and in the unified VRAM texture, alpha *is* the PSX mask bit. Invisible at
  15bpp, but at 24bpp bit 15 is picture data, so black areas decoded as green.

### Ace Combat 2 (Europe) — the FMV intro decodes and reaches VRAM (July 26 2026)
![FMV decoded in VRAM](Screenshot%202026-07-26%20230210.png)

Before that, the movie died two seconds in. Two DMA bugs: completion interrupts were lost after the
first acknowledge (the interrupt line was never brought low, so no later completion produced an edge,
and the player's frame queue froze after 49 of 1905 frames), and block transfers on the GPU channel
read guest RAM lazily across scheduled slices — the movie player refills its staging buffer the moment
it kicks a transfer, so the deferred read picked up the *next* column's pixels.

### Full 3D PlayStation boot logo (July 2026)
![3D PlayStation boot logo](Screenshot%202026-07-23%20214744.png)

The spinning logo and its coloured base, not just the "Licensed by SCEE" text. This was a multi-session
blocker with a one-byte cause: the GTE's colour FIFO dropped the CODE byte when pushing a computed
colour, and the BIOS shell's software ordering-table renderer reads that byte back as the GP0 opcode —
so every lit logo primitive was discarded as opcode `0x00`.

### Ace Combat 2 (Europe) — real main menu and in-engine 3D
![Ace Combat 2 main menu](Screenshot%202026-07-18%20125937.png)
![In-engine 3D cockpit view](Screenshot%202026-07-18%20125717.png)

### Earlier milestones
![SCEE boot logo](Screenshot%202026-07-14%20191115.png)
![Produced by Namco](Screenshot%202026-07-18%20115008.png)
![BIOS Menu with Debug UI](screenshots/Screenshot%202026-05-02%20212421.png)
![Sony Logo](screenshots/Screenshot%202026-02-22%20153306.png)

---

## Build & Run

```sh
make
./myps1_emu roms/SCPH1001.BIN                                                    # BIOS menu
./myps1_emu "roms/SCPH-7502 (3).BIN" --game="games/Ace Combat 2 (Europe).cue"    # PAL game
```

Requirements: `gcc`, `g++`, `SDL2`, `OpenGL`, `GLEW`.

The BIOS must match the disc's region, as on hardware: a PAL disc booted with the US BIOS is rejected
and you are left at the BIOS menu. BIOS tested: SCPH-1001 (US), SCPH-7502 (PAL). The game path has to
be passed as `--game=<cue>`; a bare positional path is treated as the BIOS path.

Useful environment variables:

| Variable | Effect |
|---|---|
| `ZS1_LOG_LEVEL=silent\|error\|warn\|info\|debug\|trace` | Log level for the run (default `info`; the hot paths are genuinely expensive above it) |
| `ZS1_LOG_STDERR=1` | Also write the log to stderr, not just the in-app windows |
| `ZS1_LUA_SCRIPT=scripts/x.lua` | Run a Lua debug script at startup |
| `ZS1_DUMP_FRAME=path` | Dump a rendered frame as raw RGB (`ZS1_DUMP_FRAME_N` selects which) |
| `ZS1_AUDIO_DUMP=path` | Record exactly what is handed to the sound device, as raw interleaved 16-bit stereo |
| `ZS1_SPU_NO_REVERB=1` | Bypass the reverb stage — an A/B switch when judging where an artefact comes from |
| `ZS1_FRAME_PROFILE=1` | Log where each frame's wall-clock time goes (emulation, VRAM upload, viewer, submit) |

---

## Current status (July 28 2026)

The BIOS boots to its menu. `Ace Combat 2 (Europe)` boots from a real disc image, plays its FMV intro
correctly, and reaches its textured main menu and an in-engine 3D cockpit view.

**Audio now runs on the emulated clock** (July 28 2026). The SPU's DSP was already complete — 24
voices with ADPCM decode and Gaussian interpolation, the full ADSR state machine, the 32-register
reverb, noise, pitch modulation, DMA and the IRQ address watch — but sample generation was driven by
wall-clock time on its own thread while the game wrote its registers on the emulation thread, so the
two clocks drifted apart permanently and envelopes, note lengths and CD audio were all wrong.
Generation is now driven by emulated time: a scheduled event in 64-sample batches, plus a catch-up on
every SPU register access so a write flushes the audio it owes before changing state. Two DSP defects
came out from under that fix and are also gone: the reverb ran at twice its hardware rate and resolved
its buffer addresses four times too close, writing over the voices' own sample data, and XA sectors
claimed eight times their real sample count, so most of what reached the audio queue during FMV
playback was undecoded buffer contents. What the emulator hands the sound device is now clean — a
capture of it plays back correctly end to end.

**On speed**: the emulator keeps real time with the debug interface closed. Running it with the log
windows and inspector panels open, on WSL, does not — and when audio falls behind, the device's
callback pads the difference with silence, heard as crackle. Earlier notes here quoted "85-95% of real
time" as if that were the emulator's speed; that figure was measured with stderr logging and
per-vblank probes active, which cost more than the thing they were measuring, and it has been
withdrawn. What the debug interface costs when open, on a machine that is not WSL, has not been
measured yet.

**Savestates do not exist yet.**

Known rendering gaps: the mask-bit *test* (don't overwrite masked pixels) is not applied to rasterized
primitives, VRAM readback and VRAM→VRAM copy still read the CPU-side VRAM model, and texture sampling
reads a separate mirror — so render-to-texture effects don't work. The CRTC advances once per frame
rather than per scanline, which also leaves Timer0's hblank gate unwired. Details and priorities in
`GAP_ANALYSIS_REFACTOR_2026-07-13.md` and `GPU_GAP_ANALYSIS_2026-07-15.md`.

This is a real disc booting to its menu and playing its movies — not a claim of finished, stable
gameplay.

### Component status

| Component | Status | Notes |
|---|---|---|
| CPU (MIPS R3000A) | Working | All instructions, COP0, exceptions, branch/load delay, MULT/DIV and GTE stalls |
| I-Cache | Working | 256 lines × 4 words, tag + per-word valid bits |
| RAM / BIOS ROM | Working | 2 MB + 1 KB scratchpad; SCPH-1001 and SCPH-7502 |
| IRQ controller | Working | Edge-triggered I_STAT/I_MASK, every source wired |
| Event scheduler | Working | Single authority; wrap-safe scheduling |
| DMA | Working | All channels; linked-list and block transfers, completion interrupts |
| Timers 0/1/2 | Working | Derived counters, all sync modes, video-mode-derived clock rates |
| CDROM | Working | Async command/response model, disc region detection, XA audio decode |
| SIO / controllers | Working | Digital pad, both memory card slots. No analog pad or multitap yet |
| GTE | Working | All 22 opcodes with saturation/flags, per-op cycle costs charged to the CPU |
| GPU / renderer | Working | Unified VRAM texture (raster + upload + scanout), 15bpp and 24bpp display |
| MDEC | Working | Full decode pipeline, exercised by real FMV playback |
| PCDrv | Working | Host filesystem side-channel for homebrew |
| Debugger / UI | Working | Disassembler, breakpoints, watchpoints, exec trace, Lua console |
| SPU / audio | Working | Emulated-clock sample generation; output quality not yet surveyed |
| Savestates | **Absent** | Not started |

---

## Debug UI

The SDL2 window is an ImGui workspace; all output goes to it, nothing to the terminal. It is organised
around a **machine bar**, a **mode rail**, a **stage** and a docked **log console** rather than a grid
of floating panels — the direction and its rationale live in `docs/ui/`.

- **Machine bar** — BIOS, disc, live PC, and the vitals that used to need a Lua script: real-time %,
  frame ms, audio-queue depth and SPU drift, read once per frame from counters that exist anyway. A
  **Controls** popup folds in pause/step, the log level and the log windows.
- **Mode rail (F1–F8)** — one mode replaces a cluster of windows:
  - **Pipeline** — CD → XA → MDEC → DMA → VRAM/scanout on one row with live rates, plus a contextual
    inspector (VRAM CPU-vs-GPU status, audio meters, pinned watches)
  - **Display** — the emulated screen given the whole stage
  - **Frame** — per-frame event timeline (chrome in place; the cycle-timestamped event ring is pending)
  - **Code** — disassembly (PC/breakpoint highlight, clickable dots, go-to, live exec trace) with
    registers and breakpoints in the inspector
  - **Memory** — hex view over RAM / scratchpad / BIOS with a goto and region jumps
  - **Audio** — the SPU voice/ADSR/reverb panel with an audio-meters inspector
  - **VRAM** — the full 1024×512 VRAM with selectable decode (4/8/16/24 bpp), 24bpp byte-phase shift,
    CLUT picking, mask-bit and greyscale views, pixel/texture-page grids, the active display area
    outlined, zoom, pan, magnifier and an exact per-pixel readout
  - **Script** — the Lua console: breakpoints/watchpoints with callbacks, register/memory reads, VRAM
    inspection, GPU/MDEC/DMA probe events. Scripts live in `scripts/`
- **The screen is the anchor** — the emulated output stays on the stage in every mode; Display expands
  it to the whole stage.
- **Log console** — per-category log windows tabbed along the bottom, each with its own level selector,
  plus the BIOS/game TTY (`printf`-style with o32 varargs argument substitution).
- **Identity** — a blued-graphite `ImGuiStyle`; the two accents *encode the data path* (cyan = the
  video chain CD→MDEC→DMA→VRAM, rose = the audio chain XA→SPU→device), severity colours kept separate.
- **Shortcuts** — F1–F8 pick the mode, Space run/pause, F11 single step, Alt+Enter fullscreen. The
  window opens maximised with its titlebar buttons.

---

## Architecture

```
src/main.c                — host shell: SDL, GL context, audio device, threads, frame cap
src/debug_ui.cpp          — ImGui debug interface (the only C++ in the project)

src/core/
  system.c                — "run one frame": the machine's timing loop
  interconnect.c          — machine wiring, TTY buffer
  bus.c                   — memory routing + 256-entry hardware register dispatch
  bus_irq.c               — edge-triggered interrupt controller
  event_scheduler.c       — the single scheduling authority
  dma.c                   — 7 DMA channels, linked-list and block transfers
  timers.c                — timers 0/1/2, derived counters, sync modes
  sio.c                   — controller and memory card protocol
  controller.c            — keyboard to pad mapping
  mdec.c                  — macroblock decoder
  debugger.c              — breakpoints, watchpoints, execution trace
  lua_debug.c             — Lua scripting surface for live debugging
  pcdrv.c                 — host filesystem side-channel
  ram.c / bios.c          — main RAM, BIOS ROM

src/cpu/
  cpu_execution.c         — instruction loop, downcount, interrupt checks
  cpu_instructions.c      — R3000A instruction implementations
  cpu_decode.c            — dispatch tables
  cpu_exceptions.c        — exception entry, SR mode stack, EPC/Cause
  cpu_icache.c            — instruction cache
  cpu_bios.c              — BIOS syscall observation (TTY capture, call tracing)
  cpu_disasm.c            — disassembler
  cpu_init.c / cpu_registers.c

src/gpu/
  gpu.c                   — GP1, GPUSTAT, CRTC, display mapping
  gpu_commands.c          — GP0 dispatch table and every draw command
  gpu_helpers.c           — command helpers
  renderer.c              — OpenGL renderer, unified VRAM texture, GPU thread
  vram.c                  — CPU-side VRAM model

src/gte/                  — geometry transformation engine (registers + all 22 operations)
src/cdrom/                — controller, commands, disc images (CUE/BIN), XA/CDDA audio
src/spu/                  — voices, ADSR, reverb/mixing, DMA, IRQ
src/utils/                — logging
```

### Memory map (physical addresses)

| Region | Range |
|--------|-------|
| RAM | 0x00000000 – 0x001FFFFF |
| Scratchpad | 0x1F800000 – 0x1F8003FF |
| SIO / JOY | 0x1F801040 – 0x1F80104F |
| IRQ registers | 0x1F801070 – 0x1F801077 |
| DMA | 0x1F801080 – 0x1F8010FF |
| Timers | 0x1F801100 – 0x1F80112F |
| CDROM | 0x1F801800 – 0x1F801803 |
| GPU | 0x1F801810 – 0x1F801817 |
| SPU | 0x1F801C00 – 0x1F801E7F |
| EXP2 (BIOS TTY) | 0x1F802000 – 0x1F803FFF |
| BIOS ROM | 0x1FC00000 – 0x1FC7FFFF |

### Design notes

- **Pure C99**, no allocation in hot paths, structs embedded rather than heap-allocated. The only C++
  is the ImGui wrapper.
- **One timing authority**: `system.c` runs the machine until the VBlank event closes the frame.
  Everything with a deadline — timers, CDROM, DMA slices, VBlank — is a scheduled event.
- **Counters are derived, not stepped**: a timer's value is computed from the global cycle counter on
  read, so a game polling it always sees continuous motion and there is no per-tick loop.
- **One VRAM**: a single GPU texture is the rasterization target, the upload destination and the
  scanout source, with the PSX mask bit carried in its alpha channel.
- **Threads**: emulation on the main thread, GL on a render thread, audio on its own thread. Frames
  are handed over as double-buffered command lists.
- **LLE throughout**: BIOS syscalls run for real; the A0/B0/C0 hooks only watch.

---

## Logging

17 categories: `SYSTEM CPU IRQ DMA GPU CDROM TIMER BIOS INTERCONNECT RENDERER EVENT GTE VRAM RAM
DEBUG MDEC SPU` — 6 levels: `SILENT ERROR WARN INFO DEBUG TRACE`.

Macro pattern: `LOG_<CATEGORY>_<LEVEL>(fmt, ...)`, e.g. `LOG_GPU_DEBUG("gp0=0x%08x", v)`. The default
level is INFO because DEBUG and TRACE log per DMA transfer, per GP0 command and per macroblock.

---

## Documentation in this repo

- `GAP_ANALYSIS_REFACTOR_2026-07-13.md` — per-subsystem state, open gaps, and the work queue
- `GPU_GAP_ANALYSIS_2026-07-15.md` — GPU deep dive: renderer architecture, invariants, gaps
- `CHANGELOG.md` — what changed and why, including the root cause of each non-obvious fix
- `DOCS/` — the hardware documentation this implementation is written against

---

## References

Hardware behaviour is implemented from the documentation in `DOCS/` (PSX-SPX and related notes) and
from Lionel Flandrin's *PlayStation Emulation Guide*. Where hardware documentation was ambiguous, the
DuckStation and PCSX-Redux sources were consulted as a second opinion on intended behaviour.

The MDEC's transform stage (IDCT, RLE decode, YUV→RGB) is ported from DuckStation, with attribution in
the file header. Everything else is this project's own implementation.

---

## License

Educational purposes only. PlayStation is a trademark of Sony Interactive Entertainment.
