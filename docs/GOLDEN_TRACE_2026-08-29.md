# The golden trace

*2026-08-29. Written as the prerequisite for the dynarec work: nothing else in
this repository can say whether a change to the CPU left the machine intact.*

## What it is

A run writes a checkpoint line every *n* instructions:

```
# zs1 golden trace v1 every=50000000 stop=700000000
# instr cycle pc path_hash state_hash
50000000 96482913 8005A1C4 3F1A...  9C02...
```

- **`path_hash`** — FNV-1a folded over `(current_pc, instruction)` for *every*
  instruction executed. It diverges on the first instruction that goes somewhere
  else, so a mismatch localises a control-flow change to one checkpoint interval.
- **`state_hash`** — folded over the register file, HI/LO, PC/nPC, the COP0
  registers the machine actually uses, and **both load-delay slots**, at the
  checkpoint. It catches a wrong *value* on a path that is otherwise identical.
- **`cycle`** — `inter->cpu_cycle_counter`. A change to the timing model shows
  here while both hashes still match, which is the distinction that matters when
  deciding whether a "host-only" optimisation really was one.

The load-delay slots are in the state hash on purpose: the bug this was written
after lived entirely in them (`LWL/LWR`, see CLAUDE.md), and it was invisible to
a boot and to CPI.

## Using it

```sh
tools/golden_trace.sh record     # on a build you trust
tools/golden_trace.sh verify     # after every CPU / bus / scheduler change
```

`verify` exits non-zero and prints the first differing checkpoint. Narrow it by
re-running with a smaller `ZS1_TRACE_EVERY`.

Environment, if driving it by hand:

| | |
|---|---|
| `ZS1_TRACE=<path>` | write a trace; unset means no trace and no cost |
| `ZS1_TRACE_EVERY=<n>` | instructions per checkpoint (default 1048576) |
| `ZS1_TRACE_STOP=<n>` | run exactly *n* instructions, then quit cleanly |
| `ZS1_CD_SYNC=1` | **required** — see below |

`ZS1_TRACE_STOP` ends the session itself, so a harness run needs no timeout and
no window interaction, and always covers exactly the same work.

## The machine was not reproducible, and that had to be fixed first

`cdrom_execute_drive()` reschedules `CDROM_READ_RETRY_DELAY` cycles later when
the async reader has not delivered yet (`cdrom_commands.c:822`). Whether a sector
has arrived by the time the drive looks depends on host file I/O, so the same
read lands at a different **emulated** cycle from one run to the next, and
everything after it diverges.

That is the right trade for playing a game — blocking there froze the whole
emulation thread for 115-232 ms on a cold seek, which is a dropped frame and an
audible gap — and the wrong one for comparing two builds. So `ZS1_CD_SYNC=1`
makes the drive wait instead, and the harness always sets it. A stalled frame
costs a harness run nothing.

Two other host-timing paths were checked and do **not** reach guest state: the
audio-ring backpressure in `main.c` only delays the loop, and the debug UI's
`/proc` sampling and Lua watches are read-only.

## What it does not cover

- **Host input.** A capture assumes no key and no pad movement. Touching the
  window during a capture invalidates it.
- **The renderer.** Nothing about what reached the screen is in the hash. GP0(C0)
  reads the CPU-side VRAM, so guest-visible state does not depend on the backend
  — but a rendering regression is invisible here by construction.
- **The SPU's output.** Voice state is not folded in. `ZS1_AUDIO_DUMP` is the
  instrument for that.
- **Anything after `ZS1_TRACE_STOP`.** The default 700M instructions is roughly
  the first 35 emulated seconds: boot, the shell, and into the first FMV on each
  of the four discs. A defect that needs an hour of play is not in scope.

A pass means: same instructions, same order, same register contents, same
emulated cycle, for that stretch, on that disc. It is not a proof of correctness
— it is a proof of *no change*, which is the thing an optimisation needs.
