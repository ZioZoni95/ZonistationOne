# CPU → GPU → VRAM → screen: how the path is supposed to work

**Date:** 2026-08-18
**Sources:** `psx-spx-docs/docs/graphicsprocessingunitgpu.md` (the official psx-spx clone, newer
than the `DOCS/` fork — cited by its line numbers throughout), and `pcsx-redux/src/core/display.cc`
/ `gpulogger.cc` for a second implementation.
**Rule, as for every audit here:** no verdict may say "correct" without citing both a documentation
line and a code line. Anything not compared is marked UNVERIFIED.

Written because the display defects were being chased by running the game and looking at it. The
rules below settle most of them on paper; `scripts/display_map_probe.lua` covers what is left, live,
without a DEBUG run.

---

## Part 1 — The path, stage by stage

### 1.1 CPU → GPU

Two ports, and nothing else reaches the GPU:

| Address | Write | Read |
|---|---|---|
| `1F801810h` | GP0 — drawing and VRAM transfer | GPUREAD — VRAM-to-CPU data, GP1(10h) register reads |
| `1F801814h` | GP1 — display control | GPUSTAT |

Words arrive either from the CPU directly or through DMA channel 2. GP1 is decoded as
"command in the upper 8 bits, 24-bit parameter in the lower" (`:604`-ish in the fork; the official
clone documents each GP1 individually from `:632`).

One rule with real consequences, `:573-580`:

> "Non-DMA transfers seem to be working at any time, but GPU-DMA Transfers seem to be working ONLY
> during V-Blank (outside of V-Blank, portions of the data appear to be skipped, and the following
> words arrive at wrong addresses) […] That problem appears ONLY for continuous DMA aka VRAM
> transfers (linked-list DMA aka Ordering Table works even outside V-Blank)."

So a game that uploads frames by DMA does it inside vblank, which is why FMV upload rectangles are
stable per scene and why sampling them once per vblank (as the probe does) sees every one of them.

### 1.2 GPU → VRAM

VRAM is 512 lines of 2048 bytes, addressed as coordinates, `(0,0)` upper-left (`:126-129`). The
horizontal coordinate is counted in whatever unit the data is — 4/8/16/24-bit or halfwords
(`:131-137`).

Two disjoint ways in, and the difference decides what can be displayed in 24bpp:

- **Rendering commands** GP0(20h..7Fh). Clipped to the drawing area, offset by the drawing offset,
  affected by the mask bits. **Always write 15-bit pixels** — "The Display Area Color Depth bit does
  NOT affect GP0 draw commands, which always draw in 15 bit" (`:784-785`).
- **Transfer commands** GP0(A0h) CPU→VRAM, GP0(C0h) VRAM→CPU, GP0(80h) VRAM→VRAM. "The coordinates
  for the above VRAM transfer commands are **absolute** framebuffer addresses (not relative to Draw
  Offset, and not clipped to Draw Area)" (`:573-575`). These are the only way 24-bit pixels get into
  VRAM: "The 24bit display mode works only with VRAM transfer commands like GP0(A0h); the rendering
  commands GP0(20h..7Fh) cannot output 24bit data" (`:1170-1172`).

Both wrap rather than clamp: "If the Source/Dest starting points plus the width/height value exceed
the addressable VRAM size, then the Copy/Fill operations wrap to the opposite memory edge (without
any carry-out from X to Y, nor from Y to X)" (`:583-587`).

### 1.3 VRAM → screen

Four registers, and they do not overlap in meaning:

| Register | Sets | Units |
|---|---|---|
| GP1(05h) | **where** in VRAM the picture starts | X: halfword address 0-1023; Y: scanline 0-511 (`:695-698`) |
| GP1(06h) | **how wide**, and where on the line | video clock units relative to HSYNC (`:712-714`) |
| GP1(07h) | **how tall**, and where on the frame | scanline numbers relative to VSYNC (`:751-754`) |
| GP1(08h) | resolution, video mode, depth, interlace | (`:771-778`) |

The size is stated once, flatly, and it has no interlace term (`:703-704`):

> "The size and target position on screen is set via Display Range registers; target=X1,Y2;
> size=((X2-X1)/cycles_per_pix), (Y2-Y1)."

Width is rounded, height is not (`:718-719`, `:756-758`):

> "The number of displayed pixels per line is `(((X2-X1)/cycles_per_pix)+2) AND NOT 3`"
> "The number of lines is Y2-Y1 (unlike as for the width, there's no rounding applied to the height)."

`cycles_per_pix` comes from the horizontal resolution, and the dividers are given by the dotclocks
(`:1361-1367`): 256→10, 320→8, 368→7, 512→5, 640→4.

The official clone adds a table of the X1/X2 pairs official games actually use (`:733-746`) — useful
as a sanity check on a measured window:

```
  Width    X1   X2    Range        Width    X1   X2    Range
  NTSC 256 590  3150  2560         PAL 256  610  3170  2560
  NTSC 320 600  3160  2560         PAL 320  624  3184  2560
  NTSC 368 539  3227  2688         PAL 368  560  3248  2688
  NTSC 512 615  3175  2560         PAL 512  635  3195  2560
  NTSC 640 620  3180  2560         PAL 640  640  3200  2560
```

Note every non-368 range is 2560 clocks: 2560/8 = 320, +2, AND NOT 3 → 320. The formula reproduces
the table, which is the check that it was implemented right.

### 1.4 Interlace and 480-lines mode — the one that was got wrong

Three statements, and they only fit together one way:

1. `GP1(08h).2` is "Vertical Resolution (0=240, 1=480, **when Bit5=1**)" (`:772`). The 480 setting
   is conditional on interlace; interlace is not conditional on it.
2. "Interlace must be enabled to see all lines in 480-lines mode" (`:781-782`).
3. `GPUSTAT.31` is "Drawing even/odd lines in interlace mode", and — decisively — "**In 480-lines
   mode, bit31 changes per frame. And in 240-lines mode, the bit changes per scanline**"
   (`:917-920`).

Per *frame* means the two fields take different halves of the buffer: the VRAM rectangle is
`2 × (Y2-Y1)` tall. Per *scanline* means the bit is just line parity and both fields walk the same
`(Y2-Y1)` lines: the rectangle is `(Y2-Y1)` tall.

**So the height doubles when interlace AND vres=480 — not on interlace alone.** A game running 240
lines with interlace on (a common configuration, and what the flicker note at `:781-783` is
complaining about) has a `(Y2-Y1)`-line framebuffer like any progressive title.

The other half of statement 2 — "the Vertical Interlace flag DOES affect GP0 draw commands"
(`:785-786`) — is about *drawing* being masked to even or odd lines, not about the display size.
Unmodelled here; see Part 4.

### 1.5 24bpp display

Pixels are 3 bytes, "so each 6 bytes contain two 24bit pixels" (`:1167-1169`). A 24bpp line of *W*
pixels therefore occupies `W * 3 / 2` halfwords of VRAM, while GP1(06h) still yields *W* — the width
formula is in pixels and knows nothing about depth. Anything that treats the display width as a
halfword count in 24bpp reads 2/3 of the picture and lands the rest wrong.

### 1.6 Blank

`GP1(03h).0` → `GPUSTAT.23`. "The 'Off' settings displays a black picture (and still sends /SYNC
signals to the television set)" (`:671-672`). Black, not the last frame, and not the display window.

### 1.7 Mis-centered PAL — why a PAL window cannot be trusted to be sane

The official clone carries a section the older fork does not (`:867-882`). Condensed: NTSC games
centre at X1=260h, Y1/Y2=88h±N; PAL games *should* use A3h±N but "most PAL games are using
completely different 'random' centering values […] Many PAL games are even using different centerings
for their Intro, Movie, and actual Game sequences", and "In result, most PAL games are looking like
crap when playing them on a real PSX."

Two things follow. A PAL window that changes between the intro, the FMV and gameplay is **normal**,
not a bug to chase. And the doc's own recommendation — "For PSX emulators it may be recommended to
ignore the GP1(06h)/GP1(07h) centering, and instead, apply auto-centering to PAL games" — is a
*presentation* choice on top of a correct emulation, never a substitute for one.

### 1.8 Overscan

`:762-765`: "Many NTSC games display 240 lines, but on most analog television sets, only 224 lines
are visible (8 lines of overscan on top and 8 lines of overscan on bottom). **Many PAL games display
only 256 lines (underscan with black borders).**"

Overscan is an NTSC property. A PAL field is already underscanned — its borders are in the 256 lines
the game drew — so cropping it removes picture the TV would have shown.

---

## Part 2 — What pcsx-redux does

`updateDispArea()` (`pcsx-redux/src/core/display.cc:95-129`), for the same four registers:

```
divider   = dividers[hres]                    // {10, 8, 5, 4, 7, 7}
horRange* = (range / divider) * divider       // start and end snapped down
height    = min(totalScanlines, y2 - y1)
if (interlace) height *= 2                    // :111-113
width     = ((horizontalCycles / divider) + 2) & ~3
```

Three notes:

- **The height doubling is on `interlace` alone** (`display.cc:111-113`), with
  `interlace = (value >> 5) & 1` and nothing else (`gpulogger.cc:258`). This disagrees with
  `:772`/`:919-920`. The doc wins; redux is not evidence here, and this is exactly the kind of
  agreement-by-copying that the "cite both" rule exists to catch.
- Redux snaps `X1` and `X2` down to a multiple of the divider **before** subtracting. The doc gives
  the formula on the raw difference (`:718-719`). The two differ by up to one pixel before the
  `AND NOT 3`; nothing observed depends on it, so: UNVERIFIED.
- Redux decodes `hres` with bit 6 set as `{368, 384, 512, 640}` by bits 0-1 (`gpulogger.cc:237-252`),
  where the doc says bit 6 means 368 outright (`:776`). A 384-wide mode is not in the doc's
  resolution list (`:1144-1150`). Not resolved here: UNVERIFIED.

---

## Part 3 — What we do

| Rule | Doc | Code | Verdict |
|---|---|---|---|
| Width `(((X2-X1)/cyc)+2) & ~3` | `:718-719` | `gpu.c:97` | **correct** |
| Divider 10/8/5/4, 7 for 368 | `:1361-1367` | `gpu.c:gpu_cycles_per_pixel` | **correct** |
| X2 clamped to the scanline | `:712-714` | `gpu.c:95` | **correct** |
| Height `Y2-Y1`, no rounding | `:756-758` | `gpu.c:124` | **correct** |
| Height doubled only in 480-lines mode | `:772`, `:919-920` | `gpu.c:129`, and the hint at `:374` | **fixed 2026-08-18** — was `if (interlaced)`, copied from redux |
| Field tag alternates only in 480-lines mode | `:919-920` | `gpu.c:263-267` | **correct** |
| 368 from bit 6 regardless of bits 0-1 | `:776` | `gpu.c:364-372` | **correct** |
| GP1(05h) X is a halfword address 0-1023 | `:695-696` | `gpu.c` GP1(05) mask `0x3FF` | **correct** |
| GP1(05h) Y is 0-511 on 1 MB VRAM | `:697-698` | mask `0x1FF` | **correct** for retail; the 10-bit v2 form (`:699-707`) is not implemented — UNVERIFIED, no retail console has it |
| GP1(03h) shows a black picture | `:671-672` | `renderer.c:2091-2095` | **correct** — the scanout clears instead of sampling VRAM |
| Transfer coords are absolute, unclipped | `:573-575` | `gpu_commands.c:gp0_image_load` | **correct** |
| Overscan is NTSC; PAL is underscan | `:762-765` | `gpu.c:gpu_overscan_crop` | **fixed 2026-08-18** — the crop was applied to PAL too and ate the bottom lines |
| Display state applies for the whole field | — | `renderer.c:renderer_submit_frame` | **fixed 2026-08-18** — a field-start latch was added on top of a submit that already happens at the field boundary, delaying the picture one field; see the comment there |
| 24bpp width is in pixels, VRAM cost is 3/2 | `:1167-1169` | scanout shader, `renderer.c:2102` | UNVERIFIED — the shader takes `disp_w` and a depth flag; the halfword arithmetic inside it has not been read against the doc |
| Drawing masked to even/odd lines in interlace | `:785-786`, `:917-920` | — | **absent** |
| GPU-DMA VRAM transfers only inside vblank | `:573-580` | `bus.c` DMA2 | **absent** — we transfer whenever asked. Games do it in vblank anyway, so nothing is known to depend on it |
| CRTC latches per line | — | one snapshot per field | **absent by design** — see Part 4 |

---

## Part 4 — What is left, and what the probe decides

1. **The 8-line offset.** Frames land at one Y, the display window starts at another, so the top of
   the screen is stale VRAM — the strip of macroblock noise above the Pixar logo. The overscan crop
   was hiding it. Which side is wrong (our GP1(05h) Y, or the upload rectangle) is not decidable
   from the documentation: it needs the two numbers side by side, which is line 2 of the probe's
   output.
2. **Per-line CRTC latch.** A game that changes depth or window part-way through a field gets one
   wrong field from us. The whole-field latch attempted on 2026-08-17 made it worse, not better
   (Part 3). Doing it properly means running the scanout inside the field, which is the same job as
   the cross-thread GL readback — item G23 in `CLAUDE.md`.
3. **Interlaced drawing.** `:785-786` says the interlace flag affects GP0 draw commands, and
   `GPUSTAT.31` says drawing alternates even/odd lines. We draw every line every field. Only
   matters for a true 480-lines title; nothing in the current test set is one, so it stays open.
4. **24bpp scanout arithmetic.** Read the shader against `:1167-1169` before trusting any 24bpp
   geometry. A horizontal misalignment seen on a 24bpp screen is more likely here than in
   `gpu_update_display_mapping()`.
5. **Redux's two disagreements** (divider snapping, the 384 mode) are unresolved and stay
   UNVERIFIED. Neither affects anything measured so far.
