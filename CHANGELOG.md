# Changelog

All notable changes to ZonistationOne are documented here.
Format follows [Keep a Changelog](https://keepachangelog.com/en/1.0.0/).

---

## [Unreleased]

### Added
- **`src/core/system.c` / `include/system.h` — unified core "run one frame" driver**: extracted the CPU + event-scheduler timing loop out of `main.c`. `system_init()` seeds the VBlank and timer events; `system_run_frame()` runs the machine until the VBlank event marks the frame boundary (`Interconnect.frame_complete`). `main.c` is now a thin host shell (SDL/GL/audio/threads + framecap) whose per-frame work is a single `system_run_frame()` call — the ~40-line nested chunk loop is gone. Threading is unchanged (GPU render thread + SPU audio thread still started by `main.c`). Mirrors the DuckStation/PCSX-Redux split of a thin outer loop over a core that owns all timing.

- **VRAM Viewer (PCSX-Redux-style)**: Rebuilt the ImGui VRAM viewer (`src/debug_ui.cpp`) to match Redux's `vram-viewer` widget: selectable decode modes (4/8/16/24 bpp), 24bpp byte-phase shift, selectable CLUT (right-click a pixel), greyscale and mask-bit views, a 16×16 pixel grid and a 64×256 texture-page grid, an outline of the active CRTC display area, cursor-anchored wheel zoom, drag-to-pan, a magnifier lens, and an exact per-pixel readout (raw / 5:5:5 / mask / bytes / 24bpp / tpage) read straight from the CPU-side VRAM buffer. Decode modes are driven through a new `VramViewParams` on the renderer (`renderer_set_vram_view_params`, `include/renderer.h`). The 2 MB/frame VRAM→RGBA8 snapshot is now only taken while the window is open (`debug_ui_vram_viewer_open`).
- **Lua debug bindings**: `emu.gpustat`, `emu.display_area`, `emu.vram16(x,y)`, `emu.vram_upload_rect`, `emu.gpu_pool`, `emu.gp0_opcode/word/word_count`, `emu.timer(i)`, and `emu.mdec_block/info/scale/qtable/in_peek/in_count/dma` — query-only helpers (zero cost unless a script calls them) plus a few `lua_debug_notify` probe points (`mdec_macroblock`, `gp0_vram_upload/copy/image_start`, `vram_full_upload`) for live MDEC/GPU pipeline tracing.
- **MDEC (Macroblock Decoder)**: Full implementation in `src/core/mdec.c` / `include/mdec.h`.
  - State machine ported 1:1 from DuckStation (`IDCT_Old`, `DecodeRLE_Old`, `YUVToRGB_Old`).
  - 6-block color path (Cr,Cb,Y1-Y4) and mono path; 4-bit, 8-bit, 24-bit, 15-bit output modes.
  - DMA in/out FIFO (2048 HW in, 768 W out); integrated with DMA ch0 (MDECin) and ch1 (MDECout).
  - Status register: data-out-empty, data-in-full, command-busy, DMA-request bits.
- **Memory Card**: SIO memory card protocol state machine (`src/core/sio.c`).
  - Device select (0x81), Read/Write/GetID commands, sector addressing, checksum verification.
  - `sio_load_memcard()` loads `.mcd` image files; auto-loads `memcard1.mcd` on startup.
  - FLAG byte (0x08 = directory unread on powerup) per PSX-SPX spec.

### Changed
- **Logging defaults to INFO again, and the level is settable per run**: `current_log_level` had drifted to `DEBUG`, contradicting the documented default. The hot paths log per DMA transfer, per GP0 command and per MDEC macroblock — thousands of formatted lines per frame during FMV playback, enough to visibly drag the emulator below real time until the level was lowered by hand. Default is INFO, and `ZS1_LOG_LEVEL=silent|error|warn|info|debug|trace` sets it at startup. Measured after the change: ~87% of real time during FMV playback.
- **VRAM unified into one GL texture (DuckStation `GPU_HW` model)**: the renderer kept three unsynchronized VRAM stores and the display sampled the wrong one — rasterized primitives lived only in `display_texture` (never written back to VRAM: "Gap B"), while CPU/MDEC uploads lived in `gpu.vram.data` and only reached the screen through a fragile 24bpp mirror hack whose column mapping depended on the current `display_x`, decoupled from the game's double buffering. Now ONE RGBA8 texture is the FBO colour attachment (rasterization target), the CPU/MDEC upload target, and the scanout source, so what is written to VRAM is what the display reads. PSX halfwords are stored 5:5:5:1 expanded to 8 bits/channel (`(v<<3)|(v>>2)`), which round-trips losslessly so CLUT index bits survive. A new scanout-extract pass renders the CRTC display window out of that texture, unpacking per depth (15bpp direct fetch; 24bpp recombines two texels and byte-shifts per pixel — DuckStation `GenerateVRAMExtractFragmentShader` / `GPU_SW::CopyOut24Bit`), and `draw_ps1_display` shows it 1:1 instead of cropping the FBO. The rasterizer's Y flip was removed (vertex shader + scissor) so PSX line N is VRAM texel row N — the same row an upload writes and the scanout reads; rendering flipped while uploading unflipped was why FMV frames and rasterized output disagreed about where a scanline lives. Removed with it: the 24bpp mirror hack and its `dst_x`/`depth24` fields, the disabled `renderer_apply_vram_readback` bridge and its 1.5 MB buffer, and the throttled `glGetTexImage` that fed it. (Texture sampling still uses the R16UI mirror; folding that in via a read-shadow ping-pong remains for a follow-up.)
- **Timing unified under one scheduler authority (interpreter-native, PCSX-Redux `Counters` model)**: timers were the only subsystem not driven by the event scheduler — they were stepped by hand in the main loop (`timers_step`) once per coarse chunk, alongside a half-wired, conflicting `EVQ_TIMER0/1/2` event path. Timers are now first-class scheduled events. The counter is **derived on read** as `(cpu_cycle_counter - cycle_start) / rate` (no per-tick increment loop, no fractional accumulator — `src/core/timers.c`, reusing the existing `cycle_start`/`rate` fields), IRQ/reset fires from the scheduled event at the next target/overflow through a single shared IRQ path, and every register read/write/gate-change catches the timer up on demand. Dotclock (Timer0) / hblank (Timer1) rates now derive from the GPU's active video mode via `gpu_dotclock_hz`/`gpu_hblank_hz` (CRTC 53'693'175 NTSC / 53'203'425 PAL, dotclock divider from GPUSTAT h-res, hblank per-scanline), not fixed NTSC constants. Not DuckStation's two-counter `pending_ticks` model — unnecessary for an interpreter whose `cpu_cycle_counter` is always "now". New `src/core/system.c` owns the per-frame run loop; `main.c` shrank to a thin host shell.

### Fixed
- **DMA completion interrupts were lost after the first DICR acknowledge (FMV playback blocker)**: `interconnect_set_irq_line` only latches I_STAT on a low→high edge, but the DICR acknowledge path in `dma.c` cleared `irq_status` while leaving `irq_line_state` high, and the four completion sites poked the line directly from behind a `!(irq_status & IRQ_DMA)` guard. Once a game acknowledged through DICR alone, the DMA line stayed logically high forever and every later completion produced no edge — the interrupt simply vanished. Ace Combat 2's movie player is a direct casualty: its CD ISR marks a sector descriptor "transfer running" when it kicks the ch3 payload DMA and relies on the DMA interrupt handler to mark it "ready", so the descriptors of one frame froze and the movie died after 49 of its 1905 frames while the drive kept streaming. New `dma_update_irq()` (`src/core/dma.c`) is now the only place allowed to touch the line: it recomputes DICR's master flag and acts on the transition — asserting on false→true, deasserting on true→false. Re-asserting an already-high line instead restarts the interrupt on every call and traps the CPU in the handler (reproduced as a freeze on the SONY splash). Every DICR write goes through it too, so a game that enables interrupts *after* writing CHCR still gets its completion. Mirrors DuckStation's `DMA::UpdateIRQ` (`dma.cpp:500-507`) and its `UpdateIRQ()` call on the DICR write path (`dma.cpp:457`, see the note at `dma.cpp:401`). Measured over the same 130 s run: MDEC frame commands 46 → 386, decoded macroblocks 9121 → 365 800, MDEC busy continuously instead of stalling after ~2 s of movie.
- **GPU block DMA sampled guest RAM too late (FMV column striping)**: `REQUEST`/`MANUAL` transfers on ch2 were sliced across event ticks like the linked-list path, so the source words were read long after the guest kicked the transfer. The movie player owns only two staging buffers and refills one as soon as its transfer is kicked, so the deferred read picked up the *next* column's pixels — VRAM ended up holding just two distinct payloads repeated across all twenty 16-pixel columns of a frame (measured: 20 uploads, 2 unique hashes). Block transfers now consume the buffer at kick time, as both references do (DuckStation delays only the completion, never the data read; PCSX-Redux copies immediately and schedules just the IRQ). The linked list stays sliced — its node chain is built before the kick and is not rewritten under us.
- **DMA kicks arriving during an in-flight slice were dropped outright**: a kick for a channel whose sliced transfer had not finished was silently discarded, losing a whole transfer — during FMV playback the player kicks the next column upload while a GPU linked list is still draining, and that column stayed blank. Real hardware cannot lose the transfer, so the GPU channel now drains its outstanding slices (bounded) and then runs the new transfer. MDEC's two channels still drop such kicks: they gate on each other's FIFO readiness, so a synchronous drain there deadlocks (it hangs at the first FMV frame) — queueing them is left for the MDEC path itself.
- **CPU/MDEC uploads never reached the displayed texture**: `glTexSubImage2D` wrote the unified VRAM texture while that texture was still attached as the bound FBO's colour attachment — undefined in GL, and in practice the driver dropped the write. Detach (bind FBO 0) around the upload. Also, the every-frame full-VRAM sync must only refresh the R16UI sampling mirror and must NOT blit into the unified texture: `gpu.vram.data` holds no rasterized pixels, so blitting it erased each frame's drawing (black/flickering screen).
- **24bpp scanout dropped bit 15**: the halfword recovered from the 5:5:5:1-expanded texel discarded bit 15, which in 24bpp is a data bit of the packed byte stream (not a mask flag), corrupting every pixel whose high byte was ≥ 0x80. Restored from alpha. Together with the upload fix, the FMV region on screen went from `max=1` / 0 non-black pixels to `max=128` / 35840.
- **Frozen timer counter reads**: a game busy-polling a timer counter (Ace Combat 2 read Timer1 ~300k times/run) saw a value frozen between main-loop chunks (~99% of reads returned an unchanged value at a later cycle), because timers only advanced once per chunk. Reads now derive the live value from the global cycle counter, so a poll always sees continuous advance (DuckStation `InvokeEarly` / Redux `psxRcntRcount` behaviour).
- **Timer IRQ double-fire**: the two parallel timer mechanisms requested the same IRQ line with different guards (`already_pending`/I_STAT vs `interrupt_requested`), and both reset the counter — allowing duplicate edges and spurious resets. Collapsed to a single event-driven IRQ/reset path.
- **Event scheduler wrap-unsafe comparisons**: `eventq_schedule`'s "is this event sooner" test and the `evq_next_cycle` recompute compared absolute cycle values (`>`/`<`), which are wrong across the uint32 `cpu_cycle_counter` wrap (~every 127 s). Both now use signed-delta compares.
- **MDEC IDCT scale-table not transposed (FMV macroblock-grid blocker)**: `mdec_handle_set_scale` (`src/core/mdec.c`) stored the 64-entry scale/IDCT matrix sequentially, but the IDCT reads it as `scale_table[y*8 + u]` (frequency → output position). It must be transposed on the way in — DuckStation does this in `SetScaleMatrix()` (`scale_table[y*8+x] = values[x*8+y]`), which the rest of `mdec.c` was ported from while this one step was missed. With the wrong-orientation basis every block decoded with a distorted IDCT: a DC-only macroblock (which must decode to a flat patch of colour) instead came out as a smooth blob fading to its edges, so FMV frames rendered as a regular grid of blobs, one per macroblock. Now transposed; DC-only blocks decode flat. Verified in isolation via a Lua `block_rgb` probe and against a reference IDCT.
- **DMA sliced-transfer re-kick (FMV VRAM corruption)**: sliced, event-scheduled transfers (GPU ch2, MDEC ch0/1) stay `enable/busy` for their whole duration, so any later kick that re-inspects the channel — a `DPCR` write unblocking channels, or software re-poking `CHCR` — restarted the transfer from `base_addr` and re-sent the entire payload. `interconnect_perform_dma` (`src/core/bus.c`) now no-ops when a slice for that channel is already in flight (`dma_slice_in_flight`), matching the fact that DuckStation's `TransferChannel()` is resumable (advances `base_address` and continues rather than rewinding). The duplicate GPU ch2 payload had been arriving with no `GP0(0xA0)` in front of it, so MDEC pixel words (`0x80…` chroma) were decoded as GP0 commands — producing ~14 000 phantom VRAM copies/frame, a saturated staging pool, and a wrecked VRAM. Phantom copies dropped from ~14 000 to 1; MDEC TTY timeouts (`MDEC_in_sync timeout` / `time out in decoding`) from 27 to 0.
- **PAL/NTSC frame timing (PAL titles ran ~20% fast)**: the VBlank period and host frame pacing were hardcoded to NTSC 60 Hz (`564480 = 33868800/60`) in both `event_scheduler.c` and `main.c`. New `gpu_cycles_per_frame()` (`src/gpu/gpu.c`) derives the period from the GPU's current video mode using the real clock relationship (3413×263 lines NTSC / 3406×314 PAL GPU ticks, converted to system ticks by `×451584/715909` NTSC and `/709379` PAL — DuckStation `gpu.cpp:964-989`): 566203 cy / 59.82 Hz NTSC, 680823 cy / 49.75 Hz PAL. Re-derived each frame so a mid-run GP1(08) video-mode switch takes effect. This drove the whole PAL timebase (VBlank rate, and with it FMV/audio pacing) 20% too fast.
- **MDEC DMA pacing (input-FIFO starvation / decode timeouts)**: MDEC ch0/ch1 slices used the GPU path's flat 64-words-per-1000-cycles quantum (~15.6 cy/word), slow enough that libmdec's `DecDCTinSync`/`DecDCToutSync` spin loops gave up mid-FMV. Now slices are capped at 100 words (DuckStation `SLICE_SIZE_WHEN_DECODING_MDEC`) and the inter-slice stall is the real DRAM hyper-page cost of the words actually moved (`words + (words+15)/16`, DuckStation `Bus::GetDMARAMTickCount`), with a fixed back-off only when both directions are FIFO-blocked.
- **24bpp display decode**: `renderer_execute_one_vram_update` (`src/gpu/renderer.c`) always unpacked the display area as 5:5:5. When GPUSTAT.21 (24bpp) is set it now reads packed 3-bytes-per-pixel triplets (DuckStation `GPU_SW::CopyOut24Bit`), with the display-texture column derived from the VRAM halfword column at the 3-byte/2-halfword ratio. VRAM uploads (`renderer_upload_vram_rect`) now also mirror into `display_texture` — previously only GL-rasterized pixels reached it, so game/MDEC-painted display content (FMV frames, 2D backdrops) could never appear. Fixes the FMV display path (decoder output confirmed reaching VRAM); the movie itself still decodes near-black pending an upstream STR-demux/timer investigation.
- **VRAM viewer upside-down**: the viewer sampled its texture with a flipped `v` (`(0,1)-(1,0)`), inherited from the old viewer. Unlike `display_texture` (an FBO the GL rasterizer writes in y-up clip space), the viewer texture is a straight CPU upload of VRAM rows 0..511 in order, so `v=0` is already VRAM y=0 — all of VRAM had been shown vertically mirrored.
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
