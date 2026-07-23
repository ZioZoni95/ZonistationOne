# Changelog

All notable changes to ZonistationOne are documented here.
Format follows [Keep a Changelog](https://keepachangelog.com/en/1.0.0/).

---

## [Unreleased]

### Added
- **MDEC (Macroblock Decoder)**: Full implementation in `src/core/mdec.c` / `include/mdec.h`.
  - State machine ported 1:1 from DuckStation (`IDCT_Old`, `DecodeRLE_Old`, `YUVToRGB_Old`).
  - 6-block color path (Cr,Cb,Y1-Y4) and mono path; 4-bit, 8-bit, 24-bit, 15-bit output modes.
  - DMA in/out FIFO (2048 HW in, 768 W out); integrated with DMA ch0 (MDECin) and ch1 (MDECout).
  - Status register: data-out-empty, data-in-full, command-busy, DMA-request bits.
- **Memory Card**: SIO memory card protocol state machine (`src/core/sio.c`).
  - Device select (0x81), Read/Write/GetID commands, sector addressing, checksum verification.
  - `sio_load_memcard()` loads `.mcd` image files; auto-loads `memcard1.mcd` on startup.
  - FLAG byte (0x08 = directory unread on powerup) per PSX-SPX spec.

### Fixed
- **GTE RGB-FIFO CODE byte (PS boot-logo blocker)**: `push_rgb_from_mac` wrote `RGB2 = r | g<<8 | b<<16`, dropping the CODE byte (bits 24-31). Real hardware copies `RGBC.code` (data reg 6, byte 3) into the pushed colour's high byte — now `RGB2 = r | g<<8 | b<<16 | code<<24` (matches DuckStation `PushRGBFromMAC`). The CODE byte is the GP0 command opcode: the BIOS shell's software ordering-table renderer stores the GTE-lit colour word straight into a primitive packet and reads bits 24-31 back as the GP0 command. With CODE=0 every Gouraud-lit logo primitive decoded as opcode `0x00` and was discarded by the packet classifier, so the spinning PlayStation boot logo never drew (vertices present, opcode/colour word zero). **The 3D PS logo now renders correctly.** Multi-session Phase 2.7 blocker resolved.
- **DMA sub-word register access**: byte/halfword writes to the DMA register block (`0x1F801080-0x1F8010FF`) were logged `Non-word write` and dropped, and sub-word reads switched on the unaligned offset (fell into the register-handler defaults). The BIOS shell enables the DMA-completion IRQs with a byte write to `DICR+2` (`0x1F8010F6`), so that write was silently lost. Fixed by word-aligning the offset before dispatch and merging the written lane into the current register value (PCSX-Redux byte-array semantics; DuckStation `FIXUP_WORD_OFFSET`), excluding DICR's write-1-to-clear IRQ-flag lane (bits 24-31) from the merge.
- **GPU IRQ line (GP0(0x1F))**: `IRQ_GPU` (I_STAT bit 1) had no raise site — GP0(0x1F) Interrupt Request only set the GPUSTAT.24 status flag and never asserted the interrupt line, and GP1(02) Acknowledge never lowered it. Now `interconnect_set_irq_line(IRQ_GPU, true/false)` in both handlers, matching DuckStation `HandleInterruptRequestCommand` / GP1(02) `SetLineState(IRQ::GPU, ...)`.
- **DMA-1 (GPU LL terminator)**: `header & 0x00FFFFFC` masked bit0/1, making `next_addr == 0xFFFFFF` impossible. Fixed: extract `raw_next = header & 0x00FFFFFF` first, then check `raw_next & 0x800000` (bit23 = end-of-list per PSX-SPX, all revisions).
- **DMA-2 (DPCR gate)**: DMA channels now blocked when `DPCR` bit `(ch*4+3) = 0`. Previously transfers always started unconditionally on CHCR enable write.
- **DMA-3 (BCR zero)**: `block_size=0` and `block_count=0` now correctly mean `0x10000` per PSX-SPX. Previously caused silent zero-word transfers.
- **DMA-4 (DPCR scan)**: DPCR write now scans all 7 channels and starts any that are active+enabled. Fixes cases where DPCR is written after CHCR (correct software order on hardware).
- **DMA-5 (CHCR bits)**: Chopping enable (bit8), DMA window size (bits 16-18), CPU window size (bits 20-22) now read back from CHCR correctly.
- **DMA-6 (CPU stall)**: DMA transfer duration subtracted from CPU downcount. Device rates: GPU/OTC/MDEC=1 clk/word, SPU=4, CDROM=40. RAM hyper-page: 17 clk/16 words.
- **DMA GPU wait (bit26→bit28)**: DMA ch2 was waiting for GPUSTAT bit26 (cmd-ready), which is 0 during IMAGE_LOAD mode → 10K-spin timeout per word. Fixed to wait for bit28 (DMA-ready), which stays set in all GP0 modes as long as FIFO < 16.
- **GPUSTAT bits 25/26/27/28**: Corrected per PSX-SPX. Bit26 only set in COMMAND mode; bit27 only during active VRAM→CPU transfer; bit28 always set when FIFO<16; bit25 mirrors bit28 or bit27 based on DMA direction setting.
- **GP0(0x02) Fill Rectangle**: Rewritten per PSX-SPX: absolute VRAM coords (no draw offset), no drawing-area clip, no mask bit. Position masked `x & 0x3F0`, `y & 0x1FF`; width rounded up to 16-pixel boundary.
- **GPU Dithering**: 4×4 PSX dither matrix in fragment shader; enabled for Gouraud-shaded/textured-modulated primitives and lines; disabled for mono flat-shaded polygons and all rectangle types. `renderer_set_dither_mode()` flushes before mode change.
- **SPU ADSR return value**: All ADSR phases were returning `EnvelopeVol >> 5` (0–1023). Fixed to return `EnvelopeVol` (0–32767) matching DuckStation 15-bit precision.
- **SPU decay mode**: Decay phase was wrongly using `release_mode_exp` flag for exponential check. Fixed to always use exponential (PSX hardware behaviour).
- **SPU voice mixing**: Volume scaling was `/ 0x4000L` (≈×2 overscale). Fixed to `>> 15` matching the 15-bit range of `vol_left/right`.
- **SPU main volume scaling**: `main_vol_left/right` was used raw (0x3FFF → half volume). Fixed: applied same `<< 1` fixed-mode scaling as voice volumes via `main_vol_left/right_cur`. 0x3FFF now gives ≈full-volume output.
- **SPU volume register fixed mode**: `vol_left = value & 0x3FFF` (wrong). Fixed: sweep mode (bit15=1) stores raw 14-bit; fixed mode (bit15=0) stores `(value & 0x7FFF) << 1` for 15-bit range.
- **SPU sweep direction bit**: `(reg >> 13) & 1` was extracting wrong bit. Per PSX-SPX bit7 is Direction (0=inc, 1=dec). Fixed to `(reg >> 7) & 1`.
- **SPU CD audio feed**: `spu->cd_audio_left/right` were never updated (always 0). Now fed from CDROM audio FIFO on every SPU tick (per-sample), with CD volume scaling applied. XA/CDDA audio now mixed into SPU output when `SPUCNT` CD-audio-enable bit is set.
- **SPU vol_left/right_count**: Sweep counters not reset on key-on. Fixed: `vol_left_count = vol_right_count = 0` in key-on handler.
- **SPU sweep tick**: `spu_voice_sweep_tick()` was not being called per-sample. Now called for all 24 voices each SPU tick, after `spu_voice_get_sample`.
- **SPU reverb output**: Was mixing `rev_l >> 2` (25% reverb). Fixed to `rev_l` (100%).

### Fixed (pre-existing, carried forward)
- **Performance Overhead**: Removed all `TRACE` logs globally and stripped `DEBUG` logs from hot paths (`bus.c`, `timers.c`, `cpu_instructions.c`, `dma.c`).
- **CDROM Command Dropping**: Removed strict `interrupt_flag != 0` check in `cdrom_write8` when processing new commands (e.g. `SeekL`), resolving game load hangs (Ace Combat 2).
- **BIOS Syscall Spam**: Expanded B0 table in `cpu_bios.c` to `0x5D`; ignored out-of-bounds B0 calls, stopping `[BIOS] B0(unknown)` spam during game load.
- **Log Formatting**: Standardized log strings codebase-wide; stripped trailing newlines for ImGui compatibility.

---

## [sound] — 2026-05

### Added
- **SPU (Sound Processing Unit)**: Full implementation across 6 source files (`spu.c`, `spu_voice.c`, `spu_adsr.c`, `spu_mixing.c`, `spu_dma.c`, `spu_irq.c`).
  - 24 voices with XA-ADPCM decoder (5 prediction filters, Gaussian 4-tap interpolation)
  - ADSR envelope (Attack/Decay/Sustain/Release phases)
  - Noise generator, reverb (IIR/comb/allpass), capture buffer (4 channels)
  - 512 KB SPU RAM, DMA transfer (manual/DMA read/write)
  - IRQ9 address boundary detection
  - Circular sample buffer (4096 stereo frames) for SDL audio callback synchronization
  - `spu_step()` integrated in main emulation loop with CPU cycle timing
- **ImGui SPU Debug window**: Voice status table (24 voices, ADSR phase, pitch, volume), audio peak level meters (L/R), buffer fill indicator, global control/status registers, transfer/DMA state, reverb registers.
- **SPU log category**: `LOG_SPU_*` macros for ERROR/WARN/INFO/DEBUG/TRACE; visible in ImGui SPU log viewer.

### Status
Sound pipeline active. Minor timing/sync issues remain (see commit `0a8d69e`).

---

## [debug-ide] — 2026-03 to 2026-05

### Added
- **CPU Disassembler**: PCSX-Redux-style disassembly window with 128-row virtual list (`ImGuiListClipper`), row highlights for PC (yellow) and breakpoints (dark red), clickable breakpoint dots, Go-To-Address footer.
- **Run / Pause / Step controls**: F5 (run/pause toggle) and F11 (single step) wired via `debug_ui_step_requested()` edge-triggered flag into main emulation loop.
- **Breakpoint manager**: ImGui table (address / enable checkbox / delete); click address to jump disassembly view; Add BP input field.
- **CPU Registers window**: PC / SR / Cause / EPC / HI / LO header + 32 GPRs in two-column table with MIPS register names; non-zero registers highlighted yellow.
- **IDE-style Debug UI**: Complete `src/debug_ui.cpp` rewrite (~400 lines). Menu bar: `Emulator` | `Debug` | `Logs` | `Options`. `[PAUSED]` indicator. Initial docking layout: PS1 Display (main), Disassembly (right-top 62%), CPU Registers + Breakpoints (right-bottom 38%), component logs (bottom 22%).
- **FBO Display Rendering**: PS1 display rendered to off-screen OpenGL FBO; shown inside dockable/floatable ImGui window.
- **Modular component log windows**: 16 ImGui log windows (one per hardware category), individually dockable. Open All / Close All in `Logs` menu.
- **Debugger struct** (`include/debugger.h` / `src/debugger.c`): breakpoints + per-BP `bp_enabled[]` array, read/write watchpoints, `step_skip_bp` flag to avoid re-triggering when stepping off a breakpoint.
- **GPU partial VRAM upload** (`renderer_upload_vram_rect`): `gp0_image_load` and `gp0_copy_rectangle` upload only the dirty rectangle via `glTexSubImage2D` + `GL_UNPACK_ROW_LENGTH` instead of full 512 KB VRAM texture.

### Changed
- **Texture window uniform**: Fragment shader `uvec2 tex_window_and` + `uvec2 tex_window_or` consolidated into single `ivec4 u_texWindow` (and_x, and_y, or_x, or_y).
- Standard command-line logging (stdout/stderr) fully disabled; all output routes to ImGui windows.

### Fixed
- **Textured rectangle VRAM upload ordering**: `renderer_set_semi_trans_mode` (which triggers batch flush) was called before `renderer_upload_vram` → stale VRAM texture → wrong CLUT → rainbow corruption on font sprites. Fixed by uploading VRAM before flush-triggering call.
- **ImGui crash** (`TableSetBgColor` outside table scope): Replaced with `GetWindowDrawList()->AddRectFilled` using cursor screen position.

---

## [ui-refactor] — 2026-03

### Added
- **ImGui docking + multi-viewports**: `ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_ViewportsEnable`; windows can be dragged outside the main SDL2 window.
- **Kernel/TTY Logging**: BIOS `printf`/`putchar` syscalls routed to ImGui "BIOS" log window.
- **ImGui Log Level Selector**: Global verbosity (TRACE → SILENT) toggleable at runtime from `Options` menu.
- **rxi/log integration** (`src/log.c`): 16 categories × 6 levels, per-category filter.

### Changed
- Main SDL2 window is now a pure ImGui DockSpace host; all UI rendered via ImGui. No terminal output.

---

## [cpu-cycle-model] — 2026-03

### Added
- **DuckStation-style CPU cycle model**: `Cpu.downcount` decrements per instruction; `eventq_dispatch_due` fires when ≤ 0.
- **MulDiv stall emulation**: MFHI/MFLO stall until `muldiv_completion_tick` (MULT = 7 cy, DIV = 37 cy).
- **Bus modular split**: `src/interconnect.c` split into `src/bus.c` (memory routing), `src/bus_irq.c` (IRQ controller), slim `src/interconnect.c` (init + event glue).

### Fixed
- **Timer clock source routing**: All three timer clock_source bit pairs now correct per PSX-SPX spec. Timer2 `clock_source=2` fell through to skip → BIOS Timer2 polling loop froze post-menu.
- **GPU draw offset**: `gp0_drawing_offset` (GP0 0xE5) now calls `renderer_set_draw_offset` so double-buffer offsets (0,1)/(0,241) are applied to vertex shader uniform.
- **VRAM blit color channel order**: R and B were swapped in `renderer_blit_vram` (PSX RGB555: R=bits 0–4, B=bits 10–14).
- **DMA completion IRQ**: `interconnect_perform_dma` raises IRQ3 on 0→1 I_STAT[3] transition only; removed PCSX ReARMed re-raise hack that caused infinite loop on wrong-channel DICR ACK.
- **SIO range ordering** in `store16/store8`: SIO (0x1F801040–0x1F80104F) now checked before MEM_CONTROL_END (0x1F80107F); SIO read handler before generic hwregs catch-all so JOY_STAT TX_RDY bits are visible to BIOS.
- **Drawing area initial bounds**: `(0,0,0,0)` → `(0,0,1023,511)`; `renderer_set_drawing_area` called from `gpu_reset_state`.

---

## [stable-bios-menu] — 2026-03-17

### Working at this milestone
- BIOS boot sequence (SCPH-1001 US): Sony logo animation → interactive menu → cursor → navigation
- GPU: GP0/GP1 command dispatch (polygons, rects, lines, VRAM transfers), double-buffer, blit, draw offset, scissor
- DMA: linked-list and block/request for GPU (ch2) and OTC (ch6)
- Timers 0/1/2: counter, mode, target registers; VBlank event scheduler
- CDROM: command handling, disc read, IRQ delivery
- SIO/Controller: digital pad protocol, keyboard-to-gamepad mapping (WASD/SPACE/E/C/Z/X)
- GTE: geometry transformation engine, load delay slots
- I-Cache: 256-line 4-word cache with tag/valid bits
- Event scheduler: DuckStation-style downcount dispatch for VBlank, timers, CDROM

### Added (leading up to this milestone)
- **Controller input system**: Keyboard→PSX gamepad mapping (`src/controller.c`). WASD=D-pad, SPACE=Start, Backspace=Select, E/C/Z/X=△○×□, Q/R=L1/R1, Shift/Ctrl=L2/R2.
- **SIO RX priming**: `rx_data` initialized so BIOS GetC (B0[0x32]) finds input immediately without spinning.
- **IRQ7 (IRQ_CTRLMEMCARD)**: Added STAT_IRQ + `pending_irq` in `sio_handle_transfer` when CTRL bit 12 enabled.
- **I-Cache**: 256-line 4-word instruction cache with tag/valid bits (`src/cpu/cpu_icache.c`).
- **GTE**: Geometry Transformation Engine (`src/gte.c`) with load delay slots.
- **Event Scheduler**: DuckStation-style event dispatch (`src/event_scheduler.c`).

---

## [sony-logo] — 2026-02

### Achieved
- Sony Computer Entertainment logo renders correctly (textured polygon, correct colors)
- VRAM debug view visible in early renderer (raw VRAM shown in top-right corner)
- GPU command pipeline running: DMA ch2 linked-list → GP0 decode → OpenGL draw

### Added
- **BIOS TTY capture**: EXP2+0x23 DUART write captured in `interconnect_store8` → line buffer → `fprintf(stderr, "[BIOS TTY] ...")`. A0/B0 syscall side-channel via `handle_a0/b0_syscall` (LLE — no fake `$v0` returns).
- **DMA IRQ**: `interconnect_perform_dma` signals IRQ3 on transfer complete.
- **GPU double-buffer**: VRAM regions (0,0) and (0,240) alternated per frame via draw offset GP0(0xE5).

---

## [init] — 2025

### Project start
- Based on *PlayStation Emulation Guide* by Lionel Flandrin (`guide.tex`, ~11K lines). Implementation follows the guide end-to-end: CPU instruction-by-instruction BIOS trace, memory bus, DMA, GPU command dispatch, OpenGL renderer, debugger, and instruction cache.
- Full standard MIPS I instruction set implemented: arithmetic, logic, load/store, branch, jump, shift.
- COP0 exception handling: SYSCALL, overflow, address error, bus error; EPC/SR/Cause registers.
- Memory bus: RAM (2 MB), BIOS ROM (512 KB), Scratchpad (1 KB), hardware register routing skeleton.
- Basic GPU: OpenGL 3.3 + GLEW backend, monochrome/textured polygon rendering.
- CDROM: initial command dispatch and disc image read (CUE/BIN).
- Timers: counter registers, mode, target; clock source stubs.
- DMA: controller init; SBUS registers for BIOS hardware detection.
- Multiple reboots/restarts as architecture was refined; settled on pure C11 single-pass build (`make`).
- First BIOS progression: instruction fetch, memory map routing, early loop recognition.
