# ZonistationOne — Claude Code Instructions

## Project

PS1 emulator written in C. SDL2 + OpenGL 3.3 (GLEW). Early development.

```bash
make              # build
make clean && make  # clean build
./myps1_emu roms/SCPH1001.BIN                              # BIOS menu
./myps1_emu roms/SCPH1001.BIN "games/Ace Combat 2 (Europe).cue"  # game
```

BIOS used: SCPH-1001 (US). Branch: `stable_branch`.

---

## Architecture

```
src/main.c              — SDL loop, 33868800/60 cycles/frame
src/cpu/
  cpu_execution.c       — main CPU loop (DuckStation-style downcount)
  cpu_instructions.c    — MIPS R3000A instruction decode/execute
  cpu_init.c            — CPU init, register reset
  cpu_exceptions.c      — EXCEPTION_* handler, EPC/SR/Cause
  cpu_bios.c            — A0/B0 syscall side-channel (LLE TTY capture)
  cpu_icache.c          — 256-line 4-word instruction cache
src/bus.c               — memory routing: load/store 32/16/8, DMA logic, BIOS helpers
src/bus_irq.c           — IRQ edge-triggered controller (I_STAT / I_MASK)
src/interconnect.c      — slim: init, CDROM event scheduling, TTY buffer
src/gpu.c               — GPU init/reset/GP1/GPUSTAT
src/gpu_commands.c      — GP0 256-entry dispatch table, all draw commands
src/renderer.c          — OpenGL 3.3 renderer (VAO, shaders, VRAM blit)
src/cdrom.c             — CDROM controller + command dispatch
src/cdrom_commands.c    — all CDROM command handlers
src/cdrom_disc.c        — disc image read (CUE/BIN)
src/timers.c            — PSX timers 0/1/2
src/sio.c               — SIO / JOY controller protocol
src/controller.c        — keyboard → PSX gamepad mapping
src/event_scheduler.c   — DuckStation-style event dispatch (VBlank, timers, CDROM)
src/gte.c               — Geometry Transformation Engine
src/log.c               — logging: 16 categories, 6 levels, per-category filter
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
| GPU | 0x1F801810–0x1F801817 |
| CDROM | 0x1F801800–0x1F801803 |
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

16 categories: SYSTEM CPU IRQ DMA GPU CDROM TIMER BIOS INTERCONNECT RENDERER EVENT GTE VRAM RAM DEBUG MDEC  
6 levels: SILENT ERROR WARN INFO DEBUG TRACE  
Default runtime level: **INFO** (`src/log.c:8`). All ImGui component windows open at startup.

Macro pattern: `LOG_<CATEGORY>_<LEVEL>(fmt, ...)` e.g. `LOG_GPU_DEBUG("gp0=0x%08x", v)`

**DuckStation log philosophy (1:1 alignment — completed)**:
- ERROR: hardware faults, invalid state
- WARN: recoverable anomalies, dropped commands
- INFO: init, major state changes only
- DEBUG: register writes, IRQ events, command dispatch (e.g. `"VBLANK IRQ triggered"`, `"Interrupt mask <- 0x%08x"`, `"Execute command SeekL (0x15)"`)
- TRACE: per-instruction, per-transfer, per-primitive (hot path — never in default builds)
- No counter-based rate-limiting — use level gating

Key format conventions:
- IRQ: `"{name} IRQ triggered/cleared"`, `"Interrupt mask <- 0x%08x"`
- GP1: `"Display address start <- 0x%08x"`, `"Set display mode <- 0x%08x"`
- CDROM: `"Execute command {name} (0x%02X)"`, `"Drive state: OLD -> NEW (LBA N)"`
- GTE unhandled: fires once per unique opcode via bitmask seen-set (`seen_opcodes`)

---

## Conventions

- Pure C (C11). No C++ except future `src/debug_ui.cpp` (ImGui wrapper).
- No `malloc` in hot paths. Structs embedded, not heap-allocated.
- SDL2 + GLEW + OpenGL 3.3 Core. No other runtime dependencies currently.
- `inter->cpu` pointer set after CPU init via `interconnect_set_cpu()`.
- IRQ lines are edge-triggered: `interconnect_set_irq_line(inter, IRQ_X, true/false)`.
- Exception flow: `cpu_exception(cpu, EXCEPTION_*)` saves EPC, updates SR mode stack.
- LLE style: BIOS syscalls run normally; A0/B0 hooks are side-channel only (no fake returns).

---

## Active Plans

- **UI FBO Refactor**: ✅ Complete. Main SDL window acts as an ImGui host/DockSpace. The PS1 display is rendered to an FBO and shown in an ImGui window. All components have their own log windows. CLI logging removed.
- **Comprehensive Logging Overhaul**: ✅ Complete. All components (CPU, GPU, CDROM, Timers, GTE, SIO, DMA, bus_irq) refactored to 1:1 DuckStation log philosophy. Default level INFO, all windows open at startup.

---

## Known Working

- BIOS boot sequence (SCPH-1001): logo → menu → cursor → navigation
- GPU: polygons, rects, textured, VRAM double-buffer, draw offset, scissor
- DMA: linked-list + block/request (GPU ch2, OTC ch6)
- Timers 0/1/2: counter + interrupt
- CDROM: command handling, disc read, IRQ delivery
- SIO: digital pad protocol, keyboard input (WASD/SPACE/E/C/Z/X)
- GTE: geometry transforms, load delay slots
- I-Cache: 256-line 4-word with tag/valid bits
