/* SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 ZioZoni95
 *
 * Part of ZoniStation One, a PlayStation 1 emulator.
 * See LICENSE for the full licence text and THIRD-PARTY.md for the
 * components of this project that have other authors.
 */
#ifndef CONTROLLER_H
#define CONTROLLER_H

#include <stdint.h>
#include <stdbool.h>
#include <SDL2/SDL.h>

// PSX button bit positions (0=pressed, 1=released, inverted logic), for reference:
// 0=SELECT 1=L3 2=R3 3=START 4=UP 5=RIGHT 6=DOWN 7=LEFT
// 8=L2 9=R2 10=L1 11=R1 12=TRIANGLE 13=CIRCLE 14=CROSS 15=SQUARE

typedef struct {
    bool connected;              // host input enabled/disabled (not the PSX-side pad connection)
    SDL_GameController* gc;      // active DS4; NULL when none present
    int16_t left_x, left_y;      // left stick raw (-32768..32767), for dpad emulation
    int16_t right_x, right_y;    // right stick raw (unused until the analog protocol lands)
    bool analog_toggle;          // rising edge of the DS4 touchpad click — the stand-in for
                                 // the pad's Analog button. Set true for one frame per press;
                                 // the caller toggles SIO analog mode and may leave it as-is.
    bool gc_touchpad_prev;       // touchpad state of the previous frame (edge detection)
    bool rumble_active;          // true while SDL rumble is firing (stop-on-zero edge)
    int key_map[16];             // scancode mapping for 16 PSX buttons
} Controller;

// Initialize controller
void controller_init(Controller* ctrl);

// Get pointer to currently active controller instance
Controller* controller_get_active(void);

// Consume SDL_CONTROLLERDEVICEADDED / SDL_CONTROLLERDEVICEREMOVED from the event
// drain: open/close the SDL_GameController*, log connect/disconnect. No-op for
// every other event type. First pad wins; extra pads are ignored until it leaves.
void controller_process_event(Controller* ctrl, const SDL_Event* ev);

// Produce the current PSX button state (16-bit, 0=pressed, 1=released) from a DS4
// if one is open, else fall back to the keyboard path unchanged. Also folds the
// two sticks into ctrl->left_x/left_y/right_x/right_y (raw -32768..32767) for the
// SIO analog protocol. Callers own the returned value.
uint16_t controller_update(Controller* ctrl);

// Poll SDL keyboard and return the current PSX button state (16-bit, 0=pressed,
// 1=released). Stateless aside from `connected`/internal change-log tracking —
// callers own the returned value; sio_set_button_state() is the single place
// it's stored (SioInternal.button_state), not this struct.
uint16_t controller_update_from_keyboard(Controller* ctrl);

// Route the SIO rumble motor levels (m1 large 00h..FFh, m2 small 00h/FFh) onto
// the open game controller via SDL_GameControllerRumble. Fires while a motor is
// active, stops once on the transition to both-zero. No-op when no DS4 is open.
void controller_update_rumble(Controller* ctrl, uint8_t m1, uint8_t m2);

// Connect/disconnect controller
void controller_set_connected(Controller* ctrl, bool connected);

#endif // CONTROLLER_H
