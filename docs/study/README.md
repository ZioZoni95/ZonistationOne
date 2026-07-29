# Study notes

Findings from reading the hardware documentation in `DOCS/` against this emulator's implementation,
with DuckStation and PCSX-Redux consulted as second opinions on ambiguous behaviour. These are
research documents: nothing in them has been changed in the code yet, and each claim carries the
citation it came from.

| Document | Subject | Date |
|---|---|---|
| `SPU_2026-07-29.md` | SPU timing, register semantics, DSP correctness, host audio output | 2026-07-29 |
| `CDROM_XA_2026-07-29.md` | CDROM async model, command timing, CD-XA audio path, interleave | 2026-07-29 |

---

## Combined picture

Two things came out of reading both at once that neither document says on its own.

**The audio work done on 2026-07-28 fixed the clock and left the data path half-wrong.** Sample
generation is now correctly driven by emulated time, and the XA per-sector sample count is right — but
the XA *coding-info* bits are misread (bit 2 is the sample rate, not the bit depth; bit 4 is the bit
depth, not bit 3), so any stream that is not the common 37800 Hz 4-bit case still decodes to noise. In
the same area, the SPU's reverb writes into the sample region because mBASE is truncated to 14 bits,
and it writes *even when the game has reverb disabled* because SPUCNT bit 7 is applied to the output
stage instead of to the buffer writes. Both are one-line fixes with disproportionate consequences.

**The compressed boot sequence is not an audio or CD problem.** The jingle lasting ~8 seconds where
hardware takes 10-15 was measured against both subsystems and neither accounts for it: the CD is
essentially idle during that window (115 VBlanks, 4 log lines), our read path is slower than hardware
rather than faster, and the SPU's pitch and envelope rates were verified correct. What the same
instrumented run *did* show is the BIOS printing `VSync: timeout` 1032 times in 25 emulated seconds,
beginning before the first CD command. A `VSync()` that returns early instead of waiting for a field
makes every frame-counted animation run fast, which is the symptom. **The VBlank / root-counter /
event-delivery path is the lead to pull.**

---

## Work queue from both studies

Ordered by impact per unit of effort, not by subsystem.

### Cheap fixes with large consequences — **all seven done 2026-07-29**

1. **XA coding-info masks** — 8-bit is bit 4, 18900 Hz is bit 2 (`cdrom_commands.c:634-636`).
2. **SPU mBASE truncated to 14 bits** — the reverb work area lands in the ADPCM sample region
   (`spu.c:336`).
3. **SPUCNT bit 7 inverted** — it must gate reverb *writes*, not the output mix (`spu_mixing.c:296`).
4. **18900 Hz resample ratio** — 7/3, not 7/6 (`cdrom_audio.c:199-217`). Needed as soon as 1 lands.
5. **Second-response scheduling drops queued responses** — measured 12 Inits producing 3 interrupts
   (`cdrom.c:77-82`).
6. **Volume sweep direction read from bit 7 instead of bit 13** (`spu_voice.c:284,304`), and writing a
   sweep-mode volume register destroys the current level (`spu.c:239,246`).
7. **Setmode speed change costs nothing** (0.6-0.7 s on hardware) and **Pause 1x/2x are swapped**.

All seven landed together; the audio path was re-measured afterwards (CD queue balanced at 44 688
pushed against 44 112 consumed, zero drops, boot and FMV intact). The jingle is still short, as
expected — that one belongs to item 9.

### Structural, each worth its own session

8. **Drive pacing model** — the drive must keep delivering while an interrupt is pending, sectors must
   be able to be missed, and the readable buffer must advance at INT1 delivery rather than at load.
9. **VSync / VBlank delivery** — the lead for the compressed boot sequence.
10. **Host audio when below 100% of real time** — DuckStation's answer is SoundTouch time-stretch
    driven by buffer fullness (tempo changes, pitch does not); Redux's is to pace the emulator from the
    device. We currently have neither.
11. **Capture buffers into SPU RAM** — four 1 KB regions at 0x000-0xFFF that games read for lip-sync;
    we write them to a private array with a byte/halfword indexing confusion.
12. **SPU transfer timing** — DMA4 moves whole blocks instantly, so SPUSTAT's busy and DMA-request bits
    carry no information.

### Correctness cleanups

13. Filter-rejected XA sectors delivered as data; no Mode-2 gate on the XA attempt.
14. ADPCM filter state stored before clamping.
15. CD volume matrix mis-decoded (ADPCTL's byte lands in a volume register) and never applied.
16. Mute and SPU-disable silencing CD audio, which they must not.
17. Main volume sweeps never advancing.
18. `VxPitch` masked at write time, so the documented maximum step stops the voice.
19. SPU IRQs impossible from inactive voices; the address check uses the wrong offsets.
20. CDDA not decimated at 2x; report mode firing 75 times a second instead of ~10.

---

## Things confirmed correct

Recorded so they are not re-investigated:

- SPU sample tick, flush-before-mutate, key-on/off latching, ADSR rate tables, noise generation, the
  reverb formula and its address units.
- The reverb resampling shortcut (average in, hold out) affects only aliasing and high-frequency
  response — not level, decay or duration.
- CD sector period constants, XA samples per sector, the 37800 Hz zigzag resampler, ADPCM sample
  expansion including the rounding term, BFRD arm/disarm semantics.
- The interleave arithmetic closes exactly: at 2x with a 1/8 interleave, one 37800 Hz stereo sector
  every 53.33 ms yields 2352 output frames, which is 53.33 ms at 44100 Hz.
