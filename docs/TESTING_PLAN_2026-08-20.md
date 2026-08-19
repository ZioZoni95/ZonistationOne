# Testing plan

2026-08-20. Written the day after a CPU bug that had been live for months was found by accident.

## Why this document exists

`make test` builds `tests/cpu_minimal_test.c`. That file does not exist; `tests/` is an empty
directory. There is no automated test of any kind in this repository. Every accuracy claim in
`CLAUDE.md`, `README.md` and the two audit documents was established by running a game, reading a
log, and comparing against a reference emulator by hand.

That method is not worthless — it found four real defects on 2026-08-19 — but it has a measurable
failure mode, and on 2026-08-19 the bill came due.

### The case that motivates all of this

`op_lwl` and `op_lwr` merged the register's old value from `cpu->load_reg_idx` instead of
`cpu->delay_load_reg`. Every unaligned 32-bit load whose pair needed a genuine merge came out with
stale high bits.

- It was introduced by the load-delay rework in `1e0aa4f` and lived through every commit since.
- It survived because it is **data-dependent, not code-dependent**: an `(lwl+3, lwr+0)` pair writes
  the whole register twice and looks correct. Only a partial pair such as `(lwl+1, lwr+2)` exposes
  it. The BIOS, Ace Combat 2 and Monsters & Co. never hit one on their boot paths.
- It was found by disassembling a commercial game's ISO path-table walk and logging guest registers
  at six program counters, after first proving the CD data path correct and diffing the command flow
  against a DuckStation Devel build. That is most of a day.
- A test would have been **four alignments × four instructions = sixteen assertions**, about fifty
  lines, running in microseconds, and it would have failed the moment the rework landed.

The lesson is not "we should have been more careful". It is that a whole class of defect — correct
except at one alignment, one boundary, one flag combination — is invisible to game-level testing and
trivial for a test to pin. Every subsystem here has that class: GTE saturation edges, DMA block
boundaries, timer wrap, SPU envelope corners, CDROM BCD validation.

---

## Layer 1 — instruction and unit semantics

**What:** call the emulator's own functions directly, no BIOS, no disc, no window. Set up a `Cpu`
and some RAM, execute one instruction, assert the register and memory result.

**Why first:** cheapest to write, fastest to run, and it covers exactly the class of bug described
above. It also needs no determinism work and no disc images, so it can exist today.

**First targets, in order of expected yield:**

1. `LWL`/`LWR`/`SWL`/`SWR` at all four alignments, including the pair-merge case and the
   `lw`-then-`lwl` case that the fix's own comment flags as a judgement call.
2. The load delay itself: the delay-slot opcode reads the old value; a same-register write by that
   opcode wins; an exception lands the load on the way in
   (`psx-spx-docs/docs/cpuspecifications.md:172-177`).
3. GTE saturation and flag bits — 22 opcodes, and `MVMVA` must reset FLAG, `LZCR` is UB for
   `LZCS=FFFFFFFFh`. Both are in the audit's finding list.
4. Packed-BCD validation shared by `Setloc` and `GetTD`.
5. `cdrom_get_stat_byte()` for each drive state, disc present and absent.

**Cost:** the harness is a few hours. The Makefile already has `TEST_SRCS`, `TEST_BIN` and a `test`
target wired; it needs source files, not build work.

---

## Layer 2 — hardware test ROMs

**What:** the PS1 scene has test suites written to run on real consoles and report pass/fail per
behaviour — amidog's CPU/GTE tests and PSXAuthenticTest are the usual references. Run them and
publish the score.

**Why:** it replaces "how many games boot" with an axis that does not depend on how many hours have
been spent. "N of M behaviours pass, here is the list of failures and why" is verifiable by anyone,
comparable across emulators, and does not require winning on compatibility. It is also the fastest
way to find out how many more defects of the `lwl`/`lwr` shape are in the tree.

**Cost:** low — the ROMs exist and boot like any other disc or EXE. The work is capturing their
output (the TTY side-channel already does this) and recording the result as a tracked file so a
regression is visible in a diff.

**Caveat:** a failing hardware test is not automatically a bug worth fixing. Record the failures,
rank them by whether any software depends on them, and do not let the score become the goal.

---

## Layer 3 — golden runs on real games

**What:** boot a known disc, capture the boot milestones on the emulated-field axis, and compare
against a stored baseline. This is exactly the comparison already used by hand — TTY lines carry the
CRTC field count, and the same axis is recovered from a reference emulator by counting its v-blanks.

**Why:** it catches what unit tests cannot — an interaction between subsystems, or a timing change
that moves the whole machine. `Execute !` at f804 / f874 / f843 for the three tested discs is
already, informally, a baseline; today it is checked by a human reading a log.

**Prerequisite: determinism.** Two runs of the same disc must produce the same field numbers, every
time. Today that is approximately true and not guaranteed — the CD reader and the GPU run on their
own threads. Until the run is reproducible cycle for cycle, a golden-run diff produces noise that
will be ignored, which is worse than not having it.

**Cost:** medium, and mostly in the determinism work rather than the comparison.

**Note:** discs cannot be committed. The baselines can be, keyed by a hash of the image, and skipped
when the image is absent.

---

## Layer 4 — citations as tests

**What:** the two audit documents already carry the project's own rule — no entry may claim
"correct" without citing a documentation line **and** a code line, and anything not compared is
marked `UNVERIFIED`. The next step is to make each of those pairs executable: the documentation line
states a behaviour, so a test asserts it, and the citation becomes the test's name.

**Why:** it turns the audits from a snapshot that decays into something that fails when the code
drifts away from the line it cites. It is also the one thing in this project that no other emulator
does, and it is the natural end of a discipline that already exists on paper.

**Cost:** high in total, but it is incremental by construction — every audit finding fixed can land
with its test, and the backlog can be worked at any pace.

---

## Order

1. **Layer 1**, starting with `LWL`/`LWR`/`SWL`/`SWR` and the load delay. It is small, it pays
   immediately, and it closes the specific hole that cost a day.
2. **Layer 2**, because it is cheap and tells you how much of Layer 1 is still missing.
3. **Determinism**, because Layer 3 is noise without it and because it pays elsewhere — reproducible
   bug reports, and an automatic diff against a reference run instead of a manual one.
4. **Layer 3**, then **Layer 4** as a standing habit rather than a project.

## What not to do

- Do not write tests for the renderer's output pixels first. It is the most expensive kind of test
  to write and maintain, and the GPU is not where the bugs that hurt have been.
- Do not gate commits on the suite until it is trustworthy. A flaky suite that everyone learns to
  ignore is worse than no suite.
- Do not chase the hardware-test score as a number. Rank failures by consequence.

## Related

- `docs/CDROM_AUDIT_2026-08-17.md` and `docs/DMA_IRQ_GTE_MDEC_AUDIT_2026-08-17.md` — the finding
  lists that Layer 4 would turn into tests.
- `docs/ecm_libcrypt_discovery.md` — the full account of the 2026-08-19 session, including how much
  of it was spent on two wrong theories before the CPU was suspected.
- `scripts/` — 94 Lua probes. These are the manual equivalent of Layer 3 and should be mined for
  assertions rather than replaced.
