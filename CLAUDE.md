# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

PS1 emulator written in C. SDL2 + OpenGL 3.3 (GLEW). Early development.

```bash
make                    # build
make clean && make      # clean build
make test               # run cpu_minimal_test
./myps1_emu roms/SCPH1001.BIN                                           # BIOS menu
./myps1_emu "roms/SCPH-7502 (3).BIN" --game="games/Ace Combat 2 (Europe).cue"
```

The game path must be passed as `--game=<cue>`; a bare positional path is rejected. Run PAL discs
with the PAL BIOS (`SCPH-7502`) — region mismatch is detected and rejected as on hardware.

Useful env vars: `ZS1_LOG_LEVEL=<level>`, `ZS1_LOG_STDERR=1` (log to stderr as well as the ImGui
windows), `ZS1_LUA_SCRIPT=scripts/x.lua`, `ZS1_DUMP_FRAME=<path>` + `ZS1_DUMP_FRAME_N=<n>`,
`ZS1_FRAME_PROFILE=1` (per-frame time split plus cycles per instruction),
`ZS1_AUDIO_DUMP=<path>`, `ZS1_SPU_NO_REVERB=1`.

`ZS1_GPU=nvidia|intel` picks the GPU on this hybrid machine — it sets the PRIME offload variables
before the context is created, and the run logs which driver it got and whether the request was
honoured. Undefined GL behaved differently on the two, so *always* check that line before treating a
rendering difference as an emulator bug.

`ZS1_RAM_LOAD_STALL=<n>` overrides the per-load RAM cost (default 3). `0` restores the old flat
one-cycle-per-instruction timing, which is the honest A/B — `VSync: timeout` then returns.

BIOS: SCPH-1001 (US), SCPH-7502 (PAL). Branch: `stable_branch`. Compiler: `gcc -std=c99`.

---

## Architecture

```
src/main.c                     — host shell: SDL, GL context, audio device, threads, frame cap

src/cpu/
  cpu_execution.c              — main CPU loop (DuckStation-style downcount)
  cpu_instructions.c           — MIPS R3000A instruction execute
  cpu_decode.c                 — instruction decode
  cpu_disasm.c                 — disassembler
  cpu_init.c                   — CPU init, register reset
  cpu_registers.c              — register file helpers
  cpu_exceptions.c             — EXCEPTION_* handler, EPC/SR/Cause
  cpu_bios.c                   — A0/B0 syscall side-channel (LLE TTY capture)
  cpu_icache.c                 — 256-line 4-word instruction cache

src/core/
  interconnect.c               — init, CDROM event scheduling, TTY buffer
  bus.c                        — memory routing: load/store 32/16/8
  bus_irq.c                    — IRQ edge-triggered controller (I_STAT / I_MASK)
  ram.c                        — 2 MB RAM
  bios.c                       — BIOS ROM load
  dma.c                        — DMA channels 0-6, linked-list + block, completion IRQ
  timers.c                     — PSX timers 0/1/2
  sio.c                        — SIO / JOY controller protocol
  controller.c                 — keyboard → PSX gamepad mapping
  mdec.c                       — Motion DECoder (working: RLE/IDCT/YUV→RGB, FMV verified)
  pcdrv.c                      — PC drive side-channel
  event_scheduler.c            — the single scheduling authority (VBlank, timers, CDROM, DMA slices)
  system.c                     — "run one frame": the machine's timing loop
  debugger.c                   — breakpoints, watchpoints, execution trace
  lua_debug.c                  — Lua scripting surface (emu.*) for live debugging

src/gpu/
  gpu.c                        — GPU init/reset/GP1/GPUSTAT
  gpu_commands.c               — GP0 256-entry dispatch table, all draw commands
  gpu_helpers.c                — GP0 helper utilities
  renderer.c                   — OpenGL 3.3 renderer, unified VRAM texture, GPU thread
  vram.c                       — 1024×512 VRAM buffer management

src/gte/
  gte.c                        — GTE register I/O, command dispatch
  gte_ops.c                    — all GTE operations

src/cdrom/
  cdrom.c                      — CDROM controller + command dispatch
  cdrom_commands.c             — all CDROM command handlers
  cdrom_disc.c                 — disc image read (CUE/BIN)
  cdrom_audio.c                — CDROM audio (XA/CDDA)

src/spu/
  spu.c                        — SPU init/register I/O
  spu_voice.c                  — 24-voice management
  spu_adsr.c                   — ADSR envelope
  spu_mixing.c                 — stereo mix output
  spu_dma.c                    — SPU DMA transfers
  spu_irq.c                    — SPU IRQ logic

src/utils/
  log.c                        — 16-category / 6-level logger
  rxi_log.c                    — rxi log backend

src/debug_ui.cpp               — ImGui debug UI (C++)
tests/cpu_minimal_test.c       — minimal CPU integration test
```

---

## Memory Map (physical addresses)

| Region | Range |
|--------|-------|
| RAM | 0x00000000–0x001FFFFF |
| Scratchpad | 0x1F800000–0x1F8003FF |
| SIO / JOY | 0x1F801040–0x1F80104F |
| IRQ regs | 0x1F801070–0x1F801077 |
| DMA | 0x1F801080–0x1F8010FF |
| Timers | 0x1F801100–0x1F80112F |
| CDROM | 0x1F801800–0x1F801803 |
| GPU | 0x1F801810–0x1F801817 |
| SPU | 0x1F801C00–0x1F801E7F |
| EXP2 (BIOS TTY) | 0x1F802000–0x1F803FFF |
| BIOS ROM | 0x1FC00000–0x1FC7FFFF |

---

## CPU Cycle Model (DuckStation-style)

`Cpu.downcount` decrements per instruction. When ≤ 0, `eventq_dispatch_due()` fires.
`Cpu.muldiv_completion_tick` set by MULT/DIV (7/37 cy); MFHI/MFLO stall until reached.
`inter->cpu_cycle_counter` is the global monotonic tick counter (in `Interconnect`).

---

## Logging

17 categories: `SYSTEM CPU IRQ DMA GPU CDROM TIMER BIOS INTERCONNECT RENDERER EVENT GTE VRAM RAM DEBUG MDEC SPU`  
6 levels: `SILENT ERROR WARN INFO DEBUG TRACE`  
Default runtime level: **INFO** (`src/utils/log.c`). All ImGui log windows open at startup.

Macro pattern: `LOG_<CATEGORY>_<LEVEL>(fmt, ...)` e.g. `LOG_GPU_DEBUG("gp0=0x%08x", v)`

**Log level policy**:
- ERROR: hardware faults, invalid state
- WARN: recoverable anomalies, dropped commands
- INFO: init, major state changes only
- DEBUG: register writes, IRQ events, command dispatch
- TRACE: per-instruction, per-transfer, per-primitive (hot path — never in default builds)
- No counter-based rate-limiting — use level gating

Key format conventions:
- IRQ: `"{name} IRQ triggered/cleared"`, `"Interrupt mask <- 0x%08x"`
- GP1: `"Display address start <- 0x%08x"`, `"Set display mode <- 0x%08x"`
- CDROM: `"Execute command {name} (0x%02X)"`, `"Drive state: OLD -> NEW (LBA N)"`
- GTE unhandled: fires once per unique opcode via bitmask seen-set (`seen_opcodes`)

---

## Licensing constraint

The project is **GPL-3.0-or-later**; every source file carries an SPDX header and
`THIRD-PARTY.md` is the inventory.

- **Never copy code from `duckstation_ref/`.** DuckStation has been CC-BY-NC-ND-4.0 since
  2024-09-01: no derivative works, no commercial use, incompatible with the GPL. The submodule is
  there to answer "what does hardware do here" when `DOCS/` is ambiguous, and for nothing else.
  Describing its behaviour in a comment is fine; reproducing its structure or its code is not.
- `pcsx-redux/` is GPL-2.0-or-later, so code from it *can* be used — with the attribution header kept
  intact, which is the licence condition being satisfied. Do not strip those headers.
- Prefer `DOCS/` over both. Anything implemented from a cited `DOCS/` line carries no third-party
  copyright, and the citation is what makes that checkable later.

## Conventions

- Pure C (C99). C++ only in `src/debug_ui.cpp` (ImGui wrapper).
- No `malloc` in hot paths. Structs embedded, not heap-allocated.
- SDL2 + GLEW + OpenGL 3.3 Core. ImGui via `third_party/imgui/`.
- `inter->cpu` pointer set after CPU init via `interconnect_set_cpu()`.
- IRQ lines are edge-triggered: `interconnect_set_irq_line(inter, IRQ_X, true/false)`.
- Exception flow: `cpu_exception(cpu, EXCEPTION_*)` saves EPC, updates SR mode stack.
- LLE style: BIOS syscalls run normally; A0/B0 hooks are side-channel only (no fake returns).

---

## Known Working

- BIOS boot to menu (US and PAL), full 3D boot logo
- `Ace Combat 2 (Europe)`: boots, plays its FMV intro, reaches the textured menu and 3D engine
- GPU: polygons, rects, lines, textured/CLUT, semi-transparency, scissor, unified VRAM texture,
  15bpp and 24bpp display
- MDEC: full decode pipeline, verified against real FMV playback
- DMA: all channels, linked-list + block, completion interrupts
- Timers 0/1/2: derived counters, sync modes, video-mode-derived rates
- CDROM: async command/response, disc region detection, XA audio decode
- SIO: digital pad, DualShock analog protocol (ID 73h/F3h, adc0-3, config commands), rumble on both
  the old one-motor and the new 4Dh-mapped method, both memory card slots
- Controllers: DS4 over USB/Bluetooth via SDL_GameController, hot-plug, keyboard live alongside it
- GTE: all 22 ops with cycle costs charged to the CPU
- I-Cache: 256-line 4-word with tag/valid bits
- SPU: sample generation on the emulated clock (EVQ_SPU event + catch-up on register access)
- **Savestates**: full machine, format **v3**. F5 saves, F8 loads, `emu.save_state`/`emu.load_state`
  from Lua. v2 states are refused — the SIOI section (SIO0 protocol state) arrived in v3.
- CPU memory timing: RAM data **loads** cost 3 cycles (1 documented from RAM_SIZE bit 7, 2
  calibrated); stores are free because the write buffer absorbs them. CPI lands ~1.6, tracked and
  printed by `ZS1_FRAME_PROFILE=1`. This is what stopped the BIOS printing `VSync: timeout` on
  every call.

## Known Broken / Absent

- **SPU pops during speech** — the open defect. Sounds like clipping, but the final mix peaks far
  below full scale (5869/6343 of 32767 observed), so any saturation is at an intermediate stage.
  `scripts/spu_clip_probe.lua` reports the reverb network's in/out peaks and rail hits alongside the
  XA and ring drop counters, which separates saturation from a dropped sample — the two sound alike.
  `ZS1_SPU_NO_REVERB=1` is the one-run A/B. Not yet reproduced from a fixed point: no v3 savestate
  inside a speech scene exists.
- **iGPU rendering artifacts — fix committed, not verified.** Textures came out as flat blocks of
  saturated colour on Mesa/Intel and clean on the NVIDIA dGPU. Three pieces of undefined GL were
  corrected (unsized `GL_RGB` scanout target, float-to-uint on a possibly-negative interpolated UV,
  `GL_DITHER` left enabled). The UV fix alone was tested and did not resolve it; the format fix is
  the one that had not been tried when that was reported. Needs an iGPU run to confirm.
- GPU: mask-bit *test* not applied to rasterized primitives; GP0(C0)/GP0(80) read the CPU-side VRAM;
  texture sampling reads a separate mirror; CRTC ticks once per frame.
- No multitap. No Dualshock2 pressure sensing; digital-mode transfer length does not grow when
  motors are mapped to config bytes cc..ff.

See `GAP_ANALYSIS_REFACTOR_2026-07-13.md` (per-subsystem state + work queue) and
`GPU_GAP_ANALYSIS_2026-07-15.md` (renderer deep dive) — both rewritten 2026-07-28 and authoritative
over this file for status.

---

## Picking this up on another machine

Development moved off WSL because the debug interface could not keep real time there — the emulation
core could, with the panels closed. On a native Linux box, re-measure before assuming anything about
speed.

**Build dependencies** (Ubuntu/Debian):

```sh
sudo apt install build-essential libsdl2-dev libglew-dev libgl1-mesa-dev
make clean && make
```

Everything else (ImGui, Lua) is vendored in `third_party/`. The reference emulator clones live in
`duckstation_ref/` and `pcsx-redux/` as submodules — they are consulted for behaviour, never linked.

**State as of 2026-08-04** (branch `debug`, pushed to `origin/debug`):

- Boots the BIOS and `Ace Combat 2 (Europe)`; the FMV intro decodes and displays correctly.
- The machine is an i9-14900HX with an Intel iGPU **and** an RTX 4060. Which one gets the GL context
  changes rendering behaviour, so always check the startup log before judging a visual defect.
- Host cost is ~3.7ms against a 20ms PAL field with the panels closed. There is roughly 5x headroom;
  when something *feels* slow, it is the emulated machine's cycle budget, not the host. Check the CPI
  in `ZS1_FRAME_PROFILE=1` before looking anywhere else.
- Read `docs/study/README.md` first: the combined CDROM/SPU work queue, ordered by impact per unit of
  effort, plus what is already verified correct so it is not re-investigated. Item 9 (VSync) is done.
- `GAP_ANALYSIS_REFACTOR_2026-07-13.md` and `GPU_GAP_ANALYSIS_2026-07-15.md` hold per-subsystem state.
  `docs/ui/` holds the interface direction the debug UI is being rebuilt against.
- The build emits **zero warnings from this project's own sources**. Keep it that way; the only one
  left is `tmpnam` from vendored Lua at link time.

**Debugging convention**: use the in-tree Lua probes (`ZS1_LUA_SCRIPT=scripts/x.lua`, the `emu.*` API
in `src/core/lua_debug.c`) rather than dumping state and analysing it outside. The Lua surface reads
live internals a dump cannot show — `emu.reverb()` gives the reverb network's in/out, `emu.cd_audio()`
the XA FIFO balance, `emu.audio_stats()` ring drops and underruns. Probes live in `scripts/` and are
re-runnable next session.

**DS4 support is implemented and confirmed working on hardware** (buttons, sticks, hot-plug, keyboard
still live alongside it). Two things remain on it: **rumble is written but never verified with a real
pad**, and there is **no UI for controller state or button mapping** — the mapping is hard-coded in
`controller.c`. `docs/CONTROLLER_DS4_SUPPORT.md` and `docs/CONTROLLER_MAPPING_UI.md` are the design
notes (untracked; commit them if they should travel).

**Next up, in order**:
1. **SPU pops during speech** — needs a v3 savestate taken inside a speech scene so runs start from
   the defect instead of booting to it. Then `spu_clip_probe.lua` with and without
   `ZS1_SPU_NO_REVERB=1`.
2. **Confirm the iGPU artifact fix** — one run with `ZS1_GPU=intel` against one with `ZS1_GPU=nvidia`.
3. **Controller UI** — a panel showing live pad state (mode 41h/73h/F3h, buttons, sticks, motors,
   watchdog) and editable mapping, plus verifying rumble against the real DS4.
4. The three GPU items that are really one job (cross-thread GL readback).

**Repository question, still open**: `guide.tex` (Lionel Flandrin's guide, no upstream LICENSE) and
the DuckStation-derived code that was rewritten are out of HEAD but remain in history. Recreating the
repository from a clean tree was discussed and not decided. A `git bundle` preserves all 165+ commits
in one file if the old repo is ever deleted — losing the history is separable from stopping
distribution.

**Traps that have each cost a session**:

- The Makefile has no header dependency tracking. After editing anything in `include/`, run
  `make clean && make` — an incremental build after a struct change produces a binary with mixed
  layouts, which segfaults or misbehaves silently.
- Never quote a speed figure measured with `ZS1_LOG_STDERR`, per-vblank Lua probes, or breakpoints
  active. The instrumentation costs more than what it measures; this produced a bogus "85-95% of real
  time" that was later withdrawn.
- The BIOS must match the disc's region. A PAL disc with the US BIOS is rejected exactly as on
  hardware, and the symptom — sitting at the BIOS menu — looks like a boot regression.
- `--game=` needs the `.bin`, not the `.cue`, for these discs. A `.cue` is accepted and then reports
  "Disc load failed — BIOS-only mode", which looks like a disc bug rather than a path mistake.
- Never quote a frame figure from a single run. A "17.5ms per frame" measurement taken while a build
  was running was reproduced at ~7ms minutes later on the identical binary, and nearly led to blaming
  a 2.7x regression on gcc-14. Repeat, and take a median.
- Do not charge the CPU memory cost to BIOS ROM or the I/O window without running the LBA-23
  isolation test that `cpu_icache.c` describes. Charging ROM *data* loads with the MEMCTRL word time
  killed controller input outright, because the BIOS pad routines read their tables out of ROM.
- Anything on the load/store path is the hottest code in the emulator. A chain of region tests added
  to `interconnect_load32` cost ~10% of host frame time by itself; it is now one comparison.
- `include/timers.h` is CRLF, like `include/renderer.h`. Edit both by line, never by rewriting the
  whole file, or a two-line change becomes a whole-file diff.
- `include/renderer.h` is CRLF. Editing it with a script that rewrites the whole file converts it to
  LF and produces a 750-line diff.
