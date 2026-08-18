# ZoniStation One

A PlayStation 1 emulator written from scratch in C99, with an OpenGL 3.3 renderer and a built-in
debugger. Low-level: the real BIOS runs as-is, no syscall is faked, and games boot the way hardware
boots them.

SDL3 + OpenGL 3.3 Core (GLEW), ImGui for the debug interface. One commercial disc has been played
past the halfway point — boot, FMV, menus, missions, saves — and it is stable there. That is one game
on one machine, not a compatibility claim.

---

## Screenshots

Power on, BIOS shell, disc boot, movie, mission.

![Sony Computer Entertainment boot logo](screenshots/2026-08-06-boot-sony.png)
![PlayStation logo and the SCEE licence screen](screenshots/2026-08-06-boot-playstation.png)
![BIOS shell menu with its 3D objects](screenshots/2026-08-06-bios-menu.png)
![Ace Combat 2 (Europe): the FMV intro](screenshots/2026-08-06-ace-combat-2-fmv.png)
![Ace Combat 2 (Europe): in-engine 3D with the HUD](screenshots/2026-08-06-ace-combat-2-ingame.png)

---

## Build

```sh
sudo apt install build-essential libglew-dev libgl1-mesa-dev
make
```

SDL3 is not packaged on Ubuntu 24.04 or its derivatives. Build it once:

```sh
sudo apt install cmake libwayland-dev libxkbcommon-dev libx11-dev libxext-dev libasound2-dev
git clone --depth 1 --branch release-3.2.24 https://github.com/libsdl-org/SDL.git
cmake -S SDL -B SDL/build -DCMAKE_BUILD_TYPE=Release -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF
cmake --build SDL/build -j"$(nproc)"
sudo cmake --install SDL/build && sudo ldconfig
```

Where SDL3 *is* packaged, `sudo apt install libsdl3-dev` replaces that block. Either way the Makefile
finds it through `pkg-config sdl3`.

ImGui and Lua are vendored in `third_party/`. `make` is parallel by default and tracks header
dependencies, so `make clean` is not needed after editing a header. `make DEBUG=1` gives an `-O0 -g`
build for gdb.

## Run

```sh
./ZoniStation_One roms/bios-pal.bin                          # BIOS menu
./ZoniStation_One roms/bios-pal.bin --game="games/game.bin"  # a disc
```

You supply the BIOS and the discs; neither is in this repository.

### Running your own games

**PAL only for now.** The PAL BIOS (`SCPH-7502`) is what is tested and what the emulated CD drive is
set up to report. A US BIOS boots to its own menu, but no NTSC disc has been run past that.

Three things trip people up, in order of how often:

1. **Pass the `.bin`, not the `.cue`.** `--game=<path>.cue` is accepted and then reports
   *"Disc load failed — BIOS-only mode"*, which reads like a disc problem but is a path problem.
2. **The BIOS must match the disc's region**, exactly as on hardware. A PAL disc with a US BIOS is
   rejected and you are left sitting at the BIOS menu, which looks like a boot regression.
3. **The game path needs `--game=`.** A bare positional path is taken as the BIOS path.

### Controllers

A DualShock 4 over USB or Bluetooth is picked up automatically, hot-plug included, and the keyboard
stays live beside it (`WASD` D-pad, `E C Z X` for △○×□, `Q R` shoulders, `Shift Ctrl` triggers,
`Space` START, `Tab` SELECT).

The emulated pad boots **in analog mode**, which hardware does not do — a real pad starts digital with
its LED off and waits for its Analog button. Booting analog means the sticks reach the game as analog
inputs without a keypress first, and digital-only titles read the same button bytes as before.
**F12**, or a click on the DS4 touchpad, is the Analog button:

```
digital (ID 41h, LED off) → analog pad (73h, LED red) → analog stick (53h, LED green) → digital
```

The green stick mode is what a few flight titles expect instead of a DualShock, Ace Combat 2 among
them. `ZS1_PAD_MODE=digital|analog|stick` sets the boot mode; the Controller window in the debug UI
shows the current one and cycles it. A game that drives the pad itself through the config commands
overrides all of this, as on hardware.

A DS4's light bar shows those colours, so the mode a game selected is visible on the pad. It needs
access to the pad's hidraw node, which is root-only by default; without it SDL uses the kernel evdev
path — buttons, sticks and rumble work, the light bar does not.

```sh
echo 'KERNEL=="hidraw*", ATTRS{idVendor}=="054c", MODE="0660", TAG+="uaccess"' \
  | sudo tee /etc/udev/rules.d/99-sony-hidraw.rules
sudo udevadm control --reload-rules && sudo udevadm trigger
```

Replug the pad afterwards.

Button bits are the same in every region — the pad's bottom button is × (bit 14) and its right button
○ (bit 13) worldwide. What varies is the software: the PS1 shell and Japanese-developed titles confirm
with ○, most Western ones with ×. `ZS1_PAD_SWAP_XO=1`, or the checkbox in the Controller window,
swaps which physical button drives which bit if you prefer × to confirm everywhere.

### Environment variables

| Variable | Effect |
|---|---|
| `ZS1_LOG_LEVEL=silent\|error\|warn\|info\|debug\|trace` | Log level (default `info`; the hot paths are genuinely expensive above it) |
| `ZS1_LOG_STDERR=1` | Also write the log to stderr, not just the in-app windows |
| `ZS1_LUA_SCRIPT=scripts/x.lua` | Run a Lua debug script at startup |
| `ZS1_FRAME_PROFILE=1` | Where each frame's wall-clock time goes, plus cycles per emulated instruction |
| `ZS1_GPU=nvidia\|intel` | On a hybrid-graphics machine, ask for the discrete or the integrated GPU |
| `ZS1_AUDIO_DUMP=path` | Record what is handed to the sound device, as raw interleaved 16-bit stereo |
| `ZS1_SPU_NO_REVERB=1` | Bypass the reverb stage — an A/B switch when judging an artefact |
| `ZS1_OVERSCAN=0` | Show the 8 display lines an NTSC TV crops at the top and bottom instead of cropping them (PAL is never cropped — it is underscanned already) |
| `ZS1_DMA_GPU_PACE=legacy` | Pace GPU DMA with the old flat quantum instead of the documented 1 clk/word |
| `ZS1_DISPLAY_LATCH=1` | Build each frame from the display state latched at the previous field boundary — an A/B switch, off because it delays the picture a whole field |
| `ZS1_SPU_NO_STRETCH=1` | Bypass the output time-stretch, same purpose |
| `ZS1_DUMP_FRAME=path` | Dump a rendered frame as raw RGB (`ZS1_DUMP_FRAME_N` selects which) |
| `ZS1_TTY_TRACE=1` | Name the BIOS hook behind every captured TTY line |
| `ZS1_PAD_MODE=digital\|analog\|stick` | Pad mode at boot (default `analog`; `digital` is what hardware does) |
| `ZS1_PAD_SWAP_XO=1` | Report the pad's bottom button as ○ and its right one as × |

`ZS1_GPU` sets the PRIME offload variables the driver stack already reads, before the GL context is
created; setting them by hand works identically. The run always logs which driver it ended up on and
whether the request was honoured — asking for the discrete card and quietly getting the integrated one
is a normal failure, and it is a real source of rendering differences.

---

## Status

| Component | Status | Notes |
|---|---|---|
| CPU (MIPS R3000A) | Working | All instructions, COP0, exceptions, branch/load delay, MULT/DIV and GTE stalls |
| I-Cache | Working | 256 lines × 4 words, tag + per-word valid bits |
| RAM / BIOS ROM | Working | 2 MB + 1 KB scratchpad; SCPH-1001 and SCPH-7502 |
| IRQ controller | Working | Edge-triggered I_STAT/I_MASK, every source wired |
| Event scheduler | Working | Single authority; wrap-safe scheduling |
| DMA | Working | All channels; linked-list and block transfers, completion interrupts |
| Timers 0/1/2 | Working | Derived counters, all sync modes, video-mode-derived clock rates |
| CDROM | Working | Async command/response, region detection, XA audio, drive seek and spin-up timing; a response owes its own deadline, which an interrupt acknowledge or a re-issued command cannot shorten; GetlocL/Pause refuse with 80h during a seek, Setloc validates BCD, and the ATV volume matrix reaches the mix |
| SIO / controllers | Working | DualShock 4 over USB/Bluetooth (the only pad tested), keyboard alongside; digital 41h, analog pad 73h, analog stick 53h, config F3h, both memory card slots |
| GTE | Working | All 22 opcodes with saturation/flags, per-op cycle costs charged to the CPU |
| GPU / renderer | Working | OpenGL 3.3 only. Unified VRAM texture (raster + upload + scanout), 15bpp and 24bpp, VRAM readback for render-to-texture |
| MDEC | Working | Full decode pipeline, exercised by real FMV playback |
| SPU / audio | Working | Sample generation on the emulated clock, WSOLA time-stretch on the output |
| Savestates | Working | F5 / F8, whole machine, disc identity checked on load. Format v8 |
| PCDrv | Working | Host filesystem side-channel for homebrew |
| Debugger / UI | Working | Disassembler, breakpoints, watchpoints, exec trace, Lua console |

The machine's three clocks hold nominal: measured over 128 seconds, the CPU runs at 1.0002 of
33.8688 MHz, video at 0.9997 of 50 fields a second, audio at 1.0002 of 44100 samples a second, and the
CD drive streams at the same 150 sectors a second a reference emulator does.

Boot timing is checked the same way, in emulated fields rather than wall clock — every log line
carries the CRTC field count, and a reference emulator's run is put on that axis by counting its
v-blanks. `Ace Combat 2` reaches each of its BIOS TTY milestones within ~2% of the reference, end to
end, and the machine holds 50 fields a second while doing it.

Rendering is OpenGL 3.3 only — no Vulkan, no software renderer.

---

## Known bugs

Accepted for now. Each is understood well enough to say what it is and what it is not.

- **The picture shifts at some transitions.** Games move the GP1(07) vertical display range — this one
  walks 240 → 254 → 236 → 239 → 240 scanlines while booting — and the presentation does not yet place
  those on a fixed raster the way a TV does. Not a rendering fault: the image is correct, its framing
  is not.
- **Audio cuts for a few seconds at scene changes.** Twice in a 212-second session, at 3.08 s and
  2.72 s. Both are the game itself clearing SPUCNT bit 0 and taking CD audio out of the mix; the ring
  never starved. What is not established is why the gap is that long, which depends on what the drive
  is doing meanwhile — `scripts/recenter_watch.lua` reports the sector rate inside the window.
- **Occasional small audio underruns** during play. One event in that same 212-second session.
- **Rumble is unverified.** Implemented on both the one-motor and the `4Dh`-mapped methods, never
  confirmed against a real pad.
- **No multitap, no DualShock 2 pressure sensing.**
- **The CRTC advances once per frame**, not per scanline, which leaves Timer0's hblank gate unwired.
  Texpage bit 11 (Y base 2) is not applied.
- **BIOS ROM *data* reads still cost nothing.** Instruction fetches out of ROM now pay their MEMCTRL
  wait states, which is what closed the two-second gap this entry used to describe, but the tables the
  BIOS keeps in ROM are read for free. What is left of the gap is small and points the other way in
  places: the phase after `Execute !` runs about half the reference's length. Charging those reads was
  tried and overshoots — it moves every milestone ~15% later.
- **`make test` does not build**; `tests/` is referenced by the Makefile but is not in the tree.

---

## Debug UI

The SDL3 window is an ImGui workspace; nothing goes to the terminal. A **machine bar** carries BIOS,
disc, live PC and the vitals (frame ms, audio-queue depth, drift). A **mode rail** on F1–F8 replaces
what used to be a grid of floating panels:

- **Pipeline** — CD → XA → MDEC → DMA → VRAM on one row with live rates, plus audio meters and a
  CPU-model-vs-GPU-texture VRAM comparison
- **Display** — the emulated screen given the whole stage
- **Frame** — the frame's uploads, batches, DMA completions and XA sectors plotted by CPU cycle
  against the frame budget
- **Code** — disassembly with registers and breakpoints
- **Memory** — hex view over RAM / scratchpad / BIOS
- **Audio** — SPU voices, ADSR and reverb
- **VRAM** — the full 1024×512 with selectable decode (4/8/16/24 bpp), CLUT picking, mask and
  greyscale views, the active display area outlined, zoom, pan and a per-pixel readout
- **Script** — the Lua console; `emu.*` reads live internals a memory dump cannot show. Probes live in
  `scripts/`

F10 run/pause, F11 step, F5/F8 save and load state, Alt+Enter fullscreen. Space is the pad's START
button, not pause.

---

## Layout

```
src/cpu/      MIPS R3000A: decode, execute, exceptions, I-cache, BIOS syscall side-channel
src/core/     bus, RAM, BIOS, DMA, timers, SIO, MDEC, event scheduler, savestates, Lua surface
src/gpu/      GP0/GP1 command handling, OpenGL renderer, VRAM
src/gte/      the 22 GTE operations
src/cdrom/    controller, commands, disc images, XA/CDDA audio
src/spu/      24 voices, ADSR, reverb, DMA, IRQ, time-stretch
src/main.c    host shell: SDL, GL context, audio device, threads, frame pacing
src/debug_ui.cpp  the ImGui interface (the only C++ in the tree)
```

Design notes worth knowing before changing anything:

- The **SPU ring is the machine's clock**. The emulation loop runs ahead only until the ring is full
  enough, so anything that changes how fast the ring drains changes how fast the whole machine runs.
- **VRAM is one GL texture** that is simultaneously the render target, the upload target and the
  scanout source. `gpu.vram.data` is a CPU-side model that never sees rasterised pixels.
- **The event scheduler is the single timing authority.** Nothing else may schedule work.
- **No `malloc` in hot paths.** Structs are embedded, not heap-allocated.

Memory map, per-subsystem state and the open work queue: `docs/GAP_ANALYSIS_REFACTOR_2026-07-13.md`,
`docs/GPU_GAP_ANALYSIS_2026-07-15.md`, `docs/study/README.md`, and `docs/ui/` for the interface direction.

---

## References

What this emulator was actually written against:

- **psx-spx**, Martin "nocash" Korth's PlayStation specification, [psx-spx.consoledev.net](https://psx-spx.consoledev.net/) —
  the source of nearly every value in the code, cited by file and line as `DOCS/…`.
- **[PCSX-Redux](https://github.com/grumpycoders/pcsx-redux)** (GPL-2.0+) — consulted where the
  specification is ambiguous, and the origin of parts of the SPU; credited in the file headers.
- **[DuckStation](https://github.com/stenzek/duckstation)** — consulted only for *what the hardware
  does*. Its licence forbids derivative works, so none of its code is here.
- **Lionel Flandrin's PlayStation Emulation Guide** ([simias/psx-guide](https://github.com/simias/psx-guide)) —
  followed while the first subsystems were being built.

None of those documents are redistributed here; the `DOCS/…` citations resolve against a clone you
make yourself (`git clone https://github.com/psx-spx/psx-spx.github.io && ln -s psx-spx.github.io/docs DOCS`).
`THIRD-PARTY.md` is the full account, including the MIT components that *are* vendored.

## License

GPL-3.0-or-later. Every source file carries an SPDX header; `THIRD-PARTY.md` is the inventory of
components with other authors.
