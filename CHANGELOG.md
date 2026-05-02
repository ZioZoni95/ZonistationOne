# Changelog

All notable changes to ZonistationOne are documented here.
Format follows [Keep a Changelog](https://keepachangelog.com/en/1.0.0/).

---

## [Unreleased]

### Added
- **CPU Disassembler**: PCSX-Redux-style disassembly window with 128-row virtual list (`ImGuiListClipper`), row highlights for PC (yellow) and breakpoints (dark red), clickable breakpoint dots, Go-To-Address footer.
- **Run / Pause / Step controls**: F5 (run/pause toggle) and F11 (single step) wired through `debug_ui_step_requested()` edge-triggered flag into the main emulation loop.
- **Breakpoint manager**: ImGui table (address / enable checkbox / delete button); click address to jump disassembly view; Add BP input field.
- **CPU Registers window**: PC / SR / Cause / EPC / HI / LO header, then 32 GPRs in a two-column table with MIPS register names; non-zero registers highlighted in yellow.
- **IDE-style Debug UI**: Complete rewrite of `src/debug_ui.cpp` (~400 lines). Menu bar: `Emulator` | `Debug` | `Logs` | `Options`. `[PAUSED]` indicator in menu bar. Initial docking layout: PS1 Display (main), Disassembly (right-top 62%), CPU Registers + Breakpoints (right-bottom 38%), BIOS + component logs (bottom 22%).
- **FBO Display Rendering**: PS1 display rendered to an off-screen OpenGL FBO; shown inside a dockable / floatable ImGui window.
- **Modular component log windows**: 16 ImGui log windows (one per hardware category), individually dockable. Open All / Close All shortcuts in `Logs` menu.
- **Debugger struct** (`include/debugger.h` / `src/debugger.c`): breakpoints + per-BP `bp_enabled[]` array, read/write watchpoints, `step_skip_bp` flag to avoid re-triggering a breakpoint when stepping off it. Forward-declared `struct Cpu` to break circular include cycle.
- **GPU renderer partial VRAM upload** (`renderer_upload_vram_rect`): `gp0_image_load` (CPU→VRAM) and `gp0_copy_rectangle` (VRAM→VRAM) now upload only the dirty rectangle via `glTexSubImage2D` + `GL_UNPACK_ROW_LENGTH` instead of the full 512 KB VRAM texture.

### Changed
- **Texture window uniform**: Fragment shader `uvec2 tex_window_and` + `uvec2 tex_window_or` consolidated into single `ivec4 u_texWindow` (and_x, and_y, or_x, or_y). One `glUniform4i` call per change instead of two.
- **Log window names**: Stripped redundant "Log" suffix from all 16 category windows.
- Standard command-line logging (stdout/stderr) fully disabled; all output routes to ImGui windows.

### Fixed
- **Textured rectangle VRAM upload ordering** (`src/gpu_commands.c` `draw_rectangle`): `renderer_set_semi_trans_mode` (which triggers a batch flush) was called **before** `renderer_upload_vram`. Flushed geometry was drawn with stale VRAM texture → wrong CLUT → rainbow corruption on font sprites. Fixed by uploading VRAM before the flush-triggering call.
- **`upload_vram_if_dirty` forward declaration**: Function was defined after `draw_rectangle`, causing compiler implicit non-static declaration → static-follows-non-static error. Moved definition above first call site.
- **ImGui crash** (`TableSetBgColor` outside table scope): Disassembly row highlighting used `ImGui::TableSetBgColor` in a plain-text list (no active `BeginTable`). Replaced with `GetWindowDrawList()->AddRectFilled` using cursor screen position.

---

## [ui-refactor] — 2026-03

### Added
- **ImGui docking + multi-viewports**: `ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_ViewportsEnable`; windows can be dragged out of the main SDL2 window.
- **Kernel/TTY Logging**: BIOS `printf`/`putchar` syscalls routed to ImGui "BIOS" log window.
- **ImGui Log Level Selector**: Global verbosity (`TRACE` → `SILENT`) toggleable at runtime from `Options` menu.
- **rxi/log integration** (`src/log.c`): 16 categories × 6 levels, per-category rate-limiting hooks.

### Changed
- Main SDL2 window is now a pure ImGui DockSpace host; all UI rendered via ImGui.

---

## [cpu-cycle-model] — 2026-03

### Added
- **DuckStation-style CPU cycle model**: `Cpu.downcount` decrements per instruction; `eventq_dispatch_due` fires when ≤ 0.
- **MulDiv stall emulation**: MFHI/MFLO stall until `muldiv_completion_tick` (MULT = 7 cy, DIV = 37 cy).
- **Bus modular split**: `src/interconnect.c` split into `src/bus.c` (memory routing), `src/bus_irq.c` (IRQ controller), slim `src/interconnect.c` (init + event glue).

### Fixed
- Timer clock source routing: all three timer clock_source bit pairs now correct (Timer2 `clock_source=2` fell through to skip → BIOS Timer2 polling loop froze post-menu).
- GPU draw offset: `gp0_drawing_offset` (GP0 0xE5) now calls `renderer_set_draw_offset` so double-buffer offsets (0,1)/(0,241) are applied to vertex shader uniform.
- VRAM blit color channel order: R and B were swapped in `renderer_blit_vram` (PSX RGB555: R=bits 0–4, B=bits 10–14).
- DMA completion IRQ: `interconnect_perform_dma` raises IRQ3 on 0→1 I_STAT[3] transition only; removed PCSX ReARMed re-raise hack that caused infinite loop on wrong-channel DICR ACK.
- SIO range ordering in `store16/store8`: SIO (0x1F801040–0x1F80104F) now checked before MEM_CONTROL_END (0x1F80107F).
- `interconnect_load16/load8`: SIO read handler moved before generic hwregs catch-all so JOY_STAT TX_RDY bits are visible to BIOS.
- Drawing area initial bounds: `(0,0,0,0)` → `(0,0,1023,511)`; `renderer_set_drawing_area` called from `gpu_reset_state`.

---

## [stable] — 2026-03-17

### Working
- BIOS boot sequence (SCPH-1001 US): logo animation, menu rendering, cursor, navigation
- GPU: GP0/GP1 command dispatch (polygons, rects, lines, VRAM transfers), double-buffer, blit, draw offset, scissor
- DMA: linked-list and block/request for GPU (ch2) and OTC (ch6)
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
- Memory bus: RAM (2 MB), BIOS ROM (512 KB), Scratchpad (1 KB), hardware register routing
- Basic GPU: monochrome/textured polygon rendering via OpenGL 3.3 + GLEW
- BIOS TTY capture via EXP2+0x23 DUART and A0/B0 syscall side-channel
