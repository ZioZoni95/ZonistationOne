#ifndef CONTROLLER_H
#define CONTROLLER_H

#include <stdint.h>
#include <stdbool.h>
#include <SDL2/SDL.h>

// PS1 Controller Types
typedef enum {
    CONTROLLER_NONE = 0,
    CONTROLLER_DIGITAL = 1,
    CONTROLLER_ANALOG = 2,
    CONTROLLER_MOUSE = 3,
    CONTROLLER_NEGCON = 4,
    CONTROLLER_GUNCON = 5,
    CONTROLLER_JOGCON = 6
} ControllerType;

// Button bitmasks (PS1 standard)
#define BTN_SELECT      (1 << 0)
#define BTN_L3          (1 << 1)
#define BTN_R3          (1 << 2)
#define BTN_START       (1 << 3)
#define BTN_UP          (1 << 4)
#define BTN_RIGHT       (1 << 5)
#define BTN_DOWN        (1 << 6)
#define BTN_LEFT        (1 << 7)
#define BTN_L2          (1 << 8)
#define BTN_R2          (1 << 9)
#define BTN_L1          (1 << 10)
#define BTN_R1          (1 << 11)
#define BTN_TRIANGLE    (1 << 12)
#define BTN_CIRCLE      (1 << 13)
#define BTN_CROSS       (1 << 14)
#define BTN_SQUARE      (1 << 15)

// Controller state structure
typedef struct {
    ControllerType type;
    uint16_t buttons;        // Button states (0=pressed, 1=released)
    uint8_t analog_rx;       // Right stick X (analog controllers)
    uint8_t analog_ry;       // Right stick Y
    uint8_t analog_lx;       // Left stick X
    uint8_t analog_ly;       // Left stick Y
    bool connected;
} Controller;

// Global controller state
extern Controller controller_state;

// Controller API
void controller_init(void);
void controller_update(void);
void controller_set_button(uint16_t button_mask, bool pressed);
uint16_t controller_get_buttons(void);
bool controller_is_connected(void);
void controller_handle_sdl_event(SDL_Event* event);

#endif // CONTROLLER_H
