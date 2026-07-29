# ZoniStation One — Structural Gap Analysis & Roadmap

**Opened 2026-07-13. Fully rewritten 2026-07-28** against the code as it stands today, because the
per-section state had drifted badly out of date (SPU was listed as gap-free; MDEC, the boot logo and
the FMV display path were listed as open blockers after they had been fixed).

**How to read this file**: every section states what the subsystem *does today*, then what is
genuinely missing. Sections keep their original numbers so older notes and commit messages that cite
"§8" or "§11" still resolve. Resolved investigations are compressed to a sentence and a commit hash —
the full narrative for any of them is in `CHANGELOG.md` and in `git log`.

**Scope note**: this is a correctness/architecture document. It does not track performance work, and
it does not list "features other emulators have" as gaps unless they affect this emulator's own
correctness or its ability to run software.

---

## Status at a glance (2026-07-28)

| Subsystem | State | Open work |
|---|---|---|
| CPU / interpreter | Working | Memory-timing cost model parked; single-slot load delay |
| Bus / interconnect | Working | — |
| Interrupt controller | Working | — |
| DMA | Working | MDEC ch0/ch1 drop a kick that arrives mid-slice |
| Timers | Working | Timer0's hblank gate needs a per-scanline CRTC |
| Event scheduler | Working | — |
| SIO / pads / memory cards | Working (digital pad, 2 cards) | No analog pad, no multitap |
| GPU / renderer | Working | Mask-bit *test*, GP0(C0)/(80) readback, coarse CRTC, no FIFO timing, GPUSTAT h-res bit order |
| GTE | Working | — |
| CDROM | Working | Async pacing is coarse |
| SPU / audio | Working | Emulated-clock generation landed 2026-07-28; output quality not yet surveyed |
| MDEC | Working | — |
| PCDRV | Working | — |
| Savestates | **Absent** | Whole feature |

Live status: the BIOS boots to its menu; `Ace Combat 2 (Europe)` boots, plays its FMV intro
correctly, and reaches its textured main menu and in-engine 3D view. Audio generation is paced by the
emulated clock, and the output stream is clean after the 2026-07-29 fixes; how it sounds across a
range of games has not been surveyed.

**Speed**: real time is kept with the debug interface closed. With the log windows and inspector
panels open under WSL it is not, and audio then underruns. A "85-95% of real time" figure recorded
earlier was measured with stderr logging and per-vblank Lua probes running and has been withdrawn —
it described the instrumentation, not the emulator.

---

## 1. CPU / interpreter core

**Files**: `cpu_instructions.c` (978L), `cpu_bios.c` (501L), `cpu_execution.c` (270L),
`cpu_disasm.c` (224L), `cpu_icache.c` (120L), `cpu_decode.c` (94L), `cpu_exceptions.c` (87L),
`cpu_init.c` (66L), `cpu_registers.c` (44L).

**State**: interpreter with function-pointer dispatch (`s_op_table[64]` + `s_special_table[64]`).
Register file is double-buffered per instruction (`regs` in, `out_regs` out) and committed at the end
of the step, which is what makes the load-delay slot and the branch-delay slot fall out naturally
rather than needing special cases. Branch delay is explicit (`branch_taken`/`next_pc`), and the
delay-slot flag feeds Cause/EPC on exceptions. Exceptions go through one entry point that pushes the
SR mode stack and picks the vector from BEV. Interrupts are checked every instruction.

`downcount` drives the event scheduler; MULT/DIV set a completion tick that MFHI/MFLO stall against,
and GTE ops do the same through `gte_completion_tick`. I-cache is direct-mapped, 256 lines × 4 words,
per-word valid bits, KSEG1 bypassing, invalidation on isolated-cache stores.

BIOS execution is LLE end to end: the A0/B0/C0 hooks in `cpu_bios.c` only observe (TTY capture,
call tracing) and never fake a return value.

**Open**:
1. **Memory-timing cost model is shipped disabled** — per-instruction BIOS-ROM/RAM access costs were
   implemented and then parked after they caused a CD-command hang that was never root-caused. Until
   it is re-enabled, instruction timing is uniform and slightly optimistic.
2. **Single-slot GPR load delay** — one pending load is tracked. Back-to-back loads targeting the
   same register in consecutive instructions is the case that can differ from hardware. No live bug
   is currently attributed to it.
3. No JIT and no PGXP. Both are deliberate: the interpreter is the reference implementation for this
   project, and the renderer does not upscale internal resolution, so PGXP would have nothing to
   improve.

**Priority**: Low. Nothing here blocks software.

---

## 2. Bus / interconnect / memory dispatch

**Files**: `bus.c` (1040L), `interconnect.c` (108L), `system.c` (60L).

**State**: hardware register space `0x1F801000-0x1F801FFF` is routed through a 256-entry
read/write dispatch table indexed by `(phys >> 4) & 0xFF`, so adding a device is a table entry and a
handler, not another `if` in a chain. RAM, scratchpad, BIOS ROM, EXP1/2, MEMCTRL and the cache-control
register all have real backing storage. Sub-word (byte/halfword) accesses to the DMA register block
merge into the containing word instead of being dropped, with DICR's write-1-to-clear lane excluded
from the merge — that one mattered in practice, since the BIOS shell enables DMA-completion
interrupts with a byte write to `DICR+2` (`7172960`).

`system.c` owns "run one frame": it runs the machine until the VBlank event marks the frame boundary.
`main.c` is a host shell (SDL, GL context, audio device, threads, frame cap).

**Open**: none tracked.

---

## 3. Interrupt controller

**Files**: `bus_irq.c` (55L) plus the I_STAT/I_MASK fields on `Interconnect`.

**State**: edge-triggered. `interconnect_set_irq_line(inter, IRQ_x, state)` latches I_STAT on a
low→high transition and keeps a per-source line state so a level that is already high cannot latch
twice. Every documented source is wired: VBlank, GPU, CDROM, DMA, the three timers, SPU, and
PAD/memory-card.

Two bugs found here are worth remembering because both produced "the machine is alive but one
subsystem is deaf" symptoms: the GPU line had no raise site at all until `18e0732`, and the DMA line
could be left logically high forever by a DICR-only acknowledge, swallowing every later completion
(`7923c52`).

**Open**: none tracked.

---

## 4. DMA

**Files**: `dma.c` (258L), plus the transfer implementations reached from `bus.c`.

**State**: all 7 channels with full register I/O. Linked-list transfers (GPU ch2) are sliced across
scheduled events, since the node chain is built before the kick and is not rewritten underneath us.
Block/`REQUEST` transfers read their source at kick time — deferring the read is wrong whenever the
guest refills its staging buffer immediately after kicking, which is exactly what the FMV player does
(`7bf783e`). Completion interrupts flow through one function, `dma_update_irq()`, which recomputes
DICR's master flag and acts only on the transition; DICR writes call it too, so a game that enables
interrupts after writing CHCR still gets its completion.

A kick that arrives while a slice is in flight is drained first on the GPU channel rather than being
dropped.

**Open**:
1. **MDEC ch0/ch1 still drop a kick that arrives mid-slice.** They gate on each other's FIFO
   readiness, so draining one synchronously deadlocks (tried, reverted — it hangs at the first FMV
   frame). The fix belongs in the MDEC path itself: queue the kick and run it when the FIFO frees up.

**Priority**: Medium — no current symptom, but it is a known-lossy path.

---

## 5. Timers

**Files**: `timers.c` (462L).

**State**: timers are scheduled events with a **derived counter** — the value is computed on read as
`(cpu_cycle_counter - cycle_start) / rate`, so there is no per-tick loop and no fractional
accumulator, and a game polling a counter always sees it advance. IRQ and reset fire from the
scheduled event through one shared path; register writes, reads and gate changes catch the timer up
on demand. All four sync modes genuinely gate, reset or pause. Clock sources (sysclk, dotclock,
hblank, div8) derive their rates from the GPU's active video mode rather than fixed NTSC constants.

**Open**:
1. **Timer0's gate (hblank) is not wired** — the gating logic is generic and ready, but the CRTC
   model only ticks once per VBlank, so there is no per-scanline hblank signal to feed it. Blocked on
   §8's CRTC item.

**Priority**: Medium, and it is really a GPU/CRTC task.

---

## 6. Event scheduler

**Files**: `event_scheduler.c` (232L).

**State**: single scheduling authority. Event types: VBlank, the three timers, GPU DMA, CDROM DMA,
SIO, three CDROM events (command/first response, drive tick, second response), MDEC and SPU.
Scheduling comparisons use signed deltas so they stay correct across the 32-bit cycle-counter wrap
(~127 s).

`EVQ_SPU` drives SPU sample generation in 64-sample batches (§11). `EVQ_DMA_CDROM` is intentionally
inert and should stay documented rather than "fixed": the CDROM transfer itself is synchronous, and the
event exists only as a completion marker. `EVQ_MDEC` resumes sliced MDEC DMA.

**Open**: none tracked.

---

## 7. SIO / controllers / memory cards

**Files**: `sio.c` (819L), `controller.c` (74L).

**State**: byte-stepped transfer state machine (idle → transmitting → waiting for ack) with the ack
timing scheduled rather than immediate. Digital pad protocol (ID `0x41`/`0x5A`) complete. Memory
cards: both slots, selected by JOY_CTRL bit 13, read/write/GetID with sector addressing and checksum,
`.mcd` images auto-loaded and saved. Keyboard input maps to pad buttons; the button state has a
single owner.

**Open**:
1. **No analog pad** (ID `0x73`/`0x53`) and no multitap. Some games require analog; more will refuse
   to configure without it.
2. **No controller abstraction.** With one device type this is fine; a second device type is the
   point at which a small function-pointer interface should be introduced, not before.

**Priority**: Medium once a game that needs analog is in scope.

---

## 8. GPU / renderer / VRAM

**Files**: `gpu_commands.c` (1509L), `renderer.c` (1779L), `gpu.c` (527L), `gpu_helpers.c` (131L),
`vram.c` (101L). Companion deep-dive: `GPU_GAP_ANALYSIS_2026-07-15.md`.

**State**: GP0 dispatch is a 256-entry table with a real handler for every documented opcode —
polygons and quads (flat/gouraud/textured, opaque/semi-transparent, raw/blended), rectangles and
sprites including the texture flip bits, lines and poly-lines, fills, VRAM↔VRAM copies, CPU↔VRAM
transfers. GP1 covers reset, display enable, DMA direction, display start, horizontal/vertical range,
display mode and GetGPUInfo.

VRAM is **one** RGBA8 texture that is simultaneously the rasterization target, the CPU/MDEC upload
destination and the scanout source. PSX halfwords are stored 5:5:5:1 expanded to 8 bits per channel,
which round-trips exactly, and **alpha carries the PSX mask bit** — a drawn pixel writes 0 there
normally, 1 when GP0(E6).0 forces it or when a textured pixel's source texel has bit 15 set
(`124e675`). Semi-transparency blends colour only and never the alpha channel. A scanout pass
extracts the CRTC window and unpacks it for the active depth: direct fetch at 15bpp, two-texel
recombination with a per-pixel byte shift at 24bpp, so FMV frames display correctly.

PSX line N is VRAM texel row N everywhere — the rasterizer's Y flip was removed so that rendering and
uploading agree on where a scanline lives.

**Open**:
1. **Mask-bit *test* is not applied to rasterized primitives.** The mask bit is now written
   correctly, but `preserve_masked_pixels` (GP0(E6).1, "don't overwrite masked pixels") is only
   honoured on the CPU-side write paths, not in the GL pipeline. Needs a per-pixel test in the
   fragment shader against the destination's existing bit — which means either a depth/stencil
   carrier or a read-shadow of the target.
2. **GP0(0xC0) readback and GP0(0x80) VRAM→VRAM copy read `gpu.vram.data`**, which does not contain
   GL-rasterized pixels. Any game that copies or reads back something it just drew gets stale data.
   Needs a cross-thread GL readback of the unified texture.
3. **Texture sampling still reads a separate R16UI mirror** kept in sync from `gpu.vram.data`, not
   the unified texture, because GL 3.3 forbids sampling the bound render target. Folding it in needs
   a read-shadow ping-pong. Until then, a texture whose source region was drawn (not uploaded) by the
   GPU samples stale data.
4. **CRTC is coarse** — one tick per VBlank, so there is no scanline-accurate position, no hblank
   signal (see §5) and no per-line video mode change.
5. **No GPU command-timing model** — GPUSTAT's ready/busy bits are static rather than reflecting a
   FIFO depth and per-primitive cost. Games that poll for readiness see an always-ready GPU.
6. **GPUSTAT h-resolution bit order is wrong** (found 2026-07-27, not yet fixed): the register packs
   `hres1` into bits 16-17 and `hres2` into bit 18, but hardware has bit 16 = hres2 (368-pixel mode)
   and bits 17-18 = hres1. Writes decode correctly, only the readback is scrambled — so anything that
   reads GPUSTAT back to learn the current resolution is misinformed.

**Priority**: High for items 1-3 (observable rendering correctness), Medium for 4-6.

---

## 9. GTE

**Files**: `gte_ops.c` (440L), `gte.c` (154L).

**State**: all 22 opcodes implemented with the documented saturation/flag behaviour, `MVMVA` matrix
and vector selection, IR clamping, the 33-bit MAC overflow flags and the `FLAG` error summary bit.
Per-opcode cycle costs (5-44) are charged against the CPU through `gte_completion_tick`, and both a
new COP2 data-op and an MFC2/CFC2 stall until the pending op completes. `LWC2`/`SWC2` raise the
coprocessor-unusable exception when SR.CU2 is clear.

The colour FIFO copies the `RGBC` CODE byte into the pushed colour's high byte. That single byte was
what hid the 3D boot logo for several sessions: the BIOS shell's software ordering-table renderer
reads that byte back as the GP0 opcode, so every lit primitive was being discarded as opcode `0x00`
(`cff5ab7`).

**Open**: none tracked.

---

## 10. CDROM

**Files**: `cdrom_commands.c` (766L), `cdrom.c` (415L), `cdrom_disc.c` (380L), `cdrom_audio.c` (301L).

**State**: near-complete command set with first/second response separation and scheduled delays,
sector reading from CUE/BIN, mode handling including whole-sector delivery, the Request Register
(BFRD) arm/disarm semantics that real software depends on (disarm resets the read position, arm does
not), and honest region detection — the disc's real licence sector is read once at load time and
reported by GetID, with no region spoofing anywhere in the read path.

XA audio is fully decoded: ADPCM chunk decode with both 4-bit and 8-bit modes, mono/stereo, and
resampling from 37800/18900 Hz into the 44100 Hz audio FIFO.

**Open**:
1. **Async pacing is coarse** — roughly 17 completion events fire over a whole boot, with very large
   gaps, while the BIOS busy-polls its event flags tens of thousands of times in between. It works,
   but the drive's timing granularity is far simpler than real hardware's.

**Priority**: Low-Medium. Revisit if a game shows CD-timing-sensitive behaviour.

---

## 11. SPU / audio

**Files**: `spu_mixing.c` (363L), `spu.c` (374L), `spu_voice.c` (319L), `spu_adsr.c` (163L),
`spu_dma.c` (125L), `spu_irq.c` (50L).

**State**: 24 voices with ADPCM decode and Gaussian interpolation, the full ADSR state machine with
linear/exponential phases and the rate tables, the 32-register reverb (IIR filters, accumulator taps,
feedback comb, correct SPU-RAM delay-line addressing), noise with the per-voice noise mode, pitch
modulation, SPU DMA both directions, and the IRQ9 address watch. None of it is stubbed.

Sample generation is driven by the emulated clock (2026-07-28): one stereo sample per 768 CPU cycles,
produced by a scheduled `EVQ_SPU` event in 64-sample batches, plus `spu_catch_up()` at every SPU
register read and write so a write flushes everything it owes at the old register values before
mutating state. Production and register access are therefore on the same thread, which removes the
key-on/key-off latch race entirely and makes the CD-audio FIFO single-threaded end to end. Output
reaches SDL through a single-producer/single-consumer ring; a sample is still generated when that ring
is full, because the DSP state has to advance regardless — only the audible result is dropped, counted
in `dropped_samples`.

**What it replaced**, kept because the shape of the bug is instructive: two producers existed and the
correct one was dead. `spu_step()` had zero call sites, while a wall-clock thread generated whatever
the output ring had room for and slept 1 ms. Everything that advances per sample therefore advanced at
the host's rate while the guest wrote registers at the emulated rate — two clocks, drifting
permanently. Measured after the fix: 106368 samples per 81 698 760 emulated cycles against 106378
expected (−0.01%), ring occupancy steady at 192-512 of 4096, no drops after the start-up transient.

**Open**:
1. **Output quality is unsurveyed.** The clock is right and the DSP is complete, but nobody has
   listened critically across a range of titles yet. `emu.spu_stats()` and `scripts/spu_rate.lua`
   are the instruments for the pacing side; the DSP side needs ears.
2. **Start-up transient drops samples** (~2200 in the measured run, all in one burst before the audio
   device starts draining). Harmless, but a cleaner start would prime the ring or delay the first SPU
   event until the device is running.

**Priority**: Medium — verification, not repair.

---

## 12. MDEC

**Files**: `mdec.c` (526L).

**State**: working, and exercised end to end by real FMV playback. Command state machine, RLE/zigzag
decode, quantisation, two-pass IDCT, YUV→RGB for both colour and mono, all four output depths, and
the in/out FIFOs wired to DMA channels 0 and 1.

Two bugs found here are worth keeping in mind for any future decoder work: the scale/IDCT matrix must
be stored **transposed** relative to the order it arrives in (getting this wrong turns every DC-only
macroblock into a fading blob, which rendered FMV as a grid of blobs), and the DC coefficient path
must not apply the quantisation scale.

**Open**: none tracked. The channel-kick loss described in §4 belongs to this pair of DMA channels
and is the one remaining rough edge.

---

## 13. PCDRV

**Files**: `pcdrv.c` (194L).

**State**: host-filesystem side channel for homebrew (open/read/write/seek/close). Off the critical
path for disc games.

**Open**: none tracked.

---

## 14. Savestates

**State**: **absent**. No serialisation code exists anywhere in the tree.

This is worth doing sooner than its "nice to have" reputation suggests, because it is also test
infrastructure: a state saved just before a failing moment turns a five-minute boot-and-reproduce
cycle into a one-second one, and makes regressions comparable run to run.

**Design constraints when it is written**: every subsystem's state is already in plain structs with no
heap ownership, which makes a straight struct-dump feasible; the two things that need care are the
scheduler (absolute cycle values must be rebased on load) and the renderer (VRAM lives in a GL texture
on another thread, so a save has to read it back and a load has to re-upload it).

**Priority**: High — second after §11.

---

## Roadmap

### Done

| Item | Landed |
|---|---|
| Dead-code cleanup, duplicate interrupt controller removed | 2026-07-13 |
| Single event-scheduler authority (CDROM's private timer array migrated) | 2026-07-13 |
| SIO button state single-owner; memory card slot 2 | 2026-07-13 |
| CDROM sector-buffer arm/disarm semantics; honest disc region; spoof hack removed | 2026-07-14/15 |
| Timer sync-mode gating; GTE cycle costs, CU2 checks, dead load-delay buffers removed | 2026-07-15 |
| Stack-overflow crash fix (multi-MB locals in `main`) | 2026-07-16 |
| GTE colour-FIFO CODE byte → 3D boot logo renders (`cff5ab7`) | 2026-07-23 |
| DMA sub-word register writes (`7172960`); GPU IRQ line wired (`18e0732`) | 2026-07-23 |
| Unified VRAM texture; 24bpp scanout; Y-flip removal (`08de00b`, `86b4564`) | 2026-07-24/25 |
| Timing unified: derived-counter timers, `system.c` frame driver (`df37550`) | 2026-07-25 |
| MDEC scale-matrix transpose; DMA IRQ3 line (`7923c52`); block transfers read at kick (`7bf783e`) | 2026-07-26 |
| FMV reaches the display: stale `is_viewer` flag (`43bbb0e`); real mask bit from the rasterizer (`124e675`) | 2026-07-27 |
| SPU sample generation moved onto the emulated clock; wall-clock audio thread removed | 2026-07-28 |

### Open, in the order they should be picked up

1. **Savestates** (§14) — High. Also unlocks reproducible testing.
2. **GPU: mask-bit test, GP0(C0)/(80) readback, texture read-shadow** (§8.1-8.3) — High.
   All three are the same underlying job: give the GL side a way to read the unified texture back.
3. **Per-scanline CRTC** (§8.4) — Medium. Unblocks Timer0's hblank gate (§5) and is a prerequisite
   for any GPU timing model.
4. **Audio quality survey** (§11.1) — Medium.
5. **GPUSTAT h-resolution bit order** (§8.6) — Medium, small and self-contained.
6. **Analog pad** (§7.1) — Medium, once a game needs it.
7. **MDEC channel kick queueing** (§4.1) — Medium.
8. **CPU memory-timing cost model** (§1.1) — Low; re-enable and root-cause the CD hang it caused.
9. **CDROM async pacing granularity** (§10.1) — Low.

---

## Method notes

These are the working practices that produced the fixes above, kept because they repeatedly turned
week-long symptom chases into single-session root causes.

- **Measure the mechanism, not the symptom.** The FMV was fixed by instrumenting the *game's own*
  descriptor ring and the renderer's per-rect upload path, not by more decoder archaeology.
- **One decisive measurement beats three plausible theories.** "Are the stripes already in the GL
  texture?" split the search space in one step and killed a whole branch of hypotheses (CRTC/PAL).
- **Prove the negative too.** Showing that the game issues *no* fill during playback, and that those
  VRAM rows never change, is what turned "something isn't drawing" into "something is reading it
  wrong".
- **Diagnostics are disposable.** Temporary GPU-thread readbacks and counters are added, used once
  and removed. Probes that stay are the ones with a permanent home: the in-app Lua console
  (`emu.vram16`, `emu.display_area`, `emu.draw_area`, `emu.gpu_pool`, the `gp0_*` / `vblank` /
  `mdec_macroblock` notifications) and the scripts under `scripts/`.
- **Read the hardware documentation in `DOCS/` before deciding what "correct" means**, and when a
  behaviour is ambiguous, check how an established emulator resolves it before writing code — but
  port understanding, not paragraphs.

---

## References

Hardware behaviour is taken from the documentation in `DOCS/` (PSX-SPX and related notes) and from
Lionel Flandrin's PlayStation emulation guide. Where a hardware detail was ambiguous, the
DuckStation and PCSX-Redux sources were consulted as a second opinion, and the MDEC decoder's
transform stage was ported from DuckStation's implementation with attribution in the file header.
Everything else here is this project's own implementation.
