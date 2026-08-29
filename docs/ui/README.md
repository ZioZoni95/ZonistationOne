# Interface direction

`ui_direction.html` and `ui_direction_enhanced.html` are static mockups — open them in a browser. They are the targets the debug interface is being rebuilt against, kept in the repo so the design is a thing to check work against rather than something remembered from a conversation.

## Why change anything

The current interface is a grid of floating ImGui panels, one per component: CPU here, GPU there, SPU
somewhere else. That is the shape every ImGui-based emulator debugger converges on, and it does not
match how this project is actually debugged: **every defect that has cost a session lived between two
subsystems, not inside one.** The FMV columns that never reached the texture, the mask bit that turned
black into green, the reverb writing over voice data, the audio queue draining faster than it filled —
each was found by correlating two subsystems by hand, with Lua scripts and offline analysis, because
no panel showed the relationship.

So the reorganisation is not cosmetic. The views are chosen to make cross-subsystem behaviour
visible, and the visual identity follows from that rather than the other way round.

## Design tokens

| Role | Value | Note |
|---|---|---|
| Ground | `#0E1117` | blued graphite, not neutral black |
| Panel | `#161B24` / `#1B2130` | |
| Hairline | `#242C3A` | |
| Text / muted / faint | `#DDE4F0` / `#8792A6` / `#5C6579` | |
| Video path | `#35C3F0` | CD → MDEC → DMA → VRAM |
| Audio path | `#FF5C8A` | XA → SPU → device |
| OK / warning / critical | `#4CC38A` / `#E8B33A` / `#E5484D` | severity only, never decoration |

The two accents **encode the data path**, they are not a colour scheme: anything on the video chain is
cyan, anything on the audio chain is rose. Severity colours are separate and reserved.

Type: system stacks (a webfont would need shipping a font file), with identity carried by density
instead — uppercase letter-spaced micro-labels, tabular numerals everywhere numbers align, a tight
scale.

## Layout

- **Machine bar** — BIOS, disc, PC, and the live vitals: real-time %, frame ms, audio queue depth,
  SPU drift. The numbers currently obtained by running a Lua script, always on screen.
- **Mode rail** — Pipeline, Display only, Frame, Code, Memory, Audio, VRAM, Script. Modes replace
  scattered windows.
- **Stage** — the emulated screen is permanent, at the top, in every mode, with the scanout parameters
  beside it (origin, window, depth) so a number and the pixels it describes are in one glance.
  "Display only" expands it and folds the panels away.
- **Inspector** — contextual to the mode.
- **Dock** — console and per-category logs, tabbed.

## The views that are new

- **Pipeline** — CD → XA → MDEC → DMA → VRAM/scanout on one row with live rates. A stage that stops
  feeding the next one is visible instead of deduced.
- **Frame** — uploads, draw batches, XA sectors and the display flip on a time axis against the frame
  budget. "Thirteen of twenty columns never arrived" reads straight off the track.
- **Audio** — produced against consumed, queue depth over time, drops. This is the distinction between
  a DSP bug and a delivery problem, which cost most of one session to establish by hand.
- **Pinned Lua watches** — any expression as a live tile instead of a log line.

Everything currently available stays: disassembly, registers, breakpoints, watchpoints, the VRAM
viewer with its decode modes, per-category logs, the Lua console.

## Implementation phases

Ordered so that each phase is independently useful and the risky parts come last.

| Phase | Work | Depends on |
|---|---|---|
| 1 | Visual identity (own `ImGuiStyle`), permanent screen on stage, machine bar | — |
| 2 | Mode rail with per-mode dock layouts | ImGui dock builder |
| 3 | Pipeline view | a small per-frame stats struct fed from counters that already exist |
| 4 | Frame inspector | a per-frame event ring with cycle timestamps (the renderer records order today, not time) |
| 5 | VRAM CPU-vs-GPU diff | the cross-thread GL readback already tracked in `GPU_GAP_ANALYSIS_2026-07-15.md` §3.2 |
| 6 | Pinned Lua watches | per-frame evaluation with error and cost containment |

**Standing constraint**: the debug interface is what costs, not the core. With every panel closed the
emulator keeps real time; with the log windows and inspectors open under WSL it does not, and audio
underruns follow. So new views must either read counters that exist anyway or do their work only while
visible — the pattern the VRAM viewer already follows — and the panel set as a whole needs a measured
per-frame budget rather than an assumed one.

---

## The gameplay shell (2026-08-20)

The window now has two shells. `ui_direction.html` remains the target for the **debug workspace**;
what follows is the second one, and what of it is not built.

The gameplay shell exists because the workspace is the right interface for finding a bug and the
wrong one for playing a game — nine view modes, a log dock and an inspector are noise when the
question is "does this run". It shows the emulated screen, a HUD that fades after ~3 s idle, and a
quick menu on `Esc`. Switching is continuous in both directions: the backquote key, Shift+F1, the
*Gameplay* button on the machine bar, or the menu's *Debug workspace*; the machine keeps running
across the switch, and `ZS1_UI=debug` opens straight into the workspace.

It owns no machine state. `Esc` reaches it through `debug_ui_escape_pressed()`, which returns false
in the workspace so the key still quits there, and the menu parks requests that `main.c` carries out
— the same shape a Lua script's `emu.load_state()` already used.

### What of it is not implemented

Each of these is blocked on the machine, not on the interface. They are listed here so the next
person starts from the blocker rather than from the panel.

| Missing | What it needs first |
|---|---|
| Disc library — pick a title from `games/` | A scan and a launch path; the disc comes from `--game=` today |
| Hot disc swap | The shell-open latch (status bit 4), the INT the guest is owed, and the region check. `cdrom_load_disc()` alone would change the image behind the guest's back |
| Reset console | A `system_reset()` that re-initialises CPU and Interconnect against a live machine, reaching the drive, SPU RAM and the event queue. A savestate load restores, it does not restart |
| Volume, scaling, crop, scanlines | Runtime setters in the renderer and the mixer. None exist; reverb is not a host preference at all but guest state in SPUCNT |

*Reset console* was deliberately left out of the quick menu rather than bound to something that only
looks like a reset. The settings pane shows what the machine reports and offers only the controls
that exist: pad mode, save-state slots, the workspace, quit.

### What the shell did change elsewhere

The panels stopped carrying typed-in data — see `CLAUDE.md` for the detail. Host HW and the
inspector's host block read `/proc`, `/sys`, `uname` and the live GL and SDL device strings through
`src/core/host_info.c`; the pinned watches are Lua expressions through `lua_debug_eval_expr()`
(phase 6 of the table above, now done); the Pipeline view's status words are real machine state.
