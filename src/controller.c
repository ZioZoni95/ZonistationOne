#include "controller.h"
#include <SDL2/SDL.h>
#include "log.h"

// Global controller state
Controller controller_state = {0};

// SDL Key mappings (configurable)
static SDL_Keycode key_mapping[16] = {
    SDLK_SPACE,     // SELECT
    SDLK_UNKNOWN,   // L3 (not mapped)
    SDLK_UNKNOWN,   // R3 (not mapped)
    SDLK_RETURN,    // START
    SDLK_UP,        // UP
    SDLK_RIGHT,     // RIGHT
    SDLK_DOWN,      // DOWN
    SDLK_LEFT,      // LEFT
    SDLK_q,         // L2
    SDLK_e,         // R2
    SDLK_w,         // L1
    SDLK_r,         // R1
    SDLK_i,         // TRIANGLE
    SDLK_l,         // CIRCLE
    SDLK_k,         // CROSS
    SDLK_j          // SQUARE
};

void controller_init(void) {
    // Initialize with digital controller
    controller_state.type = CONTROLLER_DIGITAL;
    controller_state.buttons = 0xFFFF;  // All buttons released
    controller_state.connected = true;

    // Analog sticks centered
    controller_state.analog_rx = 0x80;
    controller_state.analog_ry = 0x80;
    controller_state.analog_lx = 0x80;
    controller_state.analog_ly = 0x80;

    LOG_INFO("Controller initialized: Digital controller connected");
}

void controller_update(void) {
    // This function can be called periodically to update controller state
    // For now, we rely on SDL event handling in main loop
}

void controller_set_button(uint16_t button_mask, bool pressed) {
    if (pressed) {
        controller_state.buttons &= ~button_mask;  // Clear bit (pressed)
    } else {
        controller_state.buttons |= button_mask;   // Set bit (released)
    }
}

uint16_t controller_get_buttons(void) {
    return controller_state.buttons;
}

bool controller_is_connected(void) {
    return controller_state.connected;
}

// SDL Event handler - call this from main loop
void controller_handle_sdl_event(SDL_Event* event) {
    if (event->type == SDL_KEYDOWN || event->type == SDL_KEYUP) {
        SDL_Keycode key = event->key.keysym.sym;
        bool pressed = (event->type == SDL_KEYDOWN);

        // Map SDL keys to PS1 buttons
        for (int i = 0; i < 16; i++) {
            if (key_mapping[i] == key) {
                uint16_t button_mask = (1 << i);
                controller_set_button(button_mask, pressed);
                break;
            }
        }
    }
}
