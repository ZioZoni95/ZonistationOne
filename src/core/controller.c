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

void controller_init(Controller* ctrl) {
    memset(ctrl, 0, sizeof(Controller));
    ctrl->connected = true;       // Controller enabled by default
    LOG_SYSTEM_INFO("[SYSTEM] Controller initialized (keyboard input enabled)");
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

    // Start with all buttons released
    uint16_t buttons = 0xFFFF;

    // Get keyboard state
    const uint8_t* keys = SDL_GetKeyboardState(NULL);

    // Direction buttons (WASD)
    if (keys[SDL_SCANCODE_W]) buttons &= ~(1 << 4);  // UP
    if (keys[SDL_SCANCODE_D]) buttons &= ~(1 << 5);  // RIGHT
    if (keys[SDL_SCANCODE_S]) buttons &= ~(1 << 6);  // DOWN
    if (keys[SDL_SCANCODE_A]) buttons &= ~(1 << 7);  // LEFT

    // Action buttons (E/C/Z/X)
    if (keys[SDL_SCANCODE_E]) buttons &= ~(1 << 12); // △ TRIANGLE
    if (keys[SDL_SCANCODE_C]) buttons &= ~(1 << 13); // ○ CIRCLE
    if (keys[SDL_SCANCODE_Z]) buttons &= ~(1 << 14); // × CROSS
    if (keys[SDL_SCANCODE_X]) buttons &= ~(1 << 15); // □ SQUARE

    // Shoulder buttons (Q/R)
    if (keys[SDL_SCANCODE_Q]) buttons &= ~(1 << 10); // L1
    if (keys[SDL_SCANCODE_R]) buttons &= ~(1 << 11); // R1

    // Trigger buttons (SHIFT/CTRL)
    if (keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT])
        buttons &= ~(1 << 8);  // L2
    if (keys[SDL_SCANCODE_LCTRL] || keys[SDL_SCANCODE_RCTRL])
        buttons &= ~(1 << 9);  // R2

    // Menu buttons
    if (keys[SDL_SCANCODE_SPACE]) buttons &= ~(1 << 3);  // START
    if (keys[SDL_SCANCODE_BACKSPACE]) buttons &= ~(1 << 0);  // SELECT

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
