# Dynarec: the plan, and what it is actually worth

*2026-08-29. Written after profiling, before any of it is built.*

## What the profile says

70 s of `Ace Combat 2` gameplay, `perf -F 997`, no logging, no Lua probe, on the
i9-14900HX. Percentages are of all P-core cycles in the process.

| % | Symbol | Thread |
|---|---|---|
| **46.8** | `gpu_thread_main -> X11_GL_SwapWindow -> __clock_gettime` | GPU |
| 16.6 | `cpu_run_next_instruction` | emu |
| 8.7 | `cpu_icache_fetch` | emu |
| 4.2 | `debugger_check_breakpoint` | emu |
| 2.2 | `system_run_frame` | emu |
| 1.9 | `debugger_check_read_watchpoint` | emu |
| 1.5 | `interconnect_load32` | emu |
| 1.5 | `decode_and_execute` | emu |
| 1.3 | `mask_region` | emu |
| 1.2 | `cpu_reg` | emu |
| 0.6 | `debugger_check_write_watchpoint` | emu |
| 0.5 | `cpu_set_reg` | emu |

The emulation thread is ~51% of samples; CPU interpretation is about 78% of it.
Frame profile baseline: `emu=3.71 ms` median of a 20 ms PAL field, CPI 1.635.

The GPU line was the largest single item in the whole process and had nothing to
do with the emulator — the driver busy-waits on vblank. Fixed already
(`__GL_YIELD=USLEEP` in `apply_gl_yield_preference()`, `main.c`): total process
CPU over 60 s of the same gameplay went 19.3 s -> 12.9 s, with the emulation
thread unchanged (3.710 ms against 3.690 ms, three interleaved runs each way).

## What a dynarec is worth here, honestly

**It does not make games run faster.** The guest's cycle budget per emulated
field is fixed, so executing those cycles in less host time leaves the host
idle sooner; it does not give the game more cycles. The payoff is host headroom,
and there are three real uses for it:

- **Sessions per machine.** The cluster deployment runs one pod per session.
  This is the number that changes.
- **Power**, on a laptop.
- **Room for internal upscaling**, later.

If the goal were "the emulator feels faster", the answer would be in the guest's
cycle model, not here — see the CPI note in CLAUDE.md.

## The hard part is the cycle model, not the code generation

Everything below is arranged around one constraint. Today `cpu_run_next_instruction`
charges `1 + stall` cycles **per instruction** and dispatches scheduled events the
moment `downcount` reaches zero. Compiled code wants to account per *block*. That
is an accuracy change, and it is the change most likely to break something that
took months to get right — the CDROM response deadlines are the obvious victim
(CLAUDE.md: "a deferred response re-armed at a 30 us minimum" cost a session).

So the plan is arranged so that the cycle model is decided by measurement, with
the golden trace as the instrument, before any code is emitted.

## Stages

### 0. Golden trace — done

`docs/GOLDEN_TRACE_2026-08-29.md`. Also fixed the thing that made the machine
irreproducible in the first place (`ZS1_CD_SYNC`). Every stage below is gated on
`tools/golden_trace.sh verify` passing.

### 1. Block cache, still interpreted

Discover a block from the current PC, cache the decoded `(handler, instruction)`
pairs, and run them. No code is emitted. This is the dynarec's whole front end —
block discovery, the cache, invalidation, the dispatch loop — built and validated
before the emitter exists.

- Key: physical address plus a cached/uncached bit (KSEG1 has different fetch
  timing, so the two are not the same block).
- Block ends after a branch or jump's delay slot, at a page boundary, or at a
  cap.
- Removes the per-instruction i-cache tag lookup (8.7%) and the double dispatch
  through `s_op_table`/`s_special_table` (1.5%), plus part of the loop's own
  overhead.
- **The golden trace must be byte-identical.** Per-instruction accounting is
  untouched at this stage, so anything else is a bug in the block cache.

Alongside, the cheap items the profile turned up, each measured on its own:
inline count-gated fast paths for the three debugger checks (6.6%, finding
nothing when nothing is armed); `cpu_reg`/`cpu_set_reg` inlined without their
`index >= 32` bounds check, which is provably dead — every index comes from
`instr_s/t/d`, masked `& 0x1F`, or a literal <= 31 (1.7%); `mask_region` inlined
(1.3%); the exec-trace ring, which is 2 stores into 64 KB inside `Cpu` on every
instruction.

### 2. Decide the event granularity — the one real fork

Try per-block downcount checking on top of stage 1 and run `verify`.

- **If the trace still matches**, the granularity is free and stage 3 is
  unconstrained.
- **If it diverges**, the diff names the subsystem. The fallbacks, in order of
  preference: bound each block by the cycles remaining until the next event
  (so a block never straddles one); or keep the per-instruction decrement and
  check only at block end plus at instructions that can raise.

Do not skip this by assuming. The whole reason this document exists before the
code is that "a correct cost broke the machine" has already happened here twice.

### 3. The emitter

x86-64, one block at a time, falling back to the existing handler for any
instruction not yet covered — so coverage grows without a flag day and the trace
verifies at every step.

Order of coverage follows the profile: `lw`, `bne`, `addiu`, `sll`, `slt`, the
SPECIAL arithmetic, then the rest. Notes on the pieces that are specific to this
machine:

- **Load delay.** Within a block the compiler knows both the load's target and
  whether the next instruction reads it, so the two-slot rotation collapses to a
  register move in most cases and disappears in the rest. This is where a large
  part of the win is, and it is also the code the golden trace was built to
  watch — the state hash folds both slots.
- **Memory.** Inline the RAM fast path (`phys < 0x800000`, direct index, plus the
  3-cycle stall from `bus_charge_cpu_load`) and call out for everything else.
  `interconnect_load32` is already one comparison; keep it that way.
- **Exceptions.** A side table from host PC to guest PC, or store `current_pc`
  before anything that can fault. EPC and the BD bit have to stay exact.
- **Register allocation.** Start with none — a memory-resident register file is
  still far ahead of a call per operation. Add allocation once the trace is
  stable, not before.

### 4. Invalidation

Self-modifying code and code written by DMA. A per-page "contains code" bit;
a store or a DMA into a marked page drops the blocks covering it. The
cache-isolate path (`SR & 0x10000`) already exists and is the other entry point.

## Risk

The safety net is four discs times ~35 emulated seconds of golden trace. That is
enormously better than what this project had yesterday, which was nothing, and
it is still not complete: it does not cover the renderer, the SPU's output, or
anything that needs an hour of play. Widen `ZS1_TRACE_STOP` and add discs as the
work goes on, and treat a stage that cannot be verified as a stage that is not
done.
