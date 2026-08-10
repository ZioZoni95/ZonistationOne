# The display path — study session, 2026-08-10

Opened because `Disney-Pixar Monsters & Co. — L'Isola dello Spavento (Italy)` hit a hard stall, and
fixing the stall left three visual defects behind that are all in the **display/scanout** half of the
GPU, not in rasterisation. Companion to `GPU_GAP_ANALYSIS_2026-07-15.md`, which covers drawing;
this file covers everything between VRAM and the screen.

Sources, in the order they were trusted: `DOCS/graphicsprocessingunitgpu.md`, then
`pcsx-redux/src/core/display.cc` (GPL-2, quotable, and its `updateDispArea()` is a line-for-line
transcription of the documented formula), then our code. DuckStation was used only as a black box —
"does the real thing show this?" — never as a source.

---

## 0. What was fixed today, with the evidence

### 0.1 A CHCR abort left the sliced transfer running (`dma.c`, `dma.h`)

The game stalled after the "Sony Computer Entertainment" screen with
`[DMA] MDEC out: addr 0x00200000 out of bounds` and the game's own TTY line
`time out in decoding !`.

Measured chain, from a probe on ch1's register writes (`pc=0x80073d94`):

```
ch1 write off=0x10 val=0x80126000     MADR
ch1 write off=0x14 val=0xd0060020     BCR  -> bs=0x20, bc=0xd006 = 1704128 words
ch1 write off=0x18 val=0x01000200     CHCR start, sync=REQUEST, TO_RAM
ch1 write off=0x18 val=0x00000000     CHCR cleared  <-- the abort
```

libmdec kicks DMA1 with an oversized block count and clears CHCR when the frame is out. Our sliced
channels keep their remaining word count in `Dma`, not in the channel registers, so clearing CHCR
cancelled nothing: the slice kept running off the event scheduler and wrote MDEC output across RAM
from `0x126000` until it hit the end of the 2 MB at `0x200000` — about **880 KB of the game's own
code and data smeared**. Meanwhile `interconnect_perform_dma` dropped every subsequent ch1 kick as
"sliced transfer already in flight" (`bus.c:1000-1008`), so `DecDCToutSync` never completed.

`dma_cancel_slice()` now runs whenever a CHCR write leaves the channel inactive. After the fix the
FMV decodes and the game reaches its title screen and 3D engine.

**This one root cause also explains three things that looked separate:**

- the boot logo breaking up — its data was inside the smeared range;
- the screen then showing raw VRAM — the display list and the GP1 values the game had in RAM were
  smeared too (the window title flipped to `NTSC 59.82 Hz` mid-boot, which is a garbage GP1(08)
  write, not a mode change any PAL game would make);
- the **audio cut** at the same instant — compare the SPU log before and after the break in the two
  boot screenshots: `Voice Key On ... volL=0x0D0C ... adsr=8C7A/DFED` becomes
  `volL=0x0000 volR=0x0000 adsr=0000/0000`. The game's SPU parameter tables were in the smeared
  range; key-on at zero volume is silence. Nothing was wrong in the SPU.

### 0.2 GP1(03) display-off was tracked but never reached the screen (`gpu.c`, `renderer.c`)

`display_disabled` was in GPUSTAT bit 23 and read by nobody. `DOCS/graphicsprocessingunitgpu.md:647`
— *The "Off" settings displays a black picture* — so the scanout now clears to black instead of
scanning VRAM out. Measured with `scripts/display_blank_probe.lua`: the game blanks for **3 and 11
fields** around framebuffer swaps (correct, invisible), and for **138 and 189 fields** across the two
big loads. The long ones are the game's own writes, not ours — but see §2.3 before treating the black
as settled.

---

## 1. The display window: what hardware computes, and what we compute

### The documented formula

- Width in pixels is derived from **GP1(06)**, not from the resolution bits:
  `(((X2-X1)/cycles_per_pix)+2) AND NOT 3` (`DOCS:687-690`). `cycles_per_pix` comes from the
  horizontal resolution: 10/8/5/4 for 256/320/512/640, 7 for 368 (`DOCS:735-742` plus the nominal
  video clock section).
- Height in lines is `Y2-Y1` from **GP1(07)**, with no rounding (`DOCS:707-709`), doubled when
  interlace is on.
- Source origin is **GP1(05)**: X is a halfword address, bits 0-9, full 0..1023 range; Y is bits
  10-18 (`DOCS:670-676`).

`pcsx-redux/src/core/display.cc:95-130` implements exactly that: `dividers[] = {10,8,5,4,7,7}`,
range ends clamped to the scanline length (3406 PAL / 3413 NTSC), start and end floored to a multiple
of the divider, `width = ((cycles/divider)+2) & ~3`, `height = min(totalScanlines, y2-y1)` doubled on
interlace, and the GP1(05) start used **unmasked**.

### What we do — `gpu.c:36-65`, `gpu.c:228-251`

| Item | Hardware / redux | Ours | Cost |
|---|---|---|---|
| Display width | from GP1(06) and the divider | `display_width_hint`, i.e. purely the GP1(08) resolution index (`gpu.c:39`) | **High** |
| Display height | `Y2-Y1`, ×2 on interlace | `display_line_end - display_line_start`, never doubled; 480 only when `interlaced && vres==480` (`gpu.c:42-49`) | Medium |
| GP1(06)/(07) as an effect | drives width/height every write | only height reaches the screen | **High** — screen-shake effects (Chrono Cross, `DOCS:719-721`) do nothing |
| GP1(05) X | bits 0-9 | masked `0x3FE` (`gpu.c:208`), odd halfword start lost | Low |
| 368-pixel mode | bit 6 set means 368 regardless of bits 0-1 | `hr1 | (hr2<<2)`, so `hr2=1, hr1=1` falls through to 256 (`gpu.c:239-245`) | Low |
| Range clamping | ends clamped to the scanline/frame length | none | Low |

The width item is the one to fix first, and it is cheap: the formula is four lines, the inputs are
already stored (`display_horiz_start/end`, `hres_raw`), and it removes the entire class of "the
picture is the wrong size on a PAL game" defects. `DOCS:826-830` warns that most PAL games use
"random" centering values — with width taken from the resolution index instead of the range, every
one of those games is displayed at a width the game never asked for, and the columns beyond what it
drew show stale VRAM rather than a border.

---

## 2. The three remaining artifacts, and what each one still needs

### 2.1 The magenta/green strip along the top of the FMV — most likely correct hardware behaviour we fail to hide

Measured with `scripts/fmv_upload_coverage.lua`, during the Pixar logo movie:

```
vb 1175: uploads=32 hw=184320 rect=(0..768, 8..248)    window=(0..480, 256..496) d24=1
vb 1179: uploads=32 hw=184320 rect=(0..768, 264..504)  window=(0..480, 0..240)   d24=1
```

Two 24bpp buffers, 768 halfwords wide (= 512 pixels at 3 bytes each), 240 rows each, written at
**buffer row +8** — while GP1(05) points the window at buffer row +0. So the first 8 displayed rows
of every field are VRAM the movie never wrote, and read through the 24bpp unpack that is exactly the
magenta/green striping seen at the top of the frame.

`DOCS:713-716` explains why a game would do that: *Many NTSC games display 240 lines, but on most
analog television sets, only 224 lines are visible (8 lines of overscan on top and 8 lines of
overscan on bottom)*. The 8-row offset is the game putting its picture where a TV's visible area
starts. A real console feeds those rows to the TV; the TV throws them away. DuckStation's default
crop setting does the same, which is consistent with the report that it does not show the strip.

**What this needs:** an overscan crop in the scanout (a display option, not a silent hack — the full
frame stays available in the VRAM viewer). Not a bug in the upload path: coverage is complete,
`hw=184320` halfwords = 768×240 exactly, every field, with no dropped rows.

**What would falsify it:** if a run shows the same 8-row band while the game's uploads *do* start at
buffer row 0, then something else moved the window and the overscan reading is wrong.

### 2.2 The stretched licence-screen frame right after the FMV — depth outlives the content

The screenshot shows the boot licence text ("…ertainment Europe", "…tation.com") stretched
horizontally by about 1.5× and shifted. 1.5× is the signature of 15bpp content read by the 24bpp
unpack: 3 halfwords become 2 pixels. The probe caught the matching state directly —
`vb 1149: display ON after 138 fields off win=(0,0) l=35..275 d24=1` — the display comes back on,
pointed at the licence screen, with GPUSTAT.21 still set to 24bpp.

So the defect is a **latch-order problem, not a shader problem**: our snapshot of the display
parameters is taken once, at frame submit (`renderer.c:2287-2292`), and applied to the whole field.
Hardware latches per line as it scans. A game that writes GP1(08) part-way through a field gets a
field that is partly one depth and partly the other on hardware, and a whole field of the wrong depth
from us. The same single-snapshot design is why `crtc` ticking once per frame has never mattered
until now.

**What this needs:** snapshot the display state at the **start** of the field (at the VBlank event,
before the CPU runs the next field's code) rather than at submit. That is a small change and it is
strictly closer to hardware than what we do now. Per-line latching is the full fix and is a much
larger job; it belongs with the CRTC rewrite.

### 2.3 The long black screens across the two loads — not yet settled

The probe samples GPUSTAT once per field, so a game that writes GP1(03)=1 before drawing and
GP1(03)=0 after, every field, is indistinguishable from a game that turned the display off for
189 fields. If the game is toggling, hardware shows a normal picture (the display is off only for the
part of the field the game spends drawing) and we show black for the whole field — the same
end-of-field snapshot problem as §2.2, and it would explain the report that DuckStation shows no
black there.

**The one measurement that settles it:** count GP1(03) writes per field and print the pair sequence.
If the count is 2 per field the toggle theory is right and §2.2's fix removes the black as well; if
it is one write every few hundred fields, the black is the game's own and correct.

---

## 3. Display-path items already known, re-read in this context

- **CRTC advances once per frame** (`gpu.c:68-160`). Fine for a per-field scanout, wrong for anything
  that reads the scanline counter or changes registers mid-frame. §2.2 and §2.3 are both really
  "we have no notion of *when* inside the field a register changed".
- **No interlace field selection in the scanout.** GPUSTAT bit 31 and the field flag exist; the
  scanout reads a progressive window. 480i content is displayed as the top field's rows.
- **No horizontal VRAM wrap in the scanout shader** (`renderer.c:930-940`). `hx` can exceed 1023;
  hardware wraps inside the 1024-halfword line. Only reachable with a display start near the right
  edge of VRAM.
- **GPUSTAT horizontal-resolution bit order** is still wrong (`gpu.c:351`, gap 2.1 of the July
  analysis): bits 17-18 must carry hres1 and bit 16 hres2.
- **GP0(C0)/GP0(80) read the CPU-side VRAM mirror** while the scanout reads the GL texture. Any game
  that reads back what it drew, or blits VRAM→VRAM over rasterised output, sees stale data. This is
  the cross-thread readback job; it is the only item here that is genuinely large.

---

## 4. Work queue, impact per unit of effort

1. **Display width from GP1(06)** with the documented divider table. Four lines, removes a whole
   class of wrong-size PAL pictures. (§1)
2. **Latch the display state at field start, not at submit.** Small, and fixes §2.2 outright; likely
   fixes §2.3 too. (§2.2)
3. **Count GP1(03) writes per field** — one probe run, decides whether §2.3 is ours or the game's.
4. **Overscan crop option** for the scanout. (§2.1)
5. Height `Y2-Y1` with the interlace doubling, GP1(05) X unmasked, 368 decoding, range clamping —
   one small commit together. (§1)
6. GPUSTAT hres bit order. (§3)
7. Cross-thread VRAM readback so GP0(C0)/GP0(80)/VRAM→VRAM see rasterised pixels. (§3)

Items 1-5 are all in `gpu.c` and the scanout block of `renderer.c`, and none of them touch the
drawing path.
