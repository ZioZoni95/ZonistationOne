# Contributing

Thanks for looking at ZoniStation One. A PS1 emulator is checked against hardware documentation and
real games, so contributions are reviewed on evidence. This page lists what that means in practice.

## Build and test

```sh
sudo apt install build-essential libglew-dev libgl1-mesa-dev        # plus SDL3, see README.md
make clean && make        # clean build; stale objects from another gcc break the LTO link
make test                 # unit tests, no SDL, BIOS or disc needed
make hwtest               # bare-metal PS-X EXEs run inside the emulator, OpenGL and Vulkan
```

`make hwtest` needs `gcc-mipsel-linux-gnu`, `xvfb` and `mesa-vulkan-drivers`. The BIOS image and disc
images are supplied by whoever runs the emulator, and are never committed.

The build emits no warnings from this project's own sources. Keep it that way.

## Evidence

- Cite the hardware behaviour: the psx-spx file and line (`DOCS/` or `psx-spx-docs/`) next to the
  code that implements it, or state the measurement.
- Compare timing in **emulated fields** (the `[f... t...]` stamp in every log line), not wall clock.
- Never quote a speed figure from a run with debug logging, a Lua probe or a breakpoint active.
- Prefer the in-tree Lua probes (`ZS1_LUA_SCRIPT=scripts/x.lua`) over external dumps.
- A fix for a defect should come with a test that fails before it: a unit test in `tests/`, or a
  bare-metal check in `tests/hw/`.

## Licensing

- The project is GPL-3.0-or-later. Every source file carries an SPDX header.
- **Do not copy code from `duckstation_ref/`.** It is CC-BY-NC-ND-4.0 and incompatible with the GPL.
  It may be consulted for what the hardware does, and described in a comment, never reproduced.
- Code from `pcsx-redux/` (GPL-2.0-or-later) may be used with its attribution header intact.
- Update `THIRD-PARTY.md` when adding a dependency.

## Code

- Pure C99, except `src/debug_ui.cpp` (ImGui). No `malloc` in hot paths.
- Match the surrounding code: naming, comment density, idiom.
- `include/renderer.h` and `include/timers.h` are CRLF. Edit them by line.
- Changes to the savestate layout bump `ZS1_STATE_VERSION` in `savestate.c`.
- Add a `CHANGELOG.md` entry, and document any new `ZS1_*` variable in `README.md` and `CLAUDE.md`.

## Pull requests

- Target `stable_branch`.
- Fill in the template, including the "Not verified" section. Saying what you could not run is
  expected and welcome.
- Keep one topic per pull request, and split mechanical changes from behaviour changes.
- Conventional commit prefixes are used in the history: `feat`, `fix`, `docs`, `test`, `chore`.
