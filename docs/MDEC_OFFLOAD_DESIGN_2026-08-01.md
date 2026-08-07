# MDEC Decode Offload — Design Document

Date: 2026-08-01
Status: proposal — no code written.
Scope: move the MDEC macroblock decode off the emulation thread onto a worker pool, turning the idle
cores of the host into FMV decode headroom. Documentation only. Follows item 5 of
`docs/HARDWARE_UTILIZATION_ANALYSIS_2026-08-01.md`.

---

## 1. TL;DR

Today the whole MDEC pipeline — RLE, 2-pass IDCT, YUV→RGB, output packing — runs **inline on the
emulation thread** every time a DMA slice pushes a word into the MDEC input FIFO. FMV is the largest
single work spike in a session, and it lands on the exact thread that has no headroom.

The design keeps the *protocol* (command parsing, FIFO accounting, status register, DMA gates,
completion IRQ) on the emulation thread — because that is emulated, deterministic, serial state — and
moves the *compute* (IDCT, colour conversion, packing) to a worker pool. The emulation thread never
waits on the worker in the normal case: each macroblock is dispatched as an immutable job and the
decoded words are consumed from an ordered result channel at the next natural re-entry point. The
worker produces output in microseconds; the emulation thread needs hundreds of microseconds to drain
it, so there is ~100x slack. If the worker is ever pathologically late, the emulation thread falls
back to waiting on the result — which is exactly what it does today, so **offload can only ever be as
slow as sync, and is normally far faster.**

Hard rule for thread-safety: **the worker owns zero bytes of `Mdec`.** It reads an immutable job
descriptor it was handed, writes its own result buffer, and flips an atomic "ready" flag. Everything
`Mdec`-shaped that the rest of the emulator reads (status, `block_rgb`, FIFOs) is written only by the
emulation thread, at drain time.

---

## 2. Current execution path (where the cost sits today)

```
DMA ch0 (bus.c:743-763)  ──slice──▶  mdec_dma_in()            mdec.c:524
                                          │ push 2 halfwords
                                          ▼
                                      mdec_execute()          mdec.c:389
                                          │ loop:
                                          │   parse command word
                                          │   mdec_decode_macroblock()   mdec.c:304
                                          │     mdec_decode_rle()        mdec.c:98   ← parse
                                          │     mdec_idct()              mdec.c:165  ← 2×8×8, int64
                                          │     mdec_yuv_to_rgb()        mdec.c:192  ← float
                                          │     mdec_copy_out_block()    mdec.c:230  ← packing
                                          ▼
                                      out FIFO (768 words)
                                          ▲
DMA ch1 (bus.c:765-797)  ◀──slice──  mdec_dma_out()            mdec.c:534
```

Key facts that shape the design (all current behaviour):

- **`mdec_execute` is the single funnel**: every input write (`mdec_dma_in`, register write
  `mdec.c:513/519`) and every output drain on empty (`mdec_dma_out` at `mdec.c:540-541`) re-enters it.
  Adding a "drain a finished job's words into the out-FIFO" step at the top of `mdec_execute` covers
  every re-entry point with one change.
- **One macroblock at a time, only when the out-FIFO is empty** (`mdec.c:307` and `:325`
  `if (!out_empty(m)) return false;`). The pipeline is therefore self-throttling: decode is serialised
  against DMA-out draining the previous macroblock.
- **The RLE parse is incremental** — it resumes mid-block when the in-FIFO runs dry
  (`mdec_decode_rle` returns `false`, state kept in `current_coefficient`, `current_q_scale`). The
  input for one macroblock arrives as an arbitrary number of DMA words.
- **The in-FIFO (2048 halfwords) and out-FIFO (768 words) are large**; a 320×240 FMV frame is 300
  macroblocks, each producing 128 words (15bpp) or 192 words (24bpp).
- **The status register leaks decode progress** (`mdec_get_status`, `mdec.c:53-70`): bit 29
  `command_busy`, bits 18-16 `current_block`, bits 15-0 parameter words remaining. Software can poll
  it, so decode progress must remain coherent — but nothing in it needs the *computed* pixels, only
  the *bookkeeping*.
- **`lua_debug_notify("mdec_macroblock")`** at `mdec.c:334` and the `emu.mdec_block` inspector read
  `m->block_rgb` (`lua_debug.c:277`), so the assembled macroblock must still land in `Mdec.block_rgb`
  on the emulation thread at drain time.

### Cost split per macroblock (host, approximate)

| Stage | Host cost | Moves to worker? |
|---|---|---|
| RLE parse → 6 × int16[64] blocks | cheap (~64-192 hw consumed) | no (needed for bookkeeping + job framing) |
| 2-pass IDCT, 6 blocks | expensive (int64 MAC heavy) | **yes** |
| YUV→RGB / Y→mono | medium (float) | **yes** |
| output packing 15/24/8/4 bpp | medium | **yes** |
| FIFO push/pop, status, DMA gates | tiny | no (emulated protocol) |

The worker takes ~90% of the host CPU cost per macroblock.

---

## 3. Core design — predictive deferred decode

### 3.1 Job boundary = one macroblock

A job is one decoded macroblock's compute, dispatched when the emulation thread has consumed all of
that macroblock's input and the out-FIFO is empty (the same gate as today).

Job descriptor (immutable, copied into the worker, never shared):

```
blocks[6][64]      int16   ← reconstructed by RLE on the emulation thread
scale_table[64]    int16   ← current IDCT matrix
output_depth       u8      ← 0/1/2/3
output_signed      bool
output_bit15       u8
iq_y / iq_uv       not needed (RLE already applied quantisation)
```

Size ≈ 1.5 KB — copying is cheaper than sharing and removes all synchronisation on the input side.

Result descriptor (written only by the worker):

```
words[192]  u32      ← packed output (max 24bpp color = 192 words)
count       u32
block_rgb[256]  u32  ← 0x00BBGGRR, so lua_debug/inspector keep working (copied into Mdec at drain)
```

The split point in today's code is exactly at the `mdec_idct(...)` call sites inside
`mdec_decode_macroblock` (`mdec.c:309`, `mdec.c:322`): instead of running IDCT inline, dispatch a
job. The mono path (`output_depth ≤ 1`) is a 1-block job; the color path is a 6-block job.

### 3.2 The emulation thread never waits (normal case)

1. DMA-in fills the in-FIFO; the emulation thread runs RLE incrementally as today (cheap), so
   `current_block`, `current_coefficient`, `current_q_scale` and the status register stay coherent.
2. When the macroblock's last block is reconstructed and the out-FIFO is empty, the emulation thread
   copies the descriptor and **dispatches** the job (queue + wake a pool thread).
3. The worker decodes (IDCT + YUV→RGB + packing) into its result slot and flips the ready flag.
4. The next re-entry into `mdec_execute` (or `mdec_dma_out`'s empty-drain at `mdec.c:540`) sees the
   ready flag, drains `words[0..count)` into the out-FIFO, copies `block_rgb` into `Mdec.block_rgb`,
   clears the flag. DMA-out then drains the out-FIFO as today.

Steps 3 and 4 overlap step 1 of the *next* macroblock (input collection continues while the worker
decodes the current one), so the pipeline already has depth ~1 even with a single job in flight —
matching hardware's one-macroblock-at-a-time semantics.

### 3.3 The safety valve (pathological case)

If the worker is slower than the emulation thread's need (it would take ~100x slower to matter), the
result is not ready when a re-entry needs it. Two responses, pick per policy:

- **Blocking drain**: `mdec_execute` waits on the result before filling the out-FIFO. This is exactly
  today's synchronous cost — the worst case is "as slow as now", never slower. Simplest, safe.
- **Non-blocking + backoff**: leave the out-FIFO empty; DMA-out sees no data, `dma_mdec_resume`
  already reschedules with `MDEC_IDLE_BACKOFF` when nothing moves (`bus.c:814`), and the next slice
  retries the drain. Slightly more complex, keeps the emulation thread non-blocking even in the worst
  case.

Recommendation: **blocking drain** first (correct, ~10 lines), the non-blocking variant only if
profiling ever shows the worker can fall behind. Do not build the complex path speculatively.

---

## 4. Threading and memory model

### 4.1 Primitives

Mirror what already exists in the tree — no new dependencies:

- `SDL_CreateThread` for pool threads (as `renderer_start_gpu_thread`, `renderer.c:1771`).
- `SDL_mutex`/`SDL_cond` for the job queue (as the CDROM async reader, `cdrom_disc.c:344-357`).
- `SDL_atomic_t` for the per-job ready flag (as the readback channel, `renderer.c:111-135`).

### 4.2 Ownership rules (the invariant to enforce)

| Memory | Written by | Read by |
|---|---|---|
| `Mdec` struct, all fields | emulation thread only | emulation thread only |
| job descriptor | emulation thread | worker (immutable after hand-off) |
| result slot (`words`, `block_rgb`, `count`) | worker only | emulation thread, only after ready flag set |
| ready flag | worker (set) / emulation thread (clear) | both — via atomics |

Because the worker never touches `Mdec`, there is no lock on the emulated object and no ordering
hazard with the rest of the emulator's single-threaded state. The only shared mutation is the ready
flag, an acquire/release pattern, once per macroblock (~300/frame at 60 fps — negligible).

### 4.3 Pool shape

- Default thread count: small and fixed (2-4) — FMV decode is a burst, not a sustained all-core load;
  2-4 workers are far more than the ~100x slack needs. Expose `ZS1_MDEC_THREADS` for profiling.
- Each worker owns its scratch (the `int32 src[64]`/`dst[64]`, float temps, etc. from
  `mdec_idct`/`mdec_yuv_to_rgb`) so there is no per-job allocation on the hot path.
- Job granularity is one macroblock; 300 jobs/frame is fine. Do **not** split a macroblock across
  workers (6-block IDCT then YUV→RGB is a dependency chain; the merge stage needs all six blocks).
- Pool lifecycle mirrors `renderer_stop_gpu_thread` (`renderer.c:1778-1793`): on shutdown, set a stop
  flag, signal, drain and join. The emulation thread also drains the last in-flight result before
  teardown so nothing is lost.

---

## 5. Timing model

### 5.1 What this design does (and does not) change

- **DMA slice schedule is untouched** (`dma_mdec_run_slice`, `bus.c:739-800`; `EVQ_MDEC` rescheduling
  at `bus.c:816`). The emulated CPU keeps seeing the same slice cadence and the same completion IRQ
  path (`dma_mdec_signal_done`).
- **What changes**: the out-FIFO is filled a few wall-clock microseconds *later* than today (today it
  is filled inline at the instant the macroblock finishes; with offload, at the first re-entry after
  the worker lands). In emulated cycles this is a tiny, bounded delay, and it only ever moves *toward*
  hardware (real MDEC also takes time to decode). If a game polls `command_busy`/out-request it sees a
  marginally more hardware-like picture, never a wrong one.

### 5.2 An independent follow-up: hardware decode-cost model

The current emulator has **no per-macroblock cycle cost** — decode is instant and timing emerges from
DMA slices. A separate, optional refinement would introduce a documented per-macroblock emulated cost
(+`EVQ` event) so completion is defined in emulated cycles and fully independent of worker speed. That
is a correctness/accuracy change in its own right (it alters emulated timing for every game, not just
with offload enabled) and must be designed, measured and merged **separately**. Do not couple it to
this offload.

---

## 6. Savestate interaction

`Mdec` is serialised today as a plain struct dump (`savestate.c:156`, tag `MDEC`). The worker pool
and the in-flight job/result channel are **host objects** — the same category as GL names and the
CDROM reader thread (`savestate.c:34-35` already documents this exclusion pattern). Rules:

- The pool, job queue and result slot are **excluded** from the serialised state.
- **Before saving**, the emulation thread must flush the pipeline synchronously: if a job is in
  flight, wait for its result and drain it into the out-FIFO *before* the `Mdec` struct is dumped, so
  the serialised struct contains no reference to an async result.
- On load, the pipeline starts empty; the pool is (re)started if not already running.

This keeps savestates deterministic and worker-speed-independent, exactly as the current sync model is
today.

---

## 7. Verification harness

Per `CLAUDE.md`: never quote speed figures taken with instrumentation on. The harness must compare
*output*, not timing.

- **Env switch `ZS1_MDEC_SYNC=1`** forces the dispatch seam to run the worker function inline. This is
  the reference path and the regression oracle.
- **Byte-exact compare**: same FMV input → identical `out-FIFO` word sequence with sync vs offload.
  The test corpus is `Ace Combat 2 (Europe)`'s FMV intro (the path that exercised MDEC end-to-end);
  capture the output stream of `mdec_dma_out` in both modes and diff.
- **Status coherence**: record `mdec_get_status` transitions in both modes; only `command_busy` /
  out-request timing may differ (and only toward hardware-like); bit 29 must never stay high
  indefinitely, and DMA ch1 must never underflow a *mid-transfer* read (out-FIFO is 768 words, a job
  is ≤192 words; with one job in flight this is structurally safe — assert it).
- **Perf**: `ZS1_FRAME_PROFILE=1` (`main.c:437-502`) on the FMV intro, before/after — watch the `emu`
  ms/frame figure drop during the movie while the `vram_upload` line stays flat. `perf stat` should
  show the process's core count moving from ~1 busy core to 2-5 during FMV.

---

## 8. Integration checklist (file:line)

| Where | What |
|---|---|
| `mdec.c:304-338` `mdec_decode_macroblock` | split at the `mdec_idct` call sites: RLE inline, then dispatch instead of inline IDCT |
| `mdec.c:230-298` `mdec_copy_out_block` | moves into the worker's compute (job's packing stage) |
| `mdec.c:192-214` `mdec_yuv_to_rgb`, `:218-224` `mdec_yuv_to_mono` | move into the worker |
| `mdec.c:165-184` `mdec_idct` | move into the worker; keep a scalar-C fallback plus an AVX2 path, selected at pool start |
| `mdec.c:389` `mdec_execute` | add "drain ready result → out-FIFO + copy block_rgb" at entry (covers all re-entries) |
| `mdec.c:534-543` `mdec_dma_out` | unchanged; its empty-drain already re-enters `mdec_execute` |
| `mdec.c:53-70` `mdec_get_status` | unchanged (reads bookkeeping only — verify no pixel path is used) |
| `mdec.h` | add job/result descriptors + a pointer to the pool **outside** `Mdec` (host state, like the renderer pointer) |
| `bus.c:739-818` DMA slices | unchanged — they already gate on `mdec_output_has_data`/`mdec_execute` and back off |
| `main.c` | start/stop the pool beside `renderer_start_gpu_thread` (`main.c:318` / `:535`); read `ZS1_MDEC_THREADS`/`ZS1_MDEC_SYNC` |
| `savestate.c:156` | keep `Mdec` dump as-is; ensure pipeline flushed before save (Section 6) |
| `lua_debug.c:270-280` | unchanged — `block_rgb` is repopulated at drain time |

---

## 9. Failure modes and cautions

- **The documented MDEC DMA kick loss** (`GAP_ANALYSIS_REFACTOR_2026-07-13.md` §4.1: a ch0/ch1 kick
  arriving mid-slice is dropped). The offload is orthogonal to it but must not be merged on top of a
  known-lossy path — verify the FMV corpus has no dropped kicks before relying on the harness.
- **Do not put the pool inside `Mdec`.** `Mdec` is serialised by size and read by the inspector;
  embedding host handles would corrupt both.
- **No allocation on the hot path**: job descriptors come from a fixed ring owned by the emulation
  thread; workers reuse their scratch. A descriptor is ~1.5 KB, one per macroblock in flight.
- **Ordering**: with a single job in flight, result order is trivially preserved. If pipelining is
  added later, tag results with a sequence number; do not let workers push directly into the out-FIFO
  out of order.
- **The `block_rgb` copy at drain time is 1 KB per macroblock** (~300 KB/s at FMV) — the price of
  keeping `lua_debug` and the inspector working. Acceptable; revisit only if it ever shows in
  `ZS1_FRAME_PROFILE`.

---

## 10. Generalising the pattern (SPU / VRAM — not this change)

The seam used here — *split the emulated protocol from the host compute, copy across an immutable job
boundary, never share mutable state, consume results at re-entry points* — applies to the other two
candidates from the hardware-utilization doc, with different conclusions:

- **SPU mixing** (`spu_mixing.c`): the arithmetic is cheap; the correct move is **SIMD vectorisation
  of the voice mix on the emulation thread**, not thread offload. Per-emulated-clock pacing
  (`EVQ_SPU`, 64-sample batches) must stay on the emulation thread either way.
- **VRAM upload packing** (`main.c:475-482`): pure data movement, no emulated timing — trivially
  parallel once dirty-rect tracking lands. Low priority.

Do not build either until the MDEC offload is measured end to end and the pool infrastructure proves
itself.

---

## 11. Open questions

1. Whether the per-macroblock hardware decode cost (Section 5.2) should ever land — recommend: only
   if a game is found to be MDEC-timing-sensitive. Measure first.
2. Whether the worker pool should be shared with any future offload (SPU/VRAM) or stay MDEC-private.
   Recommendation: MDEC-private until a second consumer proves the shared pool is worth the
   interface.
3. Blocking vs non-blocking drain (Section 3.3): start blocking; the non-blocking path is speculative.

---

## References

- `docs/HARDWARE_UTILIZATION_ANALYSIS_2026-08-01.md` — the parent analysis (item 5 = MDEC offload).
- `src/core/mdec.c` — current decode pipeline (all section references above).
- `src/core/bus.c:736-818` — MDEC DMA slice machinery.
- `src/core/dma.c` / `include/dma.h` — channel state and the mid-slice-kick caveat.
- `src/core/cdrom_disc.c:318-357` — the existing single-worker async pattern to mirror.
- `src/core/savestate.c:34-35,156` — host-object exclusion from serialisation.
- `GAP_ANALYSIS_REFACTOR_2026-07-13.md` §4.1 — the MDEC DMA kick-loss bug to avoid entangling with.
