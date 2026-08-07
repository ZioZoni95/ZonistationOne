/* SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 ZioZoni95
 *
 * Part of ZoniStation One, a PlayStation 1 emulator.
 * See LICENSE for the full licence text and THIRD-PARTY.md for the
 * components of this project that have other authors.
 */
#include "controller.h"
#include "log.h"
#include <SDL3/SDL.h>
#include <stdlib.h>
#include <string.h>

static Controller* g_active_controller = NULL;

void controller_init(Controller* ctrl) {
    memset(ctrl, 0, sizeof(Controller));
    ctrl->connected = true;       // Controller enabled by default
    ctrl->key_map[0]  = SDL_SCANCODE_TAB;        // SELECT
    ctrl->key_map[1]  = 0;                       // L3
    ctrl->key_map[2]  = 0;                       // R3
    ctrl->key_map[3]  = SDL_SCANCODE_SPACE;      // START
    ctrl->key_map[4]  = SDL_SCANCODE_W;          // UP
    ctrl->key_map[5]  = SDL_SCANCODE_D;          // RIGHT
    ctrl->key_map[6]  = SDL_SCANCODE_S;          // DOWN
    ctrl->key_map[7]  = SDL_SCANCODE_A;          // LEFT
    ctrl->key_map[8]  = SDL_SCANCODE_LSHIFT;     // L2
    ctrl->key_map[9]  = SDL_SCANCODE_LCTRL;      // R2
    ctrl->key_map[10] = SDL_SCANCODE_Q;          // L1
    ctrl->key_map[11] = SDL_SCANCODE_R;          // R1
    ctrl->key_map[12] = SDL_SCANCODE_E;          // TRIANGLE
    ctrl->key_map[13] = SDL_SCANCODE_C;          // CIRCLE
    ctrl->key_map[14] = SDL_SCANCODE_Z;          // CROSS
    ctrl->key_map[15] = SDL_SCANCODE_X;          // SQUARE
    ctrl->led_rgb = -1;           // nothing pushed yet; 0 would mean "black, already sent"
    {
        const char* env = getenv("ZS1_PAD_SWAP_XO");
        ctrl->swap_cross_circle = (env && env[0] == '1');
    }
    g_active_controller = ctrl;
    LOG_SYSTEM_INFO("[SYSTEM] Controller initialized (custom key mapping ready)");
}

Controller* controller_get_active(void) {
    return g_active_controller;
}

/**
 * Fold the DS4's buttons + left-stick-to-dpad into a PSX button word.
 *
 * Button mapping (PSX bit → DS4 logical control, see include/controller.h:6-9):
 * 0=SELECT Share · 1=L3 L-stick click · 2=R3 R-stick click · 3=START Options
 * 4-7 D-pad · 8/9 L2/R2 triggers · 10/11 L1/R1 · 12=△ Y · 13=○ B · 14=× A · 15=□ X
 *
 * L-stick deflection past 16384 — half of full travel — presses the matching D-pad
 * direction. That threshold is itself the deadzone: anything closer to centre than
 * half travel presses nothing, so a worn stick cannot hold a direction. The fold
 * only happens while the emulated pad is in digital mode: in analog or stick mode
 * the game reads the stick from adc0-3, and folding it onto the D-pad as well
 * would make every push arrive twice.
 */
static uint16_t controller_update_from_gamepad(Controller* ctrl) {
    SDL_Gamepad* gc = ctrl->gc;
    uint16_t buttons = 0xFFFF;

    if (SDL_GetGamepadButton(gc, SDL_GAMEPAD_BUTTON_BACK))        buttons &= ~(1u << 0);  // SELECT
    if (SDL_GetGamepadButton(gc, SDL_GAMEPAD_BUTTON_LEFT_STICK))   buttons &= ~(1u << 1);  // L3
    if (SDL_GetGamepadButton(gc, SDL_GAMEPAD_BUTTON_RIGHT_STICK))  buttons &= ~(1u << 2);  // R3
    if (SDL_GetGamepadButton(gc, SDL_GAMEPAD_BUTTON_START))       buttons &= ~(1u << 3);  // START
    if (SDL_GetGamepadButton(gc, SDL_GAMEPAD_BUTTON_DPAD_UP))     buttons &= ~(1u << 4);  // UP
    if (SDL_GetGamepadButton(gc, SDL_GAMEPAD_BUTTON_DPAD_RIGHT))  buttons &= ~(1u << 5);  // RIGHT
    if (SDL_GetGamepadButton(gc, SDL_GAMEPAD_BUTTON_DPAD_DOWN))   buttons &= ~(1u << 6);  // DOWN
    if (SDL_GetGamepadButton(gc, SDL_GAMEPAD_BUTTON_DPAD_LEFT))   buttons &= ~(1u << 7);  // LEFT
    if (SDL_GetGamepadAxis(gc, SDL_GAMEPAD_AXIS_LEFT_TRIGGER)  >  16384) buttons &= ~(1u << 8);   // L2
    if (SDL_GetGamepadAxis(gc, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) >  16384) buttons &= ~(1u << 9);   // R2
    if (SDL_GetGamepadButton(gc, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER)) buttons &= ~(1u << 10);  // L1
    if (SDL_GetGamepadButton(gc, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER)) buttons &= ~(1u << 11); // R1
    if (SDL_GetGamepadButton(gc, SDL_GAMEPAD_BUTTON_NORTH))            buttons &= ~(1u << 12);  // TRIANGLE
    if (SDL_GetGamepadButton(gc, SDL_GAMEPAD_BUTTON_EAST))            buttons &= ~(1u << 13);  // CIRCLE
    if (SDL_GetGamepadButton(gc, SDL_GAMEPAD_BUTTON_SOUTH))            buttons &= ~(1u << 14);  // CROSS
    if (SDL_GetGamepadButton(gc, SDL_GAMEPAD_BUTTON_WEST))            buttons &= ~(1u << 15);  // SQUARE

    // Left-stick-to-dpad fallback with centre deadzone — digital mode only.
    if (!ctrl->analog_active) {
        int16_t lx = SDL_GetGamepadAxis(gc, SDL_GAMEPAD_AXIS_LEFTX);
        int16_t ly = SDL_GetGamepadAxis(gc, SDL_GAMEPAD_AXIS_LEFTY);
        if (lx >  16384) buttons &= ~(1u << 5);  // RIGHT
        if (lx < -16384) buttons &= ~(1u << 7);  // LEFT
        if (ly >  16384) buttons &= ~(1u << 6);  // DOWN
        if (ly < -16384) buttons &= ~(1u << 4);  // UP
    }

    // Log button state changes (mirror the keyboard path below).
    static uint16_t last_logged_state = 0xFFFF;
    if (buttons != last_logged_state) {
        LOG_SYSTEM_DEBUG("[SYSTEM] Controller buttons (gamepad): 0x%04x (was 0x%04x)", buttons, last_logged_state);
        last_logged_state = buttons;
    }

    return buttons;
}

void controller_process_event(Controller* ctrl, const SDL_Event* ev) {
    if (ev->type == SDL_EVENT_GAMEPAD_ADDED) {
        if (ctrl->gc) return;  // first pad wins
        ctrl->gc = SDL_OpenGamepad(ev->gdevice.which);
        if (ctrl->gc) {
            ctrl->connected = true;
            /* Asked once, here, rather than inferred from a failing write every
             * frame: a pad with no light bar must not be written to at all. */
            ctrl->gc_has_led = SDL_GetBooleanProperty(SDL_GetGamepadProperties(ctrl->gc),
                                                      SDL_PROP_GAMEPAD_CAP_RGB_LED_BOOLEAN,
                                                      false);
            ctrl->led_rgb = -1;
            LOG_SYSTEM_INFO("[SYSTEM] Controller connected (DS4)%s",
                            ctrl->gc_has_led ? ", light bar available" : "");
        } else {
            LOG_SYSTEM_WARN("[SYSTEM] Controller open failed: %s", SDL_GetError());
        }
    } else if (ev->type == SDL_EVENT_GAMEPAD_REMOVED) {
        /* The PSX-side pad stays connected: the BIOS/game sees a silent,
         * unresponsive pad rather than one that vanished mid-poll; a missing
         * response (0xFF) already reads as "no button pressed" to the protocol. */
        SDL_Gamepad* closing = ctrl->gc;
        ctrl->gc = NULL;
        ctrl->gc_has_led = false;
        ctrl->led_rgb = -1;
        if (closing) SDL_CloseGamepad(closing);
        LOG_SYSTEM_INFO("[SYSTEM] Controller disconnected");
    } else if (ev->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN &&
               ev->gbutton.button == SDL_GAMEPAD_BUTTON_TOUCHPAD) {
        // DS4 touchpad click is the stand-in for the Analog button that real
        // analog pads carry (DOCS/controllersandmemorycards.md:437-440, which
        // calls a manual toggle essential). Latched here rather than polled:
        // controller_update() runs more than once per frame — the debug UI polls
        // it too — and an edge detected inside the poll is lost to whichever
        // caller happens to run second.
        ctrl->analog_toggle = true;
    }
}

bool controller_take_analog_toggle(Controller* ctrl) {
    bool pressed = ctrl->analog_toggle;
    ctrl->analog_toggle = false;
    return pressed;
}

/**
 * Radial deadzone around the stick centre, applied to the raw SDL pair.
 *
 * A resting DS4 stick does not read exactly 0, and every non-zero value here
 * becomes an adc byte away from 80h — which the game reads as the player holding
 * the stick slightly off-centre for the whole session. The documented resting
 * spread on real pads is wide (DOCS/controllersandmemorycards.md:472-476: mid
 * values from 6Ch to ACh), so games tolerate a dead area; what they do not
 * tolerate is a permanent lean. Below the threshold both axes are pinned to
 * centre; above it the vector is rescaled so travel still starts from zero.
 */
#define STICK_DEADZONE 3000

static void apply_deadzone(int16_t* x, int16_t* y) {
    float fx = (float)*x, fy = (float)*y;
    float mag = SDL_sqrtf(fx * fx + fy * fy);
    if (mag < (float)STICK_DEADZONE) {
        *x = 0;
        *y = 0;
        return;
    }
    float scaled = (mag - STICK_DEADZONE) / (32767.0f - STICK_DEADZONE);
    if (scaled > 1.0f) scaled = 1.0f;
    float k = scaled * 32767.0f / mag;
    fx *= k;
    fy *= k;
    if (fx < -32768.0f) fx = -32768.0f;
    if (fx >  32767.0f) fx =  32767.0f;
    if (fy < -32768.0f) fy = -32768.0f;
    if (fy >  32767.0f) fy =  32767.0f;
    *x = (int16_t)fx;
    *y = (int16_t)fy;
}

/**
 * Optional × / ○ swap, applied to the finished word.
 *
 * The default mapping is positional and matches the hardware bit layout
 * (DOCS/controllersandmemorycards.md:405-421): the pad's bottom button is bit 14
 * Cross, its right button bit 13 Circle. What differs by region is not the pad
 * but the software on top of it — the PS1 shell and most Japanese-developed
 * titles confirm with ○, which feels inverted to anyone used to × confirming.
 * This swaps which physical button drives which bit, for players who want the
 * Western convention on software that does not follow it. Off by default: the
 * honest thing is to report the button that was actually pressed.
 */
static uint16_t apply_cross_circle_swap(const Controller* ctrl, uint16_t buttons) {
    if (!ctrl->swap_cross_circle) return buttons;

    uint16_t circle = (buttons >> 13) & 1u;  // bit 13 ()
    uint16_t cross  = (buttons >> 14) & 1u;  // bit 14 ><
    buttons &= (uint16_t)~((1u << 13) | (1u << 14));
    buttons |= (uint16_t)(cross << 13);
    buttons |= (uint16_t)(circle << 14);
    return buttons;
}

uint16_t controller_update(Controller* ctrl) {
    if (!ctrl->connected) {
        ctrl->left_x = ctrl->left_y = ctrl->right_x = ctrl->right_y = 0;
        return 0xFFFF;  // All released if disconnected
    }

    if (ctrl->gc) {
        ctrl->left_x  = SDL_GetGamepadAxis(ctrl->gc, SDL_GAMEPAD_AXIS_LEFTX);
        ctrl->left_y  = SDL_GetGamepadAxis(ctrl->gc, SDL_GAMEPAD_AXIS_LEFTY);
        ctrl->right_x = SDL_GetGamepadAxis(ctrl->gc, SDL_GAMEPAD_AXIS_RIGHTX);
        ctrl->right_y = SDL_GetGamepadAxis(ctrl->gc, SDL_GAMEPAD_AXIS_RIGHTY);
        apply_deadzone(&ctrl->left_x,  &ctrl->left_y);
        apply_deadzone(&ctrl->right_x, &ctrl->right_y);

        // Both inputs drive the same pad. The word is active-low, so ANDing the
        // two sources unions their presses: a button held on either device reads
        // as pressed, and neither can un-press the other. Keeping the keyboard
        // live alongside a connected DS4 is what lets the pad be plugged in
        // mid-session without the keyboard going dead under the user's hands.
        return apply_cross_circle_swap(
            ctrl, controller_update_from_gamepad(ctrl) & controller_update_from_keyboard(ctrl));
    }

    // No DS4: keyboard path unchanged (must produce exactly what the old
    // controller_update_from_keyboard() produced as the sole input source).
    ctrl->left_x = ctrl->left_y = ctrl->right_x = ctrl->right_y = 0;
    return apply_cross_circle_swap(ctrl, controller_update_from_keyboard(ctrl));
}

/**
 * Route SIO rumble levels to the DS4. M1 (large motor, analog slow/fast) maps to
 * SDL's low-frequency channel, M2 (small motor, digital) to the high-frequency
 * channel, both scaled from 8-bit to 16-bit. SDL_RumbleGamepad has a
 * finite duration, so re-fire every frame while a motor is active — games send
 * M1/M2 on every 42h read while vibrating, so the state only decays when the
 * game actually stops. Stop once on the transition to both-zero.
 */
void controller_update_rumble(Controller* ctrl, uint8_t m1, uint8_t m2) {
    if (!ctrl->gc) {
        ctrl->rumble_active = false;
        return;
    }

    if (m1 == 0 && m2 == 0) {
        if (ctrl->rumble_active) {
            SDL_RumbleGamepad(ctrl->gc, 0, 0, 1);
            ctrl->rumble_active = false;
        }
        return;
    }

    uint16_t low  = (uint16_t)(((uint16_t)m1 << 8) | m1);  // 8-bit → 0..0xFFFF
    uint16_t high = m2 ? 0xFFFF : 0x0000;
    SDL_RumbleGamepad(ctrl->gc, low, high, 200);
    ctrl->rumble_active = true;
}

/**
 * Push a light-bar colour, at most once per change.
 *
 * The caller decides the colour from the emulated pad's mode; the documented
 * mapping is in controller.h. Kept here rather than in the caller's loop because
 * the "only on change" part is the whole point: SDL_SetGamepadLED sends a HID
 * report, and a report per frame over Bluetooth competes with the input reports
 * coming the other way.
 */
void controller_set_led(Controller* ctrl, uint8_t r, uint8_t g, uint8_t b) {
    if (!ctrl->gc || !ctrl->gc_has_led) return;

    int32_t packed = (int32_t)(((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b);
    if (ctrl->led_rgb == packed) return;

    if (SDL_SetGamepadLED(ctrl->gc, r, g, b)) {
        ctrl->led_rgb = packed;
    } else {
        /* Stop asking: the capability said yes but the write does not work. */
        ctrl->gc_has_led = false;
        LOG_SYSTEM_WARN("[SYSTEM] Light bar write failed: %s — LED disabled", SDL_GetError());
    }
}

/**
 * Poll SDL keyboard and map to PSX controller buttons
 *
 * Keyboard Mapping:
 * W/A/S/D      → UP/LEFT/DOWN/RIGHT
 * SPACE        → START
 * E/C/Z/X      → △/○/×/□ (Triangle/Circle/Cross/Square)
 * Q/R          → L1/R1 (Left/Right shoulder)
 * SHIFT/CTRL   → L2/R2 (Left/Right trigger)
 *
 * Returns: 16-bit button state (0=pressed, 1=released)
 */
uint16_t controller_update_from_keyboard(Controller* ctrl) {
    if (!ctrl->connected) {
        return 0xFFFF;  // All released if disconnected
    }

    uint16_t buttons = 0xFFFF;
    const bool* keys = SDL_GetKeyboardState(NULL);

    for (int i = 0; i < 16; i++) {
        int scancode = ctrl->key_map[i];
        if (scancode > 0 && scancode < SDL_SCANCODE_COUNT && keys[scancode]) {
            buttons &= ~(1u << i);
        }
    }
    // Secondary scancode fallbacks for triggers & select
    if (ctrl->key_map[8] == SDL_SCANCODE_LSHIFT && keys[SDL_SCANCODE_RSHIFT]) buttons &= ~(1u << 8);
    if (ctrl->key_map[9] == SDL_SCANCODE_LCTRL && keys[SDL_SCANCODE_RCTRL])   buttons &= ~(1u << 9);
    if (ctrl->key_map[0] == SDL_SCANCODE_TAB && keys[SDL_SCANCODE_BACKSPACE]) buttons &= ~(1u << 0);

    // Log button state changes
    static uint16_t last_logged_state = 0xFFFF;
    if (buttons != last_logged_state) {
        LOG_SYSTEM_DEBUG("[SYSTEM] Controller buttons: 0x%04x (was 0x%04x)", buttons, last_logged_state);
        last_logged_state = buttons;
    }

    return buttons;
}

void controller_set_connected(Controller* ctrl, bool connected) {
    ctrl->connected = connected;
    LOG_SYSTEM_INFO("[SYSTEM] Controller %s", connected ? "connected" : "disconnected");
}
