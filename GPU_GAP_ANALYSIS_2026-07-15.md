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

**Correction (2026-07-18)**: the semi-transparency claim above is overstated — it's true only for **textured** primitives. See the new addendum at the bottom of this doc for the confirmed gap (flat/gouraud-shaded semi-transparent primitives render fully opaque) and its fix.

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

---

## Gap B attempt #1 (2026-07-18) — reverted after live regression; real architectural fix identified

**What was tried**: a bolt-on readback bridge — `glGetTexImage` on `display_texture` into a CPU buffer (throttled to 1-in-6 frames for performance), converted RGB8→RGB555 and merged into `gpu->vram.data` via a new `renderer_apply_vram_readback()`, called each frame before CPU execution (so fresh CPU-driven GP0(0xA0) uploads wouldn't get clobbered by the readback itself — this ordering bug was hit and fixed first).

**Live-tested regression, reverted (code kept but disabled behind `#if 0` in `src/main.c`, see comment there)**: even with correct ordering, the SONY splash's text turned into solid black rectangles. Root cause: `display_texture` only holds *real* content for pixels actually GL-rasterized this session (polygons/lines/rects drawn via `renderer_draw_gl`). VRAM regions used purely as **off-screen CPU-upload storage** — font/glyph atlases, sprite sheets, CLUTs uploaded via GP0(0xA0) and never themselves "drawn" as a primitive — are never touched by GL rasterization, so they sit at `display_texture`'s clear/initial color (black) forever. A full 1024×512 readback-and-overwrite of `gpu->vram.data` blindly stamps that black over the real, valid CPU-uploaded texture data in those off-screen regions, corrupting it for every subsequent textured draw that samples from there (exactly what made the splash text disappear — its glyph atlas got zeroed out).

**Also found, same attempt**: performance regression — `glGetTexImage` of the full 1024×512×3 buffer every frame forced a GL pipeline stall severe enough to drop the emulator to roughly 1/8th normal speed (measured: 120 VBlanks in 25 real seconds vs. the expected ~1500). Throttling to 1-in-6 frames mostly fixed this in isolation, but is moot given the correctness regression above.

**Real fix, confirmed by reading `duckstation_ref/src/core/gpu_hw.cpp` directly**: DuckStation does not have (and structurally cannot have) this bug, because it uses **one single `m_vram_texture`** for everything — it's simultaneously the GL render target for rasterized draws (`SetRenderTarget(m_vram_texture.get(), ...)`), the texture-sampler source for subsequent texture reads (`SetTextureSampler(0, m_vram_texture.get(), ...)`), and the display source (`VideoPresenter::SetDisplayTexture(m_vram_texture.get(), ...)`) — there is no second, separate "display" texture that only some operations write into. CPU-driven VRAM writes and GL-rasterized draws land in the exact same object, so nothing can ever be stale or incomplete relative to something else.

This project instead has **two separate textures** — `vram_texture` (updated by CPU uploads via `glTexSubImage2D`, sampled by the shader for textured draws) and `display_texture`/`display_fbo` (the actual GL rasterization target, only occasionally also touched by CPU uploads via the rarely-used `update_display=true` path). That split is the real, structural root of Gap B, not something a readback bridge between the two can safely paper over — a bridge is always going to either (a) miss real GL-rasterized content if it copies the wrong direction, or (b) stomp real CPU-uploaded content if it copies wholesale in the other direction, as this attempt did.

**Refined after checking DuckStation's actual VRAM texture *format*** (`gpu_hw.cpp:48`, `static constexpr GPUTextureFormat VRAM_RT_FORMAT = GPUTextureFormat::RGBA8;`) — this changes which direction the unification should go. DuckStation's single unified texture is **RGBA8**, not a packed 16-bit format matching real VRAM bit-for-bit. That's the piece that makes unification actually work cleanly: RGBA8 is a completely natural target for GL rendering (blending/semi-transparency work on it with zero friction), whereas this project's `vram_texture` is **R16UI** (an integer format chosen to match real PSX VRAM's 5:5:5:1 layout bit-for-bit) — trying to make *that* format double as a render target would hit real difficulty, since GL blend ops don't work naturally on integer/packed formats. DuckStation sidesteps the whole problem by making RGBA8 the primary/master representation everywhere (CPU uploads, GL rendering, texture sampling, display all read/write the same RGBA8 texture), and only converts to real PSX 16-bit format at the narrow boundary where something specifically needs it (CPU-side VRAM reads for GP0(0xC0), save-states) — not as the everyday internal representation.

**Recommended real fix for next attempt** (revised): unify by *promoting* `display_texture` (already RGBA8, already GL-render-friendly) to be the single source of truth, rather than trying to make the packed `vram_texture` double as a render target:
- Redirect CPU-side VRAM uploads (`renderer_record_vram_update`'s `glTexSubImage2D` call, currently targeting `vram_texture`) to also/instead write into `display_texture`, converting the CPU's RGB555 `vram.data` to RGBA8 on upload (the inverse of the RGB8→RGB555 conversion this session's `renderer_apply_vram_readback` already implements — that conversion logic isn't wasted, it's needed for the read direction, see below).
- Redirect the shader's texture-sampling uniform (currently `vram_texture`, an R16UI sampler) to sample `display_texture` (RGBA8) instead — this requires reworking the CLUT/texture-page sampling logic in the fragment shader, since it currently does bit-level unpacking assuming a raw 16-bit source (`gpu_commands.c`/shader source in `renderer.c`) and would need to instead re-pack/interpret RGBA8 values consistently with however the upload direction encodes them.
- `renderer_draw_gl` already renders into `display_fbo`/`display_texture` — no change needed there, that becomes the permanent, only render target.
- Keep (don't discard) this session's `renderer_apply_vram_readback`'s RGB8↔RGB555 conversion math — repurpose it for the **narrow, correct use case**: CPU-side reads of `gpu->vram.data` (GP0(0xC0) VRAM→CPU, save-state serialization) now need to pull from the unified `display_texture` on demand for those specific operations, instead of a blanket every-frame full-buffer sync. This is a `glReadPixels`-on-demand pattern, not a per-frame push.
- Remove `vram_texture` (the R16UI one) entirely once the above is confirmed working, along with the now-dead `vram_readback_rgb` buffer and the `#if 0` block in `main.c`.
- This is a real refactor of the renderer's texture/shader architecture (touches the fragment shader's texture-sampling math, not just C-side plumbing) — budget a dedicated session, and live-test carefully: a game using render-to-texture-style effects (confirms Gap B is fixed), a game with heavy font/UI rendering (confirms no regression like this attempt's black-rectangle bug), and a CLUT/texture-page-heavy game (confirms the resampled shader logic is still bit-correct after moving off the raw R16UI source).

---

## Rendering gap-analysis addendum (2026-07-18, continued)

*(A first attempt to dispatch a dedicated research agent for this addendum hit a session/API usage limit mid-run and produced no output; the findings below were gathered directly instead, same rigor — code read first, references checked second, no speculation left in.)*

### Item 1 — Semi-transparency gated on `texture_enabled` (Critical, confirmed, one-line-scale fix)

`renderer_draw_gl` (`renderer.c:892`): `if (!b->is_lines && b->semi_trans_enabled && b->texture_enabled) { /* two-pass opaque+blend, STP-bit discard */ } else { /* single opaque draw, GL_BLEND left disabled from the top of the function */ }`. Confirmed by reading the full function and the batch-recording path (`renderer_draw`, `renderer.c:938-987`): `texture_enabled` and `semi_trans_enabled` are recorded independently per batch from `renderer->texture_enabled`/`renderer->semi_trans_enabled` — nothing else compensates. Any flat/gouraud-shaded semi-transparent primitive (GP0 0x22/0x2A mono-semi tri/quad, 0x32/0x3A shaded-semi tri/quad — `gpu_commands.c`'s `gp0_tri_mono_semi`/`gp0_quad_mono_semi`/`gp0_tri_shaded_impl(...,true)`/`gp0_quad_shaded_impl(...,true)`, all of which correctly call `renderer_set_semi_trans_mode(&gpu->renderer, true, mode)`) falls into the `else` branch and renders **fully opaque**, unconditionally. Matches the live-tested user bug report (`Screenshot 2026-07-18 134701.png`): a dialogue/briefing text box whose background should be translucent renders solid instead.

**Why the fix isn't simply "drop `&& b->texture_enabled` from the condition"**: the existing two-pass technique exists to emulate a *per-texel* STP bit (`raw_color & 0x8000`, checked in the fragment shader at `renderer.c:211-212/224-225/233-234`) — meaningful only because a single textured quad can sample texels that individually do or don't have their high bit set. Flat/gouraud-shaded primitives have no texel fetch at all (`use_texture==0` skips straight to `final_color = vec4(color, 1.0)`, `renderer.c:182`, with **zero `u_stp_mode` discard logic** in that path) — real hardware's semi-transparency for these is uniform across the whole primitive (selected by which opcode was issued, not a per-pixel mask), so the correct fix is a **separate, simpler single-pass branch**, not reuse of the two-pass discard machinery:
```c
} else if (!b->is_lines && b->semi_trans_enabled /* && !b->texture_enabled, implied */) {
    glEnable(GL_BLEND);
    switch (b->semi_trans_mode) { /* same 4 cases already at renderer.c:899-918 */ }
    glDrawArrays(prim, 0, vc);   // single pass, no STP discard needed
}
```
No shader changes required — `u_stp_mode` simply stays at its default `-1` (off) for this path, which the shader already treats as "no discard" for the textured branch and is irrelevant to the untextured branch entirely.

**Priority**: Critical/High — one confirmed, live-tested, user-visible correctness bug with a narrow, low-risk fix (a new `else if` block reusing the existing blend-mode switch verbatim).

**✅ Fixed (2026-07-18)**: implemented exactly as recommended above — new `else if (!b->is_lines && b->semi_trans_enabled)` branch added to `renderer_draw_gl` (`renderer.c`), single-pass blend, no shader changes. Builds clean, no regressions in a smoke-test boot. **Not yet visually confirmed** against the actual dialogue-box scene (`Screenshot 2026-07-18 134701.png`) — next session should live-test that specific scene and dump a frame to verify the background now renders translucent.

### Item 2 — 24bpp direct-display mode never implemented (High, entangled with Gap B above)

`include/gpu.h:52-53,119` / `gpu.c:172,296` track `display_depth`/D24Bits from GP1(08h), but `renderer.c` has zero references to it (`grep -n "24\|D24\|display_depth" renderer.c` matches only an unrelated dithering-comment "24-to-15bit"). The display path (`debug_ui.cpp:959`, `renderer_get_display_texture`) always shows `display_texture` as plain RGB8 — no 24bpp unpack exists anywhere.

This is **entangled with Gap B, not independent of it**: `display_texture` is populated only by this project's own GL-rasterized draws (`renderer_draw_gl`); MDEC-decoded/CPU-uploaded 24bpp frame data (per `DOCS/macroblockdecodermdec.md`, MDEC video conventionally reaches the screen via GP0(0xA0)/DMA-ch2 CPU-to-VRAM transfer, i.e. raw image upload, never a rasterized primitive) lands in `vram_texture` (the R16UI CPU-upload target), which per Gap B is never reflected into `display_texture`. So even a correct 24bpp unpack routine, if added naively to the current `display_texture`-based present path, would have nothing real to unpack — the frame data it needs isn't there yet.

**Recommended fix approach** (sequenced *after* Gap B's unification, not before): once CPU uploads write into the unified `display_texture` (per Gap B's recommended fix above), add a `display_depth`-gated branch at presentation time: when D24Bits is set, reinterpret each row of the visible display rectangle as packed 24-bit triples (2 VRAM 16-bit words → 3 output bytes, per PSX-SPX's documented 24bpp scanline packing) instead of the normal 5:5:5 unpack — this is a presentation-time reinterpretation, not a VRAM storage-format change, matching how `DOCS/graphicsprocessingunitgpu.md:1113-1122` describes real hardware treating 24bpp as a display-controller-side reinterpretation of the same VRAM bytes, not a separate storage mode.

**Priority**: High as a blocker for correct FMV colors, but sequenced behind Gap B — don't implement standalone.

### Item 3 — "GPU still missing some sprites/geometry" (punch list item, no new finding)

Scanned `gp0_table[256]` (`gpu_commands.c:1210-1362`) fully: every polygon/line/polyline/rectangle/VRAM-copy/image-load-store opcode PSX-SPX documents has a real, non-stub handler — no gaps found in dispatch coverage. Specifically checked rectangle texture-flip (GP0(0xE1) bits 12-13, `gpu->rectangle_texture_x_flip`/`_y_flip`) since a wrong flip would look exactly like "missing/wrong geometry" — confirmed **correctly implemented** (`gpu_commands.c:195-199`, applied to UV computation, not just logged). No new candidate found beyond what §3/§4 above (Gap A mask-bit, Gap B VRAM split) already track.

**Priority**: N/A — no new gap identified. Recommend closing this punch-list line as "covered by existing Gap A/B" unless a specific reproduction (which game, which on-screen object) is captured in a future play session.

### Summary — fix-first recommendation

**Item 1 (semi-transparency)** first: confirmed, user-visible, live-tested repro, and the fix is a small isolated addition to `renderer_draw_gl` that doesn't touch the shader or any other architecture — lowest risk, highest immediate visible payoff. Item 2 (24bpp) is real but explicitly sequenced behind the already-planned Gap B refactor (a dedicated session per that section's own recommendation) — implementing it standalone now would be wasted work. Item 3 closed with no action needed.
