#ifndef CONTROLLER_H
#define CONTROLLER_H

#include <stdint.h>
#include <stdbool.h>

/**
 * PSX Controller button mapping (bit positions in button_state)
 * Note: 0 = pressed, 1 = released (inverted logic)
 */
typedef struct {
    unsigned select : 1;    // Bit 0
    unsigned l3 : 1;        // Bit 1 (L3 stick press)
    unsigned r3 : 1;        // Bit 2 (R3 stick press)
    unsigned start : 1;     // Bit 3
    unsigned up : 1;        // Bit 4
    unsigned right : 1;     // Bit 5
    unsigned down : 1;      // Bit 6
    unsigned left : 1;      // Bit 7
    unsigned l2 : 1;        // Bit 8 (left trigger)
    unsigned r2 : 1;        // Bit 9 (right trigger)
    unsigned l1 : 1;        // Bit 10 (left shoulder)
    unsigned r1 : 1;        // Bit 11 (right shoulder)
    unsigned triangle : 1;  // Bit 12
    unsigned circle : 1;    // Bit 13
    unsigned cross : 1;     // Bit 14
    unsigned square : 1;    // Bit 15
} ControllerButtons;

typedef struct {
    uint16_t button_state;  // Bitfield of all buttons (0=pressed, 1=released)
    bool connected;
} Controller;

// Initialize controller
void controller_init(Controller* ctrl);

// Update controller state from keyboard input
// Returns updated button_state (16-bit PSX format)
uint16_t controller_update_from_keyboard(Controller* ctrl);

// Helper: Set individual button states
void controller_set_button(Controller* ctrl, int button_bit, bool pressed);

// Helper: Get current button state as PSX format (already inverted)
uint16_t controller_get_button_state(Controller* ctrl);

// Connect/disconnect controller
void controller_set_connected(Controller* ctrl, bool connected);

#endif // CONTROLLER_H
