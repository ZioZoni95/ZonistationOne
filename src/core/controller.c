/* SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 ZioZoni95
 *
 * Part of ZoniStation One, a PlayStation 1 emulator.
 * See LICENSE for the full licence text and THIRD-PARTY.md for the
 * components of this project that have other authors.
 */
#include "controller.h"
#include "log.h"
#include <SDL2/SDL.h>
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
 * half travel presses nothing, so a worn stick cannot hold a direction.
 */
static uint16_t controller_update_from_gamepad(Controller* ctrl) {
    SDL_GameController* gc = ctrl->gc;
    uint16_t buttons = 0xFFFF;

    if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_BACK))        buttons &= ~(1u << 0);  // SELECT
    if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_LEFTSTICK))   buttons &= ~(1u << 1);  // L3
    if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_RIGHTSTICK))  buttons &= ~(1u << 2);  // R3
    if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_START))       buttons &= ~(1u << 3);  // START
    if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_DPAD_UP))     buttons &= ~(1u << 4);  // UP
    if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_DPAD_RIGHT))  buttons &= ~(1u << 5);  // RIGHT
    if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_DPAD_DOWN))   buttons &= ~(1u << 6);  // DOWN
    if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_DPAD_LEFT))   buttons &= ~(1u << 7);  // LEFT
    if (SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_TRIGGERLEFT)  >  16384) buttons &= ~(1u << 8);   // L2
    if (SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) >  16384) buttons &= ~(1u << 9);   // R2
    if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_LEFTSHOULDER)) buttons &= ~(1u << 10);  // L1
    if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)) buttons &= ~(1u << 11); // R1
    if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_Y))            buttons &= ~(1u << 12);  // TRIANGLE
    if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_B))            buttons &= ~(1u << 13);  // CIRCLE
    if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_A))            buttons &= ~(1u << 14);  // CROSS
    if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_X))            buttons &= ~(1u << 15);  // SQUARE

    // Left-stick-to-dpad fallback with centre deadzone.
    int16_t lx = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTX);
    int16_t ly = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTY);
    if (lx >  16384) buttons &= ~(1u << 5);  // RIGHT
    if (lx < -16384) buttons &= ~(1u << 7);  // LEFT
    if (ly >  16384) buttons &= ~(1u << 6);  // DOWN
    if (ly < -16384) buttons &= ~(1u << 4);  // UP

    // Log button state changes (mirror the keyboard path below).
    static uint16_t last_logged_state = 0xFFFF;
    if (buttons != last_logged_state) {
        LOG_SYSTEM_DEBUG("[SYSTEM] Controller buttons (gamepad): 0x%04x (was 0x%04x)", buttons, last_logged_state);
        last_logged_state = buttons;
    }

    return buttons;
}

void controller_process_event(Controller* ctrl, const SDL_Event* ev) {
    if (ev->type == SDL_CONTROLLERDEVICEADDED) {
        if (ctrl->gc) return;  // first pad wins
        ctrl->gc = SDL_GameControllerOpen(ev->cdevice.which);
        if (ctrl->gc) {
            ctrl->connected = true;
            LOG_SYSTEM_INFO("[SYSTEM] Controller connected (DS4)");
        } else {
            LOG_SYSTEM_WARN("[SYSTEM] Controller open failed: %s", SDL_GetError());
        }
    } else if (ev->type == SDL_CONTROLLERDEVICEREMOVED) {
        /* The PSX-side pad stays connected: the BIOS/game sees a silent,
         * unresponsive pad rather than one that vanished mid-poll; a missing
         * response (0xFF) already reads as "no button pressed" to the protocol. */
        SDL_GameController* closing = ctrl->gc;
        ctrl->gc = NULL;
        if (closing) SDL_GameControllerClose(closing);
        LOG_SYSTEM_INFO("[SYSTEM] Controller disconnected");
    }
}

uint16_t controller_update(Controller* ctrl) {
    ctrl->analog_toggle = false;  // one-frame edge flag, cleared here and set below

    if (!ctrl->connected) {
        ctrl->left_x = ctrl->left_y = ctrl->right_x = ctrl->right_y = 0;
        return 0xFFFF;  // All released if disconnected
    }

    if (ctrl->gc) {
        ctrl->left_x  = SDL_GameControllerGetAxis(ctrl->gc, SDL_CONTROLLER_AXIS_LEFTX);
        ctrl->left_y  = SDL_GameControllerGetAxis(ctrl->gc, SDL_CONTROLLER_AXIS_LEFTY);
        ctrl->right_x = SDL_GameControllerGetAxis(ctrl->gc, SDL_CONTROLLER_AXIS_RIGHTX);
        ctrl->right_y = SDL_GameControllerGetAxis(ctrl->gc, SDL_CONTROLLER_AXIS_RIGHTY);

        // DS4 touchpad click is the stand-in for the Analog button that real
        // analog pads carry (DOCS/controllersandmemorycards.md:437-440). Rising
        // edge only, so main.c toggles SIO analog mode once per press.
        bool touched = SDL_GameControllerGetButton(ctrl->gc, SDL_CONTROLLER_BUTTON_TOUCHPAD) != 0;
        if (touched && !ctrl->gc_touchpad_prev)
            ctrl->analog_toggle = true;
        ctrl->gc_touchpad_prev = touched;

        // Both inputs drive the same pad. The word is active-low, so ANDing the
        // two sources unions their presses: a button held on either device reads
        // as pressed, and neither can un-press the other. Keeping the keyboard
        // live alongside a connected DS4 is what lets the pad be plugged in
        // mid-session without the keyboard going dead under the user's hands.
        return controller_update_from_gamepad(ctrl) & controller_update_from_keyboard(ctrl);
    }

    // No DS4: keyboard path unchanged (must produce exactly what the old
    // controller_update_from_keyboard() produced as the sole input source).
    ctrl->left_x = ctrl->left_y = ctrl->right_x = ctrl->right_y = 0;
    return controller_update_from_keyboard(ctrl);
}

/**
 * Route SIO rumble levels to the DS4. M1 (large motor, analog slow/fast) maps to
 * SDL's low-frequency channel, M2 (small motor, digital) to the high-frequency
 * channel, both scaled from 8-bit to 16-bit. SDL_GameControllerRumble has a
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
            SDL_GameControllerRumble(ctrl->gc, 0, 0, 1);
            ctrl->rumble_active = false;
        }
        return;
    }

    uint16_t low  = (uint16_t)(((uint16_t)m1 << 8) | m1);  // 8-bit → 0..0xFFFF
    uint16_t high = m2 ? 0xFFFF : 0x0000;
    SDL_GameControllerRumble(ctrl->gc, low, high, 200);
    ctrl->rumble_active = true;
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
    const uint8_t* keys = SDL_GetKeyboardState(NULL);

    for (int i = 0; i < 16; i++) {
        int scancode = ctrl->key_map[i];
        if (scancode > 0 && scancode < SDL_NUM_SCANCODES && keys[scancode]) {
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
