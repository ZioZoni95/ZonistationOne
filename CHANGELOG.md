# Changelog

All notable changes to ZonistationOne are documented here.
Format follows [Keep a Changelog](https://keepachangelog.com/en/1.0.0/).

---

## [Unreleased]

### Added
- **IDE-style Debug UI**: Completely refactored the interface to mirror PCSX-Redux. The main SDL2 window now acts purely as an ImGui DockSpace.
- **FBO Display Rendering**: The PS1 display is no longer drawn directly to the default framebuffer. It is rendered to an off-screen OpenGL Framebuffer Object (FBO) and displayed within a dedicated ImGui window, allowing it to be docked or dragged out.
- **Modular Component Logs**: Replaced the monolithic console with individual, dockable ImGui log windows for every hardware component (CPU, GPU, CDROM, BIOS/Kernel, DMA, etc.).
- **ImGui Log Level Selector**: Global log verbosity (`TRACE`, `DEBUG`, `INFO`, `WARN`, `ERROR`, `SILENT`) can now be toggled in real-time from the ImGui "Options" menu.
- **Kernel/TTY Logging**: Captured BIOS `printf`/`putchar` syscalls are now accurately routed to the ImGui "BIOS Log" window instead of raw `stderr`.
- **CPU cycle model (DuckStation-style)**: `downcount` + `muldiv_completion_tick` fields in `Cpu`
  struct. Execution loop decrements `downcount` per instruction; event dispatcher fires when
  `downcount <= 0` and resets it to `next_event - cpu_cycle_counter`.
- **MulDiv stall emulation**: MFHI/MFLO stall until `muldiv_completion_tick` (MULT/MULTU = 7 cy,
  DIV/DIVU = 37 cy), matching PSX hardware latencies from DuckStation `cpu_core.cpp`.
- **Bus modular split**: monolithic `src/interconnect.c` (~2400 lines) split into:
  - `src/bus.c` — memory routing (load/store 32/16/8, DMA logic, BIOS helpers)
  - `src/bus_irq.c` — IRQ controller (edge-triggered I_STAT, request/clear/trigger)
  - `src/interconnect.c` (slim, ~156 lines) — init, CDROM event scheduling, TTY buffer
- **CDROM audio + disc modules**: `src/cdrom_audio.c`, `src/cdrom_commands.c`, `src/cdrom_disc.c`
  with matching headers.

### Changed
- Standard command-line logging (stdout/stderr) has been completely disabled in favor of the ImGui interface. CLI flags like `--quiet` and `--debug` have been removed.

### Fixed
- Timer clock source routing: all three timer clock_source bit pairs now handled correctly
  (was: Timer2 `clock_source=2` fell through to "skip", freezing BIOS post-menu animation).
- GPU draw offset: `gp0_drawing_offset` (GP0 0xE5) now calls `renderer_set_draw_offset` so
  the double-buffer offsets (0,1)/(0,241) are applied to the shader uniform.
- VRAM blit color order: R and B channels were swapped in `renderer_blit_vram` (PSX RGB555:
  R=bits 0-4, B=bits 10-14).
- DMA completion IRQ: `interconnect_perform_dma` raises IRQ3 on 0→1 transition of I_STAT[3]
  only; removed PCSX ReARMed re-raise hack that caused infinite loop when BIOS ACKed wrong
  channel in DICR.
- SIO range ordering in `store16/store8`: SIO (0x1F801040–0x1F80104F) checked before
  MEM_CONTROL_END (0x1F80107F), preventing silent discard of JOY writes.
- `interconnect_load16/load8`: SIO read handler moved before generic hwregs catch-all so
  JOY_STAT TX_RDY bits are visible to BIOS polling loop.
- IRQ7 (IRQ_CTRLMEMCARD): SIO pulses IRQ line after each transfer when CTRL bit 12 set.
- Drawing area initial bounds: `(0,0,0,0)` → `(0,0,1023,511)` to prevent full scissor clip
  on startup; `renderer_set_drawing_area` called from `gpu_reset_state`.

---

## [stable] — 2026-03-17

### Working
- BIOS boot sequence (SCPH-1001 US): logo animation, menu rendering, cursor, navigation
- GPU: GP0/GP1 command dispatch (polygons, rects, lines, VRAM transfers), VRAM double-buffer,
  display blit, draw offset, scissor rectangle
- DMA: linked-list and block/request modes for GPU (ch2) and OTC (ch6)
- Timers 0/1/2: counter, mode, target registers; VBlank event scheduler
- CDROM: command handling, disc read, IRQ delivery
- SIO/Controller: digital pad protocol, keyboard-to-gamepad mapping (WASD/SPACE/E/C/Z/X)
- GTE: geometry transformation engine, load delay slots
- SPU: register stubs (audio not rendered)
- I-Cache: 256-line 4-word cache with tag/valid bits
- Event scheduler: DuckStation-style downcount dispatch for VBlank, timers, CDROM

---

## [init] — 2025

### Added
- Initial MIPS R3000A CPU interpreter (all standard instructions + COP0 + GTE)
- Memory bus: RAM (2MB), BIOS ROM (512KB), Scratchpad (1KB), hardware register routing
- Basic GPU: monochrome/textured polygon rendering via OpenGL 3.3 + GLEW
- BIOS TTY capture via EXP2+0x23 DUART and A0/B0 syscall side-channel
