# GPU — Deep-Dive Gap Analysis

**Opened 2026-07-15. Fully rewritten 2026-07-28**, because the two headline gaps this file was built
around (rasterized output never reaching CPU-visible VRAM, and no 24bpp display mode) were resolved by
the VRAM unification and the fixes that followed, and the remaining items needed re-deriving from the
code as it stands.

Companion to `GAP_ANALYSIS_REFACTOR_2026-07-13.md` §8, which carries the one-line summary; this file
carries the detail. Files: `gpu_commands.c` (1509L), `renderer.c` (1779L), `gpu.c` (527L),
`gpu_helpers.c` (131L), `vram.c` (101L).

---

## 1. GP0 / GP1 command coverage

Dispatch is a 256-entry table of `{word_count, handler}`, so an unimplemented opcode shows up as a
table hole rather than hiding in a switch default.

Implemented and exercised: mono/gouraud/textured triangles and quads in every opaque/semi-transparent
× raw/blended combination; rectangles and sprites in all size variants including the GP0(E1) texture
flip bits; lines and poly-lines with the `0x50005000` terminator; fill rectangle with the documented
coordinate masking (X aligned to 16, size rounded up, no draw offset, no drawing-area clip, mask bits
ignored); VRAM→VRAM copy; CPU→VRAM and VRAM→CPU transfers; draw mode, texture window, drawing area,
drawing offset and mask setting; cache-clear and interrupt-request.

GP1: reset, command-buffer reset, display enable, DMA direction, display start, horizontal and
vertical display range, display mode, GetGPUInfo with its documented sub-functions.

**Gap**: none in coverage. The table was audited entry by entry; every opcode the hardware
documentation lists has a real handler.

---

## 2. GPUSTAT

Most fields are live: texture page, semi-transparency mode, texture depth, dithering, draw-to-display,
mask set/preserve, interlace field, display disable, video mode, display depth, vertical resolution,
DMA direction, IRQ flag.

**Gap 2.1 (Medium) — horizontal-resolution bit order is wrong** (confirmed 2026-07-27, unfixed). The
status word packs the two-bit `hres1` field into bits 16-17 and `hres2` into bit 18. Hardware has bit
16 = `hres2` (the 368-pixel mode flag) and bits 17-18 = `hres1` (256/320/512/640). GP1(08) *writes*
decode correctly — only the readback is scrambled, so software that reads GPUSTAT to discover the
current resolution gets a wrong answer. Small, isolated fix.

**Gap 2.2 (Medium) — ready/busy bits are static.** Bits 26-28 (ready to receive command / send VRAM /
receive DMA block) reflect no real FIFO state, because there is no command-timing model (§5). Software
polling for readiness always sees "ready".

---

## 3. Renderer and VRAM architecture

### 3.1 Current design

One RGBA8 texture, `Renderer.vram_tex`, is simultaneously:

- the colour attachment of the FBO all rasterization renders into,
- the destination of every CPU/MDEC upload, fill and VRAM→VRAM copy,
- the source the scanout pass reads.

PSX halfwords are stored 5:5:5:1 expanded to 8 bits per channel (`(v<<3)|(v>>2)`), exactly invertible,
so CLUT index bits survive a round trip. **Alpha is the PSX mask bit (bit 15), not opacity.** Scanout
renders the CRTC window into `scanout_texture` with a fullscreen triangle, unpacking per depth: direct
fetch at 15bpp, two-texel recombination with a per-pixel byte shift at 24bpp. The display panel shows
that texture 1:1.

Rendering runs on a dedicated GPU thread. The CPU thread records draw batches, VRAM updates and their
submission order into a double-buffered command list; the GPU thread replays them in order, so a
texture page uploaded, drawn from, then re-uploaded inside one frame behaves correctly.

### 3.2 Invariants that must not be broken

Each of these cost a debugging session:

1. **PSX line N is VRAM texel row N.** The rasterizer's Y flip was removed (vertex shader and
   scissor), because rendering flipped while uploading unflipped makes raster and uploads disagree
   about where a scanline lives.
2. **Never `glTexSubImage2D` into `vram_tex` while `display_fbo` is bound** — it is that FBO's colour
   attachment, so the write is undefined and drivers silently drop it. Bind FBO 0 around the upload.
3. **The every-frame full-VRAM sync must not touch `vram_tex`.** It exists only to refresh the R16UI
   sampling mirror from `gpu.vram.data`, which holds no rasterized pixels; blitting it into the
   unified texture erases the frame's drawing.
4. **The 24bpp unpack must restore bit 15 from alpha.** At 24bpp it is a data bit of the packed byte
   stream, not a mask flag.
5. **Alpha is the mask bit, everywhere.** The fragment shader emits 0 by default, 1 when GP0(E6).0
   forces it or when a textured pixel's source texel has bit 15 set; semi-transparency uses separate
   blend functions so alpha is never blended. A constant `alpha = 1.0` is invisible at 15bpp and turns
   black into green at 24bpp.
6. **Every field of a per-frame command record must be written when recorded.** Those arrays are
   static and reused; a field left unset inherits the previous occupant's value. A stale `is_viewer`
   flag silently routed upload rects into the debug viewer texture and produced a fixed pattern of
   stale columns during FMV playback.

### 3.3 Open gaps

**Gap 3.1 (High) — the mask-bit *test* is not applied to rasterized primitives.** Writing the mask bit
is correct now; reading it is not. GP0(E6).1 (`preserve_masked_pixels`, "do not overwrite a pixel
whose mask bit is set") is honoured only on the CPU-side write paths (`vram_write_masked`), never in
the GL pipeline, so drawing over a masked region overwrites it. Needs a per-pixel test against the
destination's existing bit — a depth or stencil carrier, or a read-shadow of the render target.

**Gap 3.2 (High) — GP0(0xC0) readback and GP0(0x80) copy read the CPU-side VRAM.** Both operate on
`gpu.vram.data`, which contains nothing the GL rasterizer drew, so a game that reads back or copies a
region it just rendered gets stale content. Needs a cross-thread readback of the unified texture: the
GPU thread owns the GL context, so this is a request/response through the frame queue, not a direct
call.

**Gap 3.3 (High) — texture sampling still reads a separate R16UI mirror.** Textured draws sample
`vram_texture`, a copy kept in sync from `gpu.vram.data`, because GL 3.3 forbids sampling the bound
render target. Consequence: a texture whose source region was *drawn* rather than uploaded samples
stale data, so render-to-texture effects do not work. The fix is a read-shadow ping-pong — keep a
second texture, copy the dirty rectangle into it before draws that sample it, sample the shadow.

Gaps 3.1-3.3 are three faces of one missing capability: the GL side cannot currently read the unified
texture back. Doing that work once addresses all three.

**Gap 3.4 (Low) — dithering is unverified against hardware.** It is applied in the shader after colour
computation and quantises to 5 bits per channel, enabled for the cases that should dither (gouraud,
textured-blend, lines) and excluded for the ones that should not (mono, raw texture, rectangles), but
it has never been compared pixel-for-pixel against real output.

---

## 4. CRTC and display

**State**: the display area comes from GP1(05) start address, GP1(07) line range and the GP1(08)
resolution field. Video mode selects real per-mode frame timing — 566203 CPU cycles per NTSC frame
(59.82 Hz), 680823 for PAL (49.75 Hz), derived from the GPU clock relationship instead of one
hardcoded constant, which previously ran every PAL title's whole timebase about 20% fast. Timer
dotclock and hblank rates derive from the same video mode (CRTC 53'693'175 Hz NTSC / 53'203'425 Hz
PAL, dot divider from the h-resolution field, hblank from ticks-per-line).

**Gap 4.1 (Medium) — the CRTC ticks once per VBlank.** No scanline-accurate position, so GPUSTAT's
line bit is a toggle rather than real line parity, there is no hblank signal for Timer0's gate, and a
game changing video state mid-frame sees no effect. Prerequisite for any GPU timing model.

**Gap 4.2 (Medium) — display width ignores the horizontal display range.** Visible width comes from
the GP1(08) resolution field alone; GP1(06)'s X1/X2 are stored but never used to size or offset the
window. Hardware derives the visible width from `(X2 - X1) / dot_divider`, which is how games
letterbox, centre or shift the picture horizontally. For the titles tested so far the two agree
(measured: 2560 ticks / divider 8 = 320 px), so nothing visibly breaks yet.

**Gap 4.3 (Low) — display disable is tracked but not honoured.** GP1(03) sets the flag and GPUSTAT
reports it, but scanout ignores it; hardware blanks the video output. Games blank the screen during
transitions, so this shows as content that should not be visible.

**Gap 4.4 (Low) — interlaced output is presented as a single progressive buffer.** The field flag is
tracked and toggled, but scanout does not interleave fields for 480i.

---

## 5. Command FIFO and execution timing

**State**: none. GP0 words are consumed as they arrive; there is no FIFO depth, no per-primitive cost,
no busy period. With §2.2's static ready bits, the GPU looks infinitely fast to the guest.

**Gap 5.1 (Medium)** — this is what makes software that paces itself on GPU readiness (rather than
VBlank) behave differently from hardware. It also gives the per-scanline CRTC a purpose beyond timer
gating.

---

## 6. Verified working

Recorded so future work does not re-investigate it:

- Polygons, rectangles, lines; textured primitives at 4/8/15-bit depths with CLUTs; texture-window
  masking; semi-transparency in all four modes for textured and untextured primitives.
- Drawing-area scissor and drawing offset.
- CPU→VRAM upload, VRAM→VRAM copy, fills.
- 24bpp display of MDEC-decoded FMV frames, including the double-buffer flip between the two halves of
  VRAM.
- The mask bit round-tripping through the unified texture as picture data at 24bpp.

---

## Priority summary

| Gap | Severity | Notes |
|---|---|---|
| 3.1 mask-bit test | High | Same underlying work as 3.2/3.3 |
| 3.2 GP0(C0)/(80) readback | High | Needs cross-thread GL readback |
| 3.3 texture read-shadow | High | Blocks render-to-texture effects |
| 4.1 per-scanline CRTC | Medium | Unblocks Timer0 gate and §5 |
| 4.2 horizontal display range | Medium | No current visible symptom |
| 5.1 command timing model | Medium | Depends on 4.1 |
| 2.1 GPUSTAT h-res bit order | Medium | Small, isolated |
| 2.2 static ready bits | Medium | Part of 5.1 |
| 4.3 display disable | Low | |
| 4.4 interlaced fields | Low | |
| 3.4 dither verification | Low | |

---

## Resolved (kept for context)

- **Rasterized output was invisible to VRAM, and CPU/MDEC uploads were invisible to the display** —
  the renderer kept three unsynchronised stores. Resolved by the unified-texture design in §3.1
  (2026-07-24/25).
- **24bpp display mode did not exist** — added with the same work, including the bit-15 handling in
  §3.2.4.
- **Semi-transparency was gated on `texture_enabled`**, so flat and gouraud primitives never blended
  (2026-07-20).
- **The 3D boot logo never drew** — a GTE colour-FIFO byte, not a GPU defect (2026-07-23; main
  document §9).
- **FMV columns went stale on screen** — a stale per-frame command flag, §3.2.6 (2026-07-27).
- **Black areas displayed as green at 24bpp** — the constant-alpha mask bit, §3.2.5 (2026-07-27).
