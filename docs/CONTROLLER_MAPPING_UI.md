# Controller Mapping Configuration UI — Design Document

Status: **planned** — design only, no code yet. Phases below; nothing is implemented.
Date: 2026-08-02
Scope: a runtime-editable controller/keyboard mapping, persisted to a config file, exposed through a
new mode of the debug interface. Follows on from `CONTROLLER_DS4_SUPPORT.md` (Fase 1-3 shipped).

---

## 1. Goal

Let the user remap which host key/button drives each emulated PSX button, and have that choice
survive a restart. Today both mappings are hardcoded:

- `controller_update_from_keyboard()` — `src/core/controller.c:31` (WASD, E/C/Z/X, Q/R, Shift/Ctrl,
  Space, Backspace)
- `controller_update_from_gamepad()` — `src/core/controller.c:60` (SDL `SDL_CONTROLLER_BUTTON_*` /
  `SDL_CONTROLLER_AXIS_*` → PSX bit)

The two mappings share one shape (a PSX 16-bit button word with the bit layout at
`include/controller.h:15-17`) but are expressed as two inline if-chains. This change makes the
mapping a first-class, persisted, UI-editable table without changing the input contract that
`controller_update()` and `sio_set_button_state()` already own.

## 2. Current state (as of 2026-08-02)

```
src/core/controller.c:31    controller_update_from_keyboard() — hardcoded SDL_SCANCODE_* → PSX bit
src/core/controller.c:60    controller_update_from_gamepad()  — hardcoded SDL_CONTROLLER_BUTTON_* /
                            SDL_CONTROLLER_AXIS_* → PSX bit, plus stick-to-dpad, touchpad→analog
src/main.c:430              sio_set_button_state(&inter.sio, controller_update(&gamepad))
src/debug_ui.cpp:182-198    g_modes[] rail, 8 modes (F1..F8); MODE_COUNT at 8
src/debug_ui.cpp:271-277    g_show_* window flags (no input/config window today)
```

Facts that shape the design:

- The project has **no config-file infrastructure** — the only external configuration is env vars
  (`ZS1_LOG_*`, `ZS1_LUA_SCRIPT`, `ZS1_DUMP_FRAME`, ...) read in `src/main.c:277-294`. A mapping
  file is the first persisted runtime config, so the format should be boring and self-contained.
- The debug UI is C++ (ImGui) in `src/debug_ui.cpp`; the core is C99. The mapping must live in C
  (`include/controller.h` + `src/core/controller.c`) and be *viewed* from C++.
- Input is **polled once per frame** from raw SDL state — not event-driven (except hot-plug). The
  rebind UI must therefore do its own edge detection, and must not let a "capture a key" press leak
  into the emulated pad.
- The Makefile has **no header-dependency tracking** — after any struct change, `make clean && make`
  is mandatory (CLAUDE.md trap).
- `include/renderer.h` is CRLF; `include/controller.h` is LF. Do not convert.

## 3. Design

### 3.1 Mapping model — `include/controller.h`

Two binding sources per PSX button, one table indexed by the PSX bit (0..15):

```c
typedef struct {
    SDL_Scancode key;            // keyboard source; SDL_SCANCODE_UNKNOWN = unbound
} InputKeyBinding;

typedef struct {
    int kind;                    // 0=unbound, 1=button, 2=axis
    int control;                 // SDL_GameControllerButton or SDL_GameControllerAxis
    bool neg;                    // axis: negative direction (stick up/left)
    int threshold;               // axis: |value| >= threshold reads as pressed (default 16384)
} InputPadBinding;

typedef struct {
    InputKeyBinding key;
    InputPadBinding pad;
} InputBinding;

typedef struct {
    InputBinding bind[16];       // index = PSX button bit (0=SELECT .. 15=SQUARE)
    InputBinding dpad_left[2];   // left stick → D-pad fallback (axis, ±): LEFT, RIGHT
    InputBinding dpad_vert[2];   // left stick → D-pad fallback (axis, ±): UP, DOWN
} InputMapping;
```

Rationale for a table instead of function pointers or enums: the two existing if-chains map a source
to a bit; a table indexed by bit is the direct inversion, it is trivial to render in ImGui, and it
serialises one-to-one. The stick-to-dpad fallback (Fase 1) is a special case (an *axis* driving
D-pad bits) — it gets its own four slots so the UI can rebind "left stick = D-pad" too, or leave it
unbound.

Defaults are the exact current hardcoded values:

| PSX bit | Button | Keyboard default | Pad default |
|---|---|---|---|
| 0 | SELECT | Backspace | `SDL_CONTROLLER_BUTTON_BACK` |
| 1 | L3 | — | `SDL_CONTROLLER_BUTTON_LEFTSTICK` |
| 2 | R3 | — | `SDL_CONTROLLER_BUTTON_RIGHTSTICK` |
| 3 | START | Space | `SDL_CONTROLLER_BUTTON_START` |
| 4 | UP | W | `SDL_CONTROLLER_BUTTON_DPAD_UP` |
| 5 | RIGHT | D | `SDL_CONTROLLER_BUTTON_DPAD_RIGHT` |
| 6 | DOWN | S | `SDL_CONTROLLER_BUTTON_DPAD_DOWN` |
| 7 | LEFT | A | `SDL_CONTROLLER_BUTTON_DPAD_LEFT` |
| 8 | L2 | Shift | `SDL_CONTROLLER_AXIS_TRIGGERLEFT` (axis, threshold 16384) |
| 9 | R2 | Ctrl | `SDL_CONTROLLER_AXIS_TRIGGERRIGHT` (axis, threshold 16384) |
| 10 | L1 | Q | `SDL_CONTROLLER_BUTTON_LEFTSHOULDER` |
| 11 | R1 | R | `SDL_CONTROLLER_BUTTON_RIGHTSHOULDER` |
| 12 | TRIANGLE | E | `SDL_CONTROLLER_BUTTON_Y` |
| 13 | CIRCLE | C | `SDL_CONTROLLER_BUTTON_B` |
| 14 | CROSS | Z | `SDL_CONTROLLER_BUTTON_A` |
| 15 | SQUARE | X | `SDL_CONTROLLER_BUTTON_X` |

D-pad fallback defaults: LEFTX ±, LEFTY ±, threshold 16384 — identical to
`controller_update_from_gamepad()` today.

### 3.2 Controller-side refactor — `src/core/controller.c`

`controller_update_from_keyboard()` and `controller_update_from_gamepad()` become loops over
`InputMapping`:

- keyboard: for each bound bit, `SDL_GetKeyboardState(NULL)[bind->key.key]` → clear bit.
- gamepad: for each bound bit, `SDL_GameControllerGetButton(gc, button)` or a thresholded axis read.
- the four D-pad fallback slots fold axes onto D-pad bits as today (after the button pass, so a
  real D-pad press and a stick push can both hold a direction).

`controller_update()` keeps its signature and the DS4/keyboard fallback logic unchanged. The
touchpad→analog toggle and the rumble routing are **not** remappable (documented, out of scope) —
they are host conveniences, not PSX buttons.

The mapping is owned by `Controller` (embedded, no malloc per project convention) and default-
initialised by `controller_init()`. A `ControllerMapping *` is exposed for the UI and the loader:

```c
void controller_set_mapping(Controller* ctrl, const InputMapping* map);   // copy in
const InputMapping* controller_get_mapping(const Controller* ctrl);       // read-only view
```

### 3.3 Config file — first persisted config

Format: a minimal `key = value` text file, one binding per line, `#` comments, no sections. The
parser is hand-rolled (the project vendors Lua but does not use it for config; a ~120-line
`strtok`-free parser is the right size). File: `controller.ini` in the working directory, overridden
by `ZS1_CONTROLLER_MAP` (matches the existing `ZS1_*` env-var convention).

```
# ZoniStation One controller mapping (PSX button = host source)
select = key:backspace pad:back
cross  = pad:a
up     = key:w pad:up
leftstick_dpad_left = axis:leftx:neg
leftstick_dpad_up   = axis:lefty:neg
```

Source grammar:

- `key:<scancode>` — `SDL_GetScancodeName()`/`SDL_GetScancodeFromName()` ("w", "space", "return", ...).
- `pad:<button>` — `SDL_GameControllerGetStringForButton()`/`GetButtonFromString()` ("a", "b",
  "leftstick", "back", ...).
- `pad:axis:<name>[:neg]` — `SDL_GameControllerGetStringForAxis()`/`GetAxisFromString()` ("leftx",
  "righty", "triggerleft", ...); optional `neg` for the negative direction; threshold from a
  following `:threshold=<n>` (default 16384).

Load: `bool controller_load_mapping(const char* path, InputMapping* out)` — missing file → defaults
(left in place, no error). Save: `bool controller_save_mapping(const char* path, const InputMapping* in)`.

`src/main.c` loads once at startup (after `controller_init`, before the loop) and re-saves from the
UI. If the file is unparseable, fall back to defaults and log at WARN.

### 3.4 Debug UI — a new `MODE_INPUT`

The mode rail (`docs/ui/README.md` direction) gains a ninth mode **Input** (F9), consistent with the
existing `g_modes[]` / `MODE_*` pattern at `debug_ui.cpp:182-198`. The panel shows:

- **PSX pad grid** — 16 rows (SELECT..SQUARE), each with the PSX name, the keyboard binding, and the
  pad binding, plus a "bind" action. Clicking a row's "bind" starts *capture*: the row is
  highlighted, and the next host key press / pad button press / stick deflection assigns that source
  to the bit. `Esc` cancels. An "unbind" clears a source without replacing it.
- **Stick-to-dpad** — the four fallback slots (left-stick → D-pad), captured the same way with an
  axis-threshold input.
- **Live readout** — the currently-pressed PSX word and the four analog bytes, so the user sees the
  mapping working before touching a game.
- **Actions** — Reset to defaults, Save, Load, and the current file path.

Cross-thread / ownership: `debug_ui_render()` already receives `cpu` and `inter` pointers; the
mapping pointer is passed in the same call (new param or a small setter like
`debug_ui_set_machine_info()`). The UI reads the mapping through `controller_get_mapping()` and
writes through a save-to-path call — no new locking, the UI runs on the same thread as the input
poll.

**Capture-vs-emulation hazard (the one real trap).** The emulated pad polls raw SDL state every
frame, so a key pressed to bind a button would *also* be delivered to the pad. Mitigation: while
capture is active, the panel sets a `capturing` flag read by `main.c`, which then feeds `0xFFFF` (all
released) to `sio_set_button_state()` for that frame — the same trick the UI already uses for
F5/F8/savestates (`src/main.c:411-416`). The capture is single-shot: one source per click.

## 4. Implementation phases

Ordered so each phase is independently shippable and behaviour-neutral until the last.

### Phase A — mapping tables (refactor, zero UI)
- [ ] `include/controller.h` — `InputMapping`/`InputBinding` structs + defaults comment block
- [ ] `src/core/controller.c` — `controller_init()` fills defaults; `controller_set_mapping()`/`get_mapping()`
- [ ] `src/core/controller.c` — rewrite `controller_update_from_keyboard()` / `controller_update_from_gamepad()` as table loops (behaviour identical to today)
- [ ] `make clean && make` + boot smoke test (menu cursor via keyboard = no regression)

### Phase B — persistence
- [ ] `src/core/controller.c` — `controller_load_mapping()` / `controller_save_mapping()` + the `.ini` parser/serialiser
- [ ] `src/main.c` — load after `controller_init()`, honour `ZS1_CONTROLLER_MAP`, WARN on bad file
- [ ] `make clean && make` + round-trip test (save → relaunch → same mapping)

### Phase C — MODE_INPUT UI
- [ ] `src/debug_ui.cpp` — ninth mode `MODE_INPUT` (F9) in `g_modes[]`; rail + stage dispatch
- [ ] `src/debug_ui.cpp` — panel: PSX grid, capture flow (key/button/axis), Esc-cancel, unbind
- [ ] `src/debug_ui.cpp` — stick-to-dpad slots + live readout + Reset/Save/Load buttons
- [ ] `src/main.c` — pass mapping pointer to the UI; `capturing` flag → feed `0xFFFF` to SIO
- [ ] `make clean && make` + manual test matrix (Section 6)

## 5. File inventory

| File | Change |
|---|---|
| `include/controller.h` | structs, defaults, 3 new prototypes |
| `src/core/controller.c` | table refactor, load/save, set/get mapping |
| `src/debug_ui.cpp` | `MODE_INPUT` + panel |
| `src/main.c` | load at startup, UI pointer + `capturing` gate |
| `docs/CONTROLLER_MAPPING_UI.md` | this document (tracked live in Section 4) |

No new libraries. `libsdl2-dev` already provides `SDL_GetScancodeName`, `SDL_GetKeyName`,
`SDL_GameControllerGetStringForButton/ForAxis` (SDL 2.0.9+; system has 2.30.0).

## 6. Build & test matrix

- `make clean && make` after every phase (header-dep trap).
- Keyboard-only regression: BIOS menu cursor (D-pad + Cross) unchanged with a fresh config.
- Rebind keyboard: bind a key, save, relaunch, confirm it holds.
- Rebind pad: bind a button and an axis (stick/tigger) with a DS4, confirm in the live readout.
- Capture isolation: bind a key while the emulator runs — the emulated pad must NOT see the press
  during capture.
- Reset to defaults restores the stock table.
- Corrupt config file → WARN log + defaults, emulator still boots.
- Measure speed with panels closed (CLAUDE.md instrumentation warning); the mapping is 16+4
  `SDL_GameControllerGet*`/`GetKeyboardState` reads per frame — the same count as today.

## 7. Explicitly out of scope

- Remapping the touchpad→analog toggle and rumble routing (host conveniences, hardwired).
- Analog deadzone / sensitivity configuration (threshold is per-axis binding only).
- Per-game profiles or game-detection-driven switching.
- Config for the emulator beyond the controller mapping (first persisted config only).
- Controller hotkey bindings (pause, savestates) — those are UI keys in `debug_ui.cpp`, not PSX
  inputs.
- Multitap per-slot mapping.
