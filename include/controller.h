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

// PSX button bit positions (0=pressed, 1=released, inverted logic), for reference:
// 0=SELECT 1=L3 2=R3 3=START 4=UP 5=RIGHT 6=DOWN 7=LEFT
// 8=L2 9=R2 10=L1 11=R1 12=TRIANGLE 13=CIRCLE 14=CROSS 15=SQUARE

typedef struct {
    bool connected;  // host input enabled/disabled (not the PSX-side pad connection)
} Controller;

// Initialize controller
void controller_init(Controller* ctrl);

// Poll SDL keyboard and return the current PSX button state (16-bit, 0=pressed,
// 1=released). Stateless aside from `connected`/internal change-log tracking —
// callers own the returned value; sio_set_button_state() is the single place
// it's stored (SioInternal.button_state), not this struct.
uint16_t controller_update_from_keyboard(Controller* ctrl);

// Connect/disconnect controller
void controller_set_connected(Controller* ctrl, bool connected);

#endif // CONTROLLER_H
