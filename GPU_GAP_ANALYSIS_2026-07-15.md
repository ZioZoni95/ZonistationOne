# GPU — Full Structural Gap Analysis

**Date**: 2026-07-15
**Compared against**: `duckstation_ref/` (DuckStation, C++) and `pcsx-redux/` (PCSX-Redux, C-style) — both local clones in this repo.
**Scope**: split out from `GAP_ANALYSIS_REFACTOR_2026-07-13.md` §8 given the "full GPU" scope requested for this pass — GP0/GP1 dispatch, GPUSTAT, renderer/rasterization architecture, CRTC/timing, command-timing model. Linked from §8, which now carries only a summary. Same priority framework as the main doc (Critical/High/Medium/Low).
**Trigger**: this pass was requested after `logs/GTE.log`/`logs/GPU.log` showed zero GTE ops and zero GP0/GP1 draw commands for an entire boot session — later traced (see main doc, Phase 2.5 addendum) to the game itself hitting a self-halting `SystemError('C',2)` before ever reaching a draw call, i.e. **not** a GPU bug. The gaps below were found independently while auditing the GPU code for this pass and are real regardless of that finding.

**Files**: `src/gpu/gpu.c` (469L), `src/gpu/gpu_commands.c` (1480L), `src/gpu/gpu_helpers.c` (131L), `src/gpu/renderer.c` (1499L), `src/gpu/vram.c` (101L), `include/gpu.h`. (There is no `src/gpu/debugger.c` — the system debugger lives at `src/core/debugger.c`, moved there in Phase 0; it is CPU/memory-only, no GPU-specific content.)

---

## 1. GP0/GP1 command dispatch

**Own implementation**: `gp0_table[256]` (`gpu_commands.c:1203-1361`), a compile-time-populated function-pointer array indexed by the command byte's top bits, processed through a real 16-word hardware FIFO (`gpu->gp0_fifo[16]`) that is always drained to empty within the same `gpu_gp0()` call (structurally present, but never actually holds cross-instruction backpressure).

Coverage, verified against the table:
- **Polygons**: all flat/gouraud × tri/quad × mono/textured(raw+blend) × opaque/semi combinations are real handlers (0x20-0x3F).
- **Lines**: mono/shaded 2-point (0x40-0x43, 0x50-0x53) are real handlers. Polylines (0x48-0x4B, 0x58-0x5B) use a dedicated state machine (`GP0_MODE_POLYLINE`) accumulating into `polyline_buffer[256]` until the `0x50005000` terminator pattern, then `flush_polyline` emits N-1 segments.
- **Rectangles**: variable/1×1/8×8/16×16 × mono/textured/raw/semi, all real (0x60-0x7F).
- **Fill** (0x02, real, correct PSX-SPX masking/rounding), **VRAM→VRAM copy** registered for the full mirror range 0x80-0x9F (32 entries, overlap-safe directional copy respecting mask setting).
- **State commands** 0xE1-0xE6 (draw mode, texture window, drawing area TL/BR, drawing offset, mask-bit setting) — all real, full bit coverage including the two rectangle-flip bits in 0xE1.
- **Gap (Low)**: CPU→VRAM (0xA0) and VRAM→CPU (0xC0) are registered at **exactly** those two opcodes, not the full documented 0xA0-0xBF/0xC0-0xDF mirror range (`DOCS/graphicsprocessingunitgpu.md:490-492`: "the high 3 bits are set to the values 4/5/6... the remaining 29 bits are ignored"). 0xA1-0xBF/0xC1-0xDF fall through to the unhandled default (1-word NOP + warning) instead of initiating a transfer. Low real-world risk since games essentially always use the canonical 0xA0/0xC0 forms — but flagged as a possible corroborating cause for the missing-boot-logo-graphic symptom (see main doc §8).
- Unhandled-by-design: 0x03-0x1E, 0xE0, 0xE7-0xFF fall to the `NULL` default (1-word NOP + rate-limited warning) — matches the documented "mirrors of GP0(00h) NOP" behavior for that range.

**GP1** (`gpu_gp1`, `gpu.c:240-265`): complete — reset, FIFO reset, IRQ ack, display enable, DMA direction, display start, H/V display range, display mode (incl. interlace/vres/vmode/depth, unsupported "Reverseflag" bit 7 logged-only), texture-disable-allow, `GetGPUInfo` subfunctions (texpage/texwindow/drawing area/offset/GPU type). This matches PSX-SPX's documented command set in full.

**DuckStation comparison**: GP0 is similarly table-driven (`s_GP0_command_handler_table`, `constexpr std::array<GP0CommandHandler,256>`), processed by a `TryExecuteCommands` state machine (`Idle/ReadingVRAM/WritingVRAM/DrawingPolyLine`) — structurally the same shape as this project's. GP1 is a plain `switch`, not table-driven, on `GP1Command` enum values — same as here. No material structural gap on dispatch shape.

**PCSX-Redux comparison**: uses a templated "double dispatch" pattern — `GPU::Poly<Shading,Shape,Textured,Blend,Modulation>` (32 statically-instantiated combinations covering the same permutation space this project's dispatch table covers by opcode range) rather than a flat function-pointer array. Functionally equivalent coverage. One notable PCSX-Redux-specific finding: GP1(0x1F) "IRQ1 request" is parsed as an enum value but **has zero call sites anywhere in that codebase** — i.e. even a mature reference has an acknowledged-but-unimplemented GP1 command; not something to copy, just useful context that 100% GP1 coverage isn't a universal bar every reference clears either.

**Priority**: Low (item 6 above); dispatch shape itself has no material gap against either reference.

---

## 2. GPUSTAT

**Own implementation** (`gpu_read_status`, `gpu.c:270-331`): bits 0-15 (page/semi-transparency/texture depth/dither/draw-to-display/mask bits/field/texture-disable), 16-22 (resolution/vmode/depth/interlace), 23 (display disable), 24 (GPU IRQ, real), 29-30 (DMA direction), 31 (odd/even, driven by CRTC) are all tracked from real state.

**Gap (Medium) — bits 26/28 hardcoded true**: bit 26 ("ready to receive cmd word") and bit 28 ("ready to receive DMA block") are hardcoded `true` with an explicit comment ("synchronous emulator, never busy") rather than derived from any real throughput/timing state. Bit 27 ("ready to send VRAM to CPU") is real (tracks `GP0_MODE_IMAGE_STORE` + remaining words). Bit 25 (DMA/data request) is correctly *derived* from the (partly hardcoded) ready bits per the documented DMA-direction-dependent formula — the derivation logic is right, but two of its three possible inputs are constants. Bit 14 ("Reverseflag") is explicitly documented as unemulated.

This is the direct GPUSTAT-level symptom of the missing command-timing model (§4 below) — see that section for the fix path.

**DuckStation comparison**: `GPUSTATReg` bitfield covers the same 32 bits, plus inlined helper predicates (`IsMaskingEnabled()`, `SkipDrawingToActiveField()`, `InInterleaved480iMode()`). Critically, its dynamic bits (`ready_to_send_vram`/`ready_to_receive_dma`/`dma_data_request`/`gpu_idle`) are recomputed live from `pending_command_ticks`/FIFO state on every transition — i.e. the same three bits this project hardcodes are, in DuckStation, a direct function of real timing state.

**PCSX-Redux comparison**: notably **also** never clears `GPUSTATUS_IDLE`/`GPUSTATUS_READYFORCOMMANDS` during rendering (confirmed by grep across its GPU backends) — same simplification as this project. So on this specific axis, this project matches PCSX-Redux's behavior, not a gap unique to this codebase; DuckStation is the outlier in having a real busy-bit model. Worth citing precisely because it means "implement busy bits" is a DuckStation-specific accuracy upgrade, not a baseline every reference meets.

**Priority**: Medium — real but the GPUSTAT bits it affects are typically only load-bearing for games doing precise DMA-chaining/FIFO-backpressure logic; most titles work fine against always-ready bits (evidenced by PCSX-Redux shipping the same simplification).

---

## 3. Renderer / rasterization architecture — the two headline gaps

**Own implementation**: `renderer.c` is a real OpenGL 3.3 core-profile renderer, not a logging stub. CPU thread records draw commands into a double-buffered `GpuFrame`/`GpuBatch`/`GpuOp` structure (`renderer_push_triangle/quad/line`); a dedicated `SDL_Thread` drains it and issues actual `glDrawArrays` calls (`renderer_draw_gl`, `renderer.c:829-918`). Texture mapping (4bpp/8bpp paletted + 15bpp direct, with texture-window AND/OR masking), all 4 PS1 semi-transparency blend modes via two-pass opaque/blend-discard + real GL blend state, and per-primitive-type-correct 4×4 Bayer dithering are all genuinely implemented and match the documented PS1 blend equations.

### Gap A (High) — mask-bit only applies to Fill/VRAM-copy/CPU-upload, never to rasterized primitives

`vram_write_masked` (`gpu_commands.c:121-130`) is the only function that respects `force_set_mask_bit`/`preserve_masked_pixels` (GPUSTAT 11/12). Confirmed by grepping every `vram_store16`/`vram_write_masked` call site in `gpu_commands.c`: **only 4 sites exist** — Fill (line 290, explicitly unmasked, correct per spec), VRAM→VRAM copy (line 1117), and the two halves of CPU→VRAM upload (lines 1396, 1403). **Polygon, line, and rectangle rasterization never call either function at all** — that path goes entirely through the OpenGL renderer, which has no concept of the mask bit (its output color format has no reserved bit for it).

Real hardware — per `DOCS/graphicsprocessingunitgpu.md:475-476` — applies mask-bit checking/setting to "all rendering commands, as well as CPU-to-VRAM and VRAM-to-VRAM transfer commands." This project currently implements it only for the second half of that sentence. Games documented to rely on this (Metal Gear Solid's shadow/fog compositing, Silent Hill's fog-layer masking) would render incorrectly — the mask layer these games composite against would either always block or never block, depending on which pixels happen to already have the bit set from a prior VRAM-copy/upload.

**Deeper architectural cause**: the OpenGL-rasterized output lives *only* in an off-screen `display_texture`/`display_fbo` (RGB8, no reserved mask/alpha channel), and — see Gap B — is never read back into `gpu->vram.data` (the CPU-side 1MB VRAM buffer that the mask-check logic actually operates on). So implementing per-pixel masking for rasterized primitives isn't a small tweak to the existing `vram_write_masked` helper; it requires either (a) giving the OpenGL path a way to read-modify-write the mask bit before each pixel write (a real fragment-shader-side mask check + a stencil/depth-buffer-as-mask-carrier trick, similar to what DuckStation's HW backend does — see below), or (b) reading rasterized output back into the CPU VRAM model and running the existing masked-write logic there, which folds into fixing Gap B anyway.

### Gap B (High, likely the single most consequential GPU gap) — rasterized OpenGL output never reads back into CPU-visible VRAM

Grepping `renderer.c` for `glReadPixels`/`glCopyTexImage`/`glBlitFramebuffer` returns **zero hits**. The CPU-side `vram_texture` sampler (used for texture lookups) and the VRAM-viewer debug texture are updated **exclusively** via `glTexSubImage2D`, sourced only from CPU-side memory pools populated by the Fill/Copy/Upload command paths (`renderer_upload_vram`/`renderer_upload_vram_rect`, called from `gpu_commands.c`'s Fill/Copy/Upload handlers only).

**Practical consequence**: any technique where a game renders polygons into an off-screen VRAM region and then, in the same or a later frame, either (a) samples that region as a texture for a different draw, (b) reads it back via GP0(0xC0) VRAM→CPU, or (c) copies it via GP0 VRAM→VRAM — all common PS1 techniques for render-to-texture effects, dynamic reflections, or sub-screen compositing — will see **stale data from before the polygon draw**, because the GL-rasterized pixels never make it back into the CPU-visible VRAM model that all of those other operations actually read from.

**DuckStation comparison**: architecturally similar split (CPU-thread frontend decodes commands, pushes `VideoThreadCommand` structs to a separate `GPUBackend` on a video thread — same two-thread shape as this project), but its `GPU_HW` backend maintains a real hardware texture-cache (`gpu_hw_texture_cache.cpp`) that *is* kept coherent with rendered output, and its software backend (`gpu_sw_rasterizer`) writes directly into the same VRAM buffer texture lookups read from — there is no code path in either DuckStation backend where a just-rendered region silently returns stale data to a subsequent read.

**PCSX-Redux comparison**: its software backend (`src/gpu/soft/`) rasterizes **directly into a raw `uint16_t* m_vram16` buffer** — the same buffer used for texture sampling and VRAM reads — so this specific staleness bug is structurally impossible there; the OpenGL hardware backend is a separate, opt-in path. This project only has the OpenGL path, which is the direct cause of the gap — there's no software-rasterizer fallback that would sidestep it the way PCSX-Redux's default backend does.

**Priority**: **High**, and worth calling out specifically as the most actionable of the two: unlike Gap A (which needs a genuine per-pixel mask mechanism added to the GL pipeline), Gap B has a comparatively mechanical fix — read rendered framebuffer contents back into the CPU `vram.data` buffer after each batch (or on-demand before any read/copy/texture-sample operation targets a region that was GL-rendered into), which is exactly the `glReadPixels`/`glBlitFramebuffer` machinery that's currently entirely absent.

### Other renderer findings

- **24-bit display mode tracked but never decoded**: `display_depth` (GPUSTAT bit 21) is set correctly from GP1(08), but neither `renderer_execute_one_vram_update`'s RGB conversion nor `renderer_update_vram_viewer` has any code path that unpacks adjacent VRAM halfwords as packed 3-byte RGB when 24-bit mode is active — display always unpacks as 5:5:5 regardless of the tracked mode. — **Low-Medium**

---

## 4. CRTC / timing

**Own implementation**: `CrtcState` (`gpu.h:73-84`) correctly switches `vertical_total` 263 (NTSC) / 314 (PAL) based on vmode. `gpu_crtc_tick` (`gpu.c:64-94`) uses a fixed `CPU_CYCLES_PER_SCANLINE=2146` constant, and — this is the actual gap — is only ever invoked from the VBlank event handler (`evq_handle_vblank`) with a whole-frame's-worth-of-cycles argument, meaning it effectively runs **once per frame**, not incrementally per scanline during the frame. `current_scanline`/`in_vblank`/`interlaced_field` are therefore coarse once-per-frame snapshots, not continuously-accurate per-scanline state. `gpu_update_display_mapping` (display-area computation from GP1 05/06/07/08) is correct and complete — this part is not in question.

**DuckStation comparison**: `CRTCTickEvent` runs as its own dedicated `TimingEvent`, converting elapsed *system* ticks to CRTC ticks via the exact documented GPU master-clock ratios (NTSC `sysclk*715909/451584`, PAL `sysclk*709379/451584`) — i.e. it's driven by real elapsed cycles at sub-frame granularity, not fired once per frame with a lump sum. This is what lets DuckStation model true per-scanline dotclock/hblank feeding into Timers 0/1 (see main doc §5) and any raster effects that depend on mid-frame scanline position.

**PCSX-Redux comparison**: CRTC/display timing is driven by the internal hsync pseudo-counter (`Rcnt[3]`) inside the Counters/Timers module rather than the GPU itself — a different ownership model, but still updates incrementally (per-hsync, i.e. per-scanline) rather than once per frame.

**Priority**: Medium — both references update CRTC state at (at least) scanline granularity; this project's once-per-VBlank-event update is the outlier. Impact is limited to games/effects that specifically depend on mid-frame scanline-accurate GPUSTAT bit 31 (odd/even) or interlace-field state — most boot/menu/gameplay rendering that just waits for VBlank is unaffected.

---

## 5. Command FIFO / GPU execution-timing model

**Own implementation**: every GP0 command handler runs synchronously to completion within the same call — the 16-word `gp0_fifo` is drained to empty inside `gpu_gp0()` every time, so it never holds cross-instruction backpressure. Combined with GPUSTAT bits 26/28 being hardcoded true (§2), there is **no throughput limit modeled anywhere** — not pixel fill rate, not vertex processing rate, not VRAM transfer bandwidth. The double-buffered GPU-thread architecture provides real host-parallelism (CPU emulation and GL rendering overlap for performance), but a full frame's GP0 command batch is only handed to the GPU thread once per frame — no per-command cycle cost is ever charged back to the CPU's cycle counter or the event scheduler.

**DuckStation comparison** (the accuracy benchmark here): implements genuinely realistic variable-latency command timing — documented hardware setup-time tables for polygon setup (`s_setup_time[quad][shaded][textured]`), per-pixel costs via triangle-area/shoelace-formula computation (doubled for textured, +50% for semi-transparent/masked, halved for interlace field-skip), texture-cache-reload-aware rectangle costs (differing by 4bpp/8bpp/16bpp, modeling actual cache-thrashing for wide draws), and fixed costs for state commands/fill/VRAM-copy. These feed a dedicated `TimingEvent` that drains at a 2:1 GPU:system clock ratio, and critically: **any GPUSTAT read or DMA-readiness check forces an early sync of this pending-tick pool** before returning a value, so polling code observes genuinely accurate busy/idle transitions. A `gpu_max_run_ahead` knob (default 128 ticks) bounds how far command decode can race ahead of the simulated clock — a deliberate perf/accuracy tradeoff, not a fidelity gap.

**PCSX-Redux comparison**: also has **no realistic per-command latency model** — confirmed via grep, `PSXINT_GPUBUSY` exists as a defined enum value in the interrupt-scheduling table but is **never scheduled anywhere**, a strong signal this was scaffolded and abandoned even in that reference. What PCSX-Redux *does* time-model is DMA-transfer-completion IRQ latency specifically (`scheduleGPUDMAIRQ`, size-derived, with several game-specific empirically-tuned constants left as commented-out alternatives referencing Tekken 3/Einhänder/Final Fantasy IV/etc.) — a narrower, more pragmatic "just enough to not desync DMA chaining" approach rather than a full command-timing simulation.

**Net**: on this axis, this project and PCSX-Redux are in the same place (no real command-timing model); DuckStation is the significant outlier with a fully worked-out model. Same framing note as §2 — implementing this is a "match DuckStation specifically" upgrade, not a "catch up to the field" one.

**Priority**: Medium — real accuracy gap, but the games most sensitive to it (those doing precise GPU-busy polling for frame pacing or DMA chaining) are a minority; most titles that "just work" against PCSX-Redux's equally-instant model would likely also work here.

---

## Priority summary

| # | Finding | Priority |
|---|---|---|
| A | Mask-bit ignored by rasterized polygons/lines/rects | **High** |
| B | Rasterized GL output never reads back into CPU VRAM | **High** |
| — | No GPU command-timing model / GPUSTAT ready-bits hardcoded | Medium |
| — | CRTC updates once per frame, not per-scanline | Medium |
| — | 24-bit display mode tracked but not decoded | Low-Medium |
| — | CPU↔VRAM opcode range limited to exact 0xA0/0xC0 | Low |
| — | No software-renderer fallback (structural, carried from main doc) | Medium |

## Recommended action, in priority order

1. **Gap B (VRAM readback)** first — most consequential, and the more mechanical of the two headline fixes: add a readback path (`glReadPixels`/`glBlitFramebuffer`) from the render target into `gpu->vram.data` after each rendered batch, or lazily before any VRAM-copy/CPU-read/texture-sample operation touches a GL-rendered region. This also removes the architectural blocker that makes Gap A hard to fix cleanly (see Gap A's "deeper architectural cause" note).
2. **Gap A (mask-bit on rasterized primitives)** — once Gap B's readback path exists, extend the existing `vram_write_masked`-style check into the readback/blit step, or add a fragment-shader-side mask test using DuckStation's depth-buffer-as-mask-carrier trick if avoiding a readback-per-primitive is a performance concern.
3. GPU command-timing model and real GPUSTAT ready bits — port DuckStation's per-command tick-cost tables (§5) if precise DMA-chaining/frame-pacing games are a priority; otherwise low urgency given PCSX-Redux ships the same simplification successfully.
4. CRTC per-scanline granularity — move `gpu_crtc_tick` off the once-per-VBlank call site onto its own more frequent tick (mirrors the fix already applied to Timers' dotclock/hblank feeds in the main doc's §5 history).
5. 24-bit display decode, full 0xA0-0xBF/0xC0-0xDF opcode range — small, low-risk, low-priority fixes; good candidates to bundle with whichever of the above lands first.
