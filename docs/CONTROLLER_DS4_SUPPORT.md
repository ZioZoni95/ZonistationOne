# PS4 (DualShock 4) Controller Support — Design Document

Status: **in implementation** — all three phases (Fase 1 host-side DS4, Fase 2 SIO analog protocol,
Fase 3 rumble) implemented, pending on-hardware verification (Section 10 test matrix).
Date: 2026-08-01
Scope: full controller support — DS4 over USB/Bluetooth on the host, *and* the DualShock analog
protocol on the SIO side so the sticks reach the console.

---

## 0. Implementation plan (phases)

The work splits into three independently-shippable phases. Each is testable at its boundary; the
order is chosen so that host input lands first (drop-in, no SIO change) and the SIO protocol work
builds on top of a controller state that already carries sticks.

| Phase | Scope | Deliverable | Depends on |
|-------|-------|-------------|------------|
| **1 — Host-side DS4** | `SDL_INIT_GAMECONTROLLER`, `SDL_GameController*` open/close on hot-plug, `controller_update()` folding DS4 + keyboard into the 16-bit PSX word, stick-to-dpad fallback, DS4 analog toggle | DS4 drives the digital pad (`0x41`) over USB or BT | — |
| **2 — SIO DualShock analog protocol** | SIO state: analog mode, config mode, LED/lock, sticks; ID `0x73` in analog / `0xF3` in config; 9-byte analog transfer (adc0-3); config commands `0x43`/`0x44`/`0x45`/`0x46`/`0x47`/`0x48`/`0x4C`; watchdog reset | Analog games (Gran Turismo, Crash) read real stick positions | Fase 1 (stick state) |
| **3 — Rumble** | Capture the M1/M2 bytes of the `0x42` command, route to `SDL_GameControllerRumble()` | Vibration in games that enable it via config mode | Fase 2 (config mode, M1/M2 bytes) |

Fase 1 and 2 are the "complete controller support" scope. Fase 3 is optional and only reachable
alongside the analog protocol (rumble is a DualShock feature, not digital).

---

## 0.1 Implementation todo (tracked live)

Status legend: `[ ]` pending · `[~]` in progress · `[x]` done. Updated as work lands; the phase
tables above and below define the scope, this list tracks the concrete steps.

### Fase 1 — Host-side DS4
- [x] `src/main.c` — `SDL_Init` gains `SDL_INIT_GAMECONTROLLER`
- [x] `include/controller.h` — struct: `SDL_GameController* gc`, `int16_t left_x/left_y/right_x/right_y`
- [x] `src/core/controller.c` — `controller_process_event()` (DEVICEADDED/REMOVED, open/close, log)
- [x] `src/core/controller.c` — `controller_update()` (DS4 fold-down + keyboard fallback, stick-to-dpad)
- [x] `src/main.c` — `controller_process_event()` in the SDL event drain
- [x] `src/main.c` — call-site: `controller_update(&gamepad)` (sticks folded into `ctrl`; consumed in Fase 2)

### Fase 2 — SIO DualShock analog protocol
- [x] `src/core/sio.c` — SioInternal state: `analog_mode`, `config_mode`, `analog_lock`, sticks, watchdog
- [x] `include/sio.h` + `src/core/sio.c` — `sio_set_analog_state()`, `sio_set_analog_mode()`, `sio_get_analog_mode()`
- [x] `src/core/sio.c` — ID dispatch: `0x41` digital / `0x73` analog / `0xF3` config
- [x] `src/core/sio.c` — analog 9-byte transfer (adc0-3 = rightX/rightY/leftX/leftY, centre `0x80`)
- [x] `src/core/sio.c` — config commands `0x43`/`0x44`/`0x45`/`0x46`/`0x47`/`0x48`/`0x4C`
- [x] `src/core/sio.c` — watchdog reset to digital (~1 s without comms)
- [x] `include/controller.h` + `src/core/controller.c` — DS4 touchpad-click → analog toggle edge
- [x] `src/main.c` — call-sites: analog-toggle edge + `sio_set_analog_state()` per frame

### Fase 3 — Rumble
- [x] `src/core/sio.c` — `rumble_map[6]` + `rumble_m1/m2` state, init locked (all `0xFF`)
- [x] `src/core/sio.c` — config `0x4D` get/set rumble map (reply = old values, store new)
- [x] `src/core/sio.c` — capture M1/M2 bytes from normal-mode `0x42` (`sio_capture_rumble`)
- [x] `src/core/sio.c` — watchdog reset disables + locks rumble (map → `0xFF`)
- [x] `include/sio.h` + `src/core/sio.c` — `sio_get_rumble()`
- [x] `src/core/controller.c` — `controller_update_rumble()` → `SDL_GameControllerRumble()` (M1→low, M2→high, 8→16-bit, re-fire while active)
- [x] `src/main.c` — per-frame `sio_get_rumble()` → `controller_update_rumble()`

### Cross-cutting
- [x] `make clean && make` clean build (Makefile has no header-dep tracking)
- [ ] Test matrix (Section 10): USB + BT boot, hot-plug, keyboard fallback, stick-to-dpad, analog read

---

## 1. Goal

Let a DualShock 4 drive the emulated PSX digital pad over either transport — USB cable or Bluetooth —
with zero code changes per transport. Hot-plug and hot-unplug must work live, and a DS4 should not
break the existing keyboard input.

## 2. Current input path (as of 2026-07-29)

```
src/main.c:72     SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)   ← no joystick/controller subsystem
src/main.c:346    controller_init(&gamepad)
src/main.c:347    sio_set_controller_connected(&inter.sio, true)
src/main.c:422    sio_set_button_state(&inter.sio, controller_update_from_keyboard(&gamepad))
                  ← per-frame, right after SDL_PollEvent drain

src/core/controller.c:24   controller_update_from_keyboard()  — polls SDL_GetKeyboardState,
                            folds into 16-bit PSX button word (0=pressed, 1=released)
src/core/sio.c:381         sio_set_button_state()             — stores into SioInternal.button_state
src/core/sio.c:580         sio_controller_transfer()          — digital pad packet (0x41 protocol)
```

The PSX button word layout is fixed by the SIO protocol (see `include/controller.h:7-9`):

```
0=SELECT 1=L3 2=R3 3=START 4=UP 5=RIGHT 6=DOWN 7=LEFT
8=L2 9=R2 10=L1 11=R1 12=TRIANGLE 13=CIRCLE 14=CROSS 15=SQUARE
```

Key facts that shape the design:

- Input is **polled once per frame**, not event-driven. State is consumed as a 16-bit word.
- The emulated pad is the **digital** pad (ID `0x41`). There is no DualShock analog-mode protocol
  (`0x73` config, extended packets) in `sio.c` today.
- `Controller` (`include/controller.h:11-13`) currently holds only a `connected` bool.

## 3. Why SDL's GameController API and not raw HID

SDL is already the only host-input dependency, and its `SDL_GameController*` API is the right layer:

- **One code path for both transports.** SDL's HIDAPI backend opens a DS4 over USB or Bluetooth with
  identical API; the driver handles the transport difference (HID reports, report IDs, the BT-only
  output report quirks). The game must not care which wire it arrived on.
- **Built-in mapping database.** DS4 has an entry in SDL's bundled `gamecontrollerdb`, so the buttons
  already map to `SDL_CONTROLLER_BUTTON_*` logical names without per-user config.
- **Hot-plug for free.** `SDL_CONTROLLERDEVICEADDED` / `SDL_CONTROLLERDEVICEREMOVED` fire on plug and
  unplug; all we must do is listen.
- **Future-proofing.** The same API covers XInput pads, Switch Pro, etc. later.

Raw HID (via `hidapi` or SDL's lower `SDL_Joystick`) would be needed only for DS4-specific extras that
the GameController API does not expose — lightbar, touchpad, gyro. Those are explicitly out of scope
(Section 11).

### Version requirement

- `SDL_GameControllerRumble()` needs **SDL ≥ 2.0.9** (optional, Section 9).
- `SDL_GameControllerSetLED()` needs **SDL ≥ 2.0.14** (not required — lightbar out of scope).
- Ubuntu/Debian `libsdl2-dev` in the current distros ships ≥ 2.0.20, so nothing extra to install.

## 4. Linux transport notes

### USB

DS4 over USB is a standard HID gamepad: vendor `0x054C` (Sony), product `0x05C4` (v1) / `0x09CC`
(v2). SDL enumerates it through the `evdev`/`hidapi` driver automatically once the OS has it.

Permission trap: if the user's account cannot open `/dev/input/event*` or `/dev/hidraw*`, SDL never
sees the pad. On most desktop distros the `input` group is enough:

```sh
sudo usermod -aG input $USER        # re-login required
```

`udev` rules (e.g. `/usr/lib/udev/rules.d/99-ds4.rules`) can also fix perms, but joining `input` is the
usual fix and is not specific to the emulator.

### Bluetooth

DS4 over BT appears as vendor `0x054C`, product `0x0CE0` (v1) / `0x0CE0` and `0x05C5` depending on
firmware revision; SDL's hidapi/bluez backend resolves it. Practical setup for the user:

1. Pair in the OS Bluetooth settings (hold **SHARE + PS** for 5 s until the lightbar flashes fast).
2. The pad exposes HID over GATT; SDL opens it via `hidraw` or BlueZ. This needs the same
   `input`/`hidraw` read permission as USB, plus the system Bluetooth stack running.
3. Reconnection after sleep is handled by the OS stack, not by the emulator.

### SDL hints that matter

- `SDL_HINT_JOYSTICK_HIDAPI` — leave default (`"1"`). This is what makes the PS4 HIDAPI backend active
  for both USB and BT.
- `SDL_HINT_JOYSTICK_HIDAPI_PS4` — leave default (`"1"`).
- `SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS` — default (`"0"`). A DS4 connected but with the emulator
  window unfocused will not feed input; that matches keyboard behaviour today. Set to `"1"` only if
  background play is wanted.
- Do **not** set `SDL_HINT_JOYSTICK_RAWINPUT` or the generic joystick-only hints; they can disable the
  HIDAPI/GameController path.

## 5. Proposed module changes

### 5.1 `SDL_Init` flags — `src/main.c:72`

Add the controller subsystem:

```c
SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER)
```

`SDL_INIT_GAMECONTROLLER` implies joystick init. No other SDL setup is required. Initializing the
subsystem here (rather than lazily) means `SDL_ControllerEvent*` event types are deliverable to the
poll loop immediately.

### 5.2 `Controller` struct — `include/controller.h`

The current struct is stateless. It needs to become:

```c
typedef struct {
    bool connected;              // host input enabled/disabled (existing)
    SDL_GameController* gc;      // active DS4; NULL when none present
    int16_t left_x, left_y;      // left stick raw (-32768..32767), for dpad emulation
    int16_t right_x, right_y;    // right stick raw (unused until analog protocol lands)
} Controller;
```

Do **not** put the per-button fold-down in this struct; keep the "produce a 16-bit PSX word" contract
that `sio_set_button_state()` already owns (mirror the current stateless design, `controller.h:18-21`).

### 5.3 `src/core/controller.c`

Two new responsibilities, matching the existing file's shape:

- **`controller_process_event(Controller*, const SDL_Event*)`** — consume `SDL_CONTROLLERDEVICEADDED` /
  `SDL_CONTROLLERDEVICEREMOVED` from the event drain, open/close `SDL_GameController*`, log each
  connect/disconnect via `LOG_SYSTEM_INFO` (matches the "Controller connected/disconnected" style at
  `controller.c:71-74`), and toggle `connected`.
- **`controller_update(Controller*)`** — a superset of `controller_update_from_keyboard`. If a DS4 is
  open, fold DS4 buttons + stick-to-dpad into the same 16-bit word; otherwise fall back to the
  keyboard path unchanged. Also folds the two sticks into `ctrl->left_x/left_y/right_x/right_y`
  (raw `-32768..32767`), which Fase 2 reads to feed the SIO analog bytes.

Alternative (cleaner but larger): a new `src/core/gamepad.c` module that owns DS4 state, with
`controller.c` delegating to it. The repo's module granularity (`sio.c`, `controller.c`, ...) makes a
separate module defensible; the minimal-diff path extends `controller.c` in place. Either is
acceptable — pick one and keep the keyboard fallback in `controller.c` either way.

### 5.4 Call-site — `src/main.c:422`

```c
sio_set_button_state(&inter.sio, controller_update(&gamepad));
```

`controller_update()` internally handles both DS4 and keyboard, so `main.c` does not branch. The
per-frame poll position is correct: it runs after the SDL event drain (`main.c:391-410`) and before
`system_run_frame` (`main.c:443`), so a freshly plugged pad takes effect on the next frame.

Note the SDL event drain at `main.c:391` only forwards events to `debug_ui_process_event` today; the
new `SDL_CONTROLLERDEVICE*` events must be consumed inside that same drain via
`controller_process_event()`.

## 6. Button / axis mapping (PSX word → DS4)

DS4 logical names are SDL's `SDL_GameControllerButton` / `SDL_GameControllerAxis`.

| PSX bit | PSX button | DS4 control | SDL constant |
|---|---|---|---|
| 0 | SELECT | Share | `SDL_CONTROLLER_BUTTON_BACK` |
| 1 | L3 | L stick click | `SDL_CONTROLLER_BUTTON_LEFTSTICK` |
| 2 | R3 | R stick click | `SDL_CONTROLLER_BUTTON_RIGHTSTICK` |
| 3 | START | Options | `SDL_CONTROLLER_BUTTON_START` |
| 4 | UP | D-pad up | `SDL_CONTROLLER_BUTTON_DPAD_UP` |
| 5 | RIGHT | D-pad right | `SDL_CONTROLLER_BUTTON_DPAD_RIGHT` |
| 6 | DOWN | D-pad down | `SDL_CONTROLLER_BUTTON_DPAD_DOWN` |
| 7 | LEFT | D-pad left | `SDL_CONTROLLER_BUTTON_DPAD_LEFT` |
| 8 | L2 | L2 trigger | `SDL_CONTROLLER_AXIS_TRIGGERLEFT` |
| 9 | R2 | R2 trigger | `SDL_CONTROLLER_AXIS_TRIGGERRIGHT` |
| 10 | L1 | L1 shoulder | `SDL_CONTROLLER_BUTTON_LEFTSHOULDER` |
| 11 | R1 | R1 shoulder | `SDL_CONTROLLER_BUTTON_RIGHTSHOULDER` |
| 12 | △ | Triangle | `SDL_CONTROLLER_BUTTON_Y` |
| 13 | ○ | Circle | `SDL_CONTROLLER_BUTTON_B` |
| 14 | × | Cross | `SDL_CONTROLLER_BUTTON_A` |
| 15 | □ | Square | `SDL_CONTROLLER_BUTTON_X` |

Unmapped by design: `SDL_CONTROLLER_BUTTON_GUIDE` (PS button), touchpad click
(`SDL_CONTROLLER_BUTTON_TOUCHPAD`, SDL 2.0.14+). They have no PSX digital equivalent — but the
touchpad click doubles as the **analog-mode toggle** (Fase 2): a real pad's Analog button is how a
user switches `0x41` digital ↔ `0x73` analog without software help, and the DS4 has no such button,
so the touchpad click is the natural stand-in.

Folding rule, identical in spirit to `controller.c:36-59`:

```c
if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_A)) buttons &= ~(1u << 14); // CROSS
// ...one line per row above...
```

### Stick-to-dpad fallback

Real PSX emulators map the left stick to the D-pad when the pad runs in digital mode. Apply the same:
treat a left-stick deflection past ~50% of travel as the corresponding D-pad direction.

```c
// axis range: -32768..32767; treat ±(16384..) as a press
if (SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTX) >  16384) buttons &= ~(1u << 5);
if (SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTX) < -16384) buttons &= ~(1u << 7);
if (SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTY) >  16384) buttons &= ~(1u << 6);
if (SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTY) < -16384) buttons &= ~(1u << 4);
```

Use a centre deadzone (|axis| < ~0x2000 ignore) so a worn stick does not hold a direction. The
triggers (`SDL_CONTROLLER_AXIS_TRIGGERLEFT/RIGHT`) are analog in the range 0..32767; treat any value
> ~16384 as pressed for L2/R2 bits 8/9, matching how Shift/Ctrl currently behave.

### Poll vs event

Prefer **state polling** (`SDL_GameControllerGetButton/GetAxis` once per frame) to match the existing
keyboard model — no button event bookkeeping, no missed presses, trivially folded into the 16-bit
word. SDL events are only required for device hot-plug, which is why only `SDL_CONTROLLERDEVICE*`
events are consumed.

## 7. Analogue sticks on the PSX side — Fase 2 scope

The emulated pad is the **digital** pad (`sio_controller_transfer`, `sio.c:580`, ID `0x41`). Its
packet has no stick data. Fase 2 adds the **analog/DualShock mode** protocol so true analogue play
works:

1. SIO speaks the **analog/DualShock mode** protocol: config mode via `0x43`, LED/analog-mode
   switch via `0x44`, and the extended 6-byte-payload `0x42` read with stick bytes (right/left
   `0x80 = centre`), per `DOCS/controllersandmemorycards.md:330-433` and :1202-1330.
2. `Controller` carries the two sticks as state (`left_x/left_y/right_x/right_y`) and the SIO
   transfer step machine emits the extra bytes.

Boot state: controllers are in digital mode (`0x41`, LED off, analog inputs disabled). A game
enables analog mode either by software (`0x43` enter config → `0x44` set LED=1) or the user via the
pad's analog toggle; we map a DS4 chord (or a keyboard key) to that toggle (Section 6 note).

## 8. Hot-plug and disconnect behaviour

- `SDL_CONTROLLERDEVICEADDED` (has `device` = instance index): `SDL_GameControllerOpen(idx)`, log
  `LOG_SYSTEM_INFO("[SYSTEM] Controller connected (DS4)")`, keep `connected = true`.
- `SDL_CONTROLLERDEVICEREMOVED`: `SDL_GameControllerClose(gc)`, `gc = NULL`, log disconnect. Leave the
  PSX-side pad connected (`sio_set_controller_connected`) so the BIOS/game sees a silent, unresponsive
  pad rather than a pad that vanished mid-poll; a missing response (`0xFF`) already reads as "no
  button pressed" to the protocol. Decide deliberately and document it in the code.
- If no controller is ever present, `controller_update()` must produce exactly what
  `controller_update_from_keyboard()` produces today — keyboard still works, unchanged.
- If the pad disconnects mid-transfer, the SIO state machine already tolerates a `0xFF`/no-ack reply
  (`sio.c:497-499`); nothing to change there.

## 9. Rumble (optional, SDL ≥ 2.0.9) — Fase 3

`SDL_GameControllerRumble(gc, low_freq, high_freq, duration_ms)` fires DS4 motors over both transports
with no extra code. Fase 3 captures the M1/M2 bytes of the `0x42` command (Fase 2 config-mode
`0x4D` unlock) and routes them here. Rumble is a DualShock feature, so it is only reachable alongside
the analog-mode work in Fase 2. Do not add a host-side rumble toggle in the emulator core.

## 10. Build & integration checklist

- Header `include/controller.h` gains the DS4 fields; because the Makefile has **no header dependency
  tracking** (see CLAUDE.md trap), run `make clean && make`, never an incremental build.
- No new libraries: `libsdl2-dev` already provides the GameController API.
- Test matrix:
  - USB DS4 boots to BIOS menu and drives the menu cursor (D-pad + Cross/Options).
  - Same over Bluetooth (pair first; verify `input`/`hidraw` permissions).
  - Plug/unplug USB mid-game: pad is picked up next frame, no crash, no stuck button.
  - Bluetooth sleep/wake: reconnect handled by OS; emulator re-opens on `DEVICEADDED`.
  - Keyboard still works with DS4 disconnected *and* with it connected.
  - No analog sticks required for the digital scope; L-stick must still steer D-pad.
  - Check SDL's own test: `SDL_GameControllerEvent` logs at `LOG_SYSTEM_TRACE` (never default builds).
- Measure speed with panels closed and **no** `ZS1_LOG_STDERR`/breakpoints — the CLAUDE.md
  instrumentation warning applies; gamepad polling is 6 `SDL_GameControllerGet*` calls per frame and
  should not move the frame budget.

## 11. Explicitly out of scope

- Lightbar colour/LED control (needs `SDL_GameControllerSetLED` or raw HID).
- Touchpad and gyro/IMU data.
- DS4 audio jack output.
- Multitap/analog pad protocol extensions beyond Fase 2 (Dualshock2 pressure sensing, `0x4F`
  reply-protocol).
- Memory-card slot handling (unchanged).

These would require raw HID or the SIO analog protocol extension and are tracked separately, not in
this change.
