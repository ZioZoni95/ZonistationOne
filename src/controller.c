#include "controller.h"
#include "log.h"
#include <string.h>
#include <stdio.h>

/**
 * @brief Initialize controller to default state
 * Based on PSX-SPX specifications for Controller protocol
 */
void controller_init(Controller* ctrl, uint8_t port) {
    memset(ctrl, 0, sizeof(Controller));
    
    // Basic properties
    ctrl->connected = false;
    ctrl->type = CONTROLLER_TYPE_NONE;
    ctrl->port = port;
    
    // Communication state
    ctrl->state = CONTROLLER_STATE_IDLE;
    ctrl->current_cmd = 0;
    ctrl->data_index = 0;
    ctrl->transfer_count = 0;
    
    // Input state (buttons not pressed = high = 1)
    ctrl->button_state = 0xFFFF;
    ctrl->prev_button_state = 0xFFFF;
    
    // Analog sticks centered
    ctrl->analog.left_x = ANALOG_CENTER;
    ctrl->analog.left_y = ANALOG_CENTER;
    ctrl->analog.right_x = ANALOG_CENTER;
    ctrl->analog.right_y = ANALOG_CENTER;
    
    // Configuration defaults
    ctrl->config.config_mode = false;
    ctrl->config.analog_mode = false;
    ctrl->config.analog_locked = false;
    ctrl->config.vibration_enabled = false;
    memset(ctrl->config.vibration_map, 0xFF, sizeof(ctrl->config.vibration_map));
    
    // Vibration off
    ctrl->vibration.small_motor = 0;
    ctrl->vibration.large_motor = 0;
    ctrl->vibration.small_enabled = false;
    ctrl->vibration.large_enabled = false;
    
    // Clear buffers
    memset(ctrl->tx_buffer, 0, sizeof(ctrl->tx_buffer));
    memset(ctrl->rx_buffer, 0, sizeof(ctrl->rx_buffer));
    ctrl->tx_pos = 0;
    ctrl->rx_pos = 0;
    
    // Status
    ctrl->error = false;
    ctrl->error_code = 0;
    ctrl->packet_count = 0;
    ctrl->error_count = 0;
    
    LOG_TRACE("Controller %d initialized", port);
}

/**
 * @brief Reset controller state
 */
void controller_reset(Controller* ctrl) {
    uint8_t port = ctrl->port;
    bool was_connected = ctrl->connected;
    ControllerType type = ctrl->type;
    
    controller_init(ctrl, port);
    
    if (was_connected) {
        controller_connect(ctrl, type);
    }
    
    LOG_TRACE("Controller %d reset", port);
}

/**
 * @brief Connect controller of specified type
 */
void controller_connect(Controller* ctrl, ControllerType type) {
    ctrl->connected = true;
    ctrl->type = type;
    
    // Set type-specific defaults
    switch (type) {
        case CONTROLLER_TYPE_DIGITAL:
            ctrl->config.analog_mode = false;
            ctrl->config.vibration_enabled = false;
            break;
            
        case CONTROLLER_TYPE_ANALOG_RED:
        case CONTROLLER_TYPE_ANALOG_GREEN:
            ctrl->config.analog_mode = true;
            ctrl->config.vibration_enabled = true;
            break;
            
        default:
            break;
    }
    
    LOG_INFO("Controller %d connected (type=0x%02X)", ctrl->port, type);
}

/**
 * @brief Disconnect controller
 */
void controller_disconnect(Controller* ctrl) {
    ctrl->connected = false;
    ctrl->type = CONTROLLER_TYPE_NONE;
    ctrl->state = CONTROLLER_STATE_IDLE;
    
    LOG_INFO("Controller %d disconnected", ctrl->port);
}

/**
 * @brief Set individual button state
 */
void controller_set_button(Controller* ctrl, uint16_t button_mask, bool pressed) {
    if (pressed) {
        ctrl->button_state &= ~button_mask; // Active low
    } else {
        ctrl->button_state |= button_mask;  // Released = high
    }
}

/**
 * @brief Set complete button state
 */
void controller_set_button_state(Controller* ctrl, uint16_t button_state) {
    ctrl->prev_button_state = ctrl->button_state;
    ctrl->button_state = button_state;
}

/**
 * @brief Set analog stick positions
 */
void controller_set_analog_stick(Controller* ctrl, uint8_t left_x, uint8_t left_y, 
                                 uint8_t right_x, uint8_t right_y) {
    ctrl->analog.left_x = left_x;
    ctrl->analog.left_y = left_y;
    ctrl->analog.right_x = right_x;
    ctrl->analog.right_y = right_y;
}

/**
 * @brief Update input state (called each frame)
 */
void controller_update_input(Controller* ctrl) {
    // Store previous button state for edge detection
    ctrl->prev_button_state = ctrl->button_state;
    
    // TODO: Poll actual input devices (SDL, etc.)
    // For now, this is just a stub that maintains current state
}

/**
 * @brief Exchange byte with controller (main SIO interface)
 */
uint8_t controller_exchange_byte(Controller* ctrl, uint8_t data) {
    if (!ctrl->connected) {
        return CONTROLLER_RESP_NONE;
    }
    
    uint8_t response = CONTROLLER_RESP_NONE;
    
    switch (ctrl->state) {
        case CONTROLLER_STATE_IDLE:
            // Wait for command byte
            if (data == CONTROLLER_CMD_READ || data == CONTROLLER_CMD_CONFIG || 
                data == CONTROLLER_CMD_ANALOG || data == CONTROLLER_CMD_VIBRATE) {
                ctrl->current_cmd = data;
                ctrl->state = CONTROLLER_STATE_WAIT_CMD;
                ctrl->data_index = 0;
                response = (uint8_t)ctrl->type; // Return controller type ID
            }
            break;
            
        case CONTROLLER_STATE_WAIT_CMD:
            // Send ACK and prepare data
            response = CONTROLLER_RESP_ACK;
            
            if (ctrl->current_cmd == CONTROLLER_CMD_READ) {
                controller_process_read_command(ctrl);
                ctrl->state = CONTROLLER_STATE_SEND_DATA;
            } else if (ctrl->current_cmd == CONTROLLER_CMD_CONFIG) {
                controller_process_config_command(ctrl, data);
                ctrl->state = CONTROLLER_STATE_COMPLETE;
            } else if (ctrl->current_cmd == CONTROLLER_CMD_ANALOG) {
                controller_process_analog_command(ctrl, data);
                ctrl->state = CONTROLLER_STATE_COMPLETE;
            } else if (ctrl->current_cmd == CONTROLLER_CMD_VIBRATE) {
                controller_process_vibration_command(ctrl, data);
                ctrl->state = CONTROLLER_STATE_SEND_DATA;
            }
            break;
            
        case CONTROLLER_STATE_SEND_DATA:
            // Send data bytes
            if (ctrl->data_index < ctrl->transfer_count) {
                response = ctrl->tx_buffer[ctrl->data_index++];
                
                // Store received vibration data if applicable
                if (ctrl->current_cmd == CONTROLLER_CMD_VIBRATE && ctrl->data_index <= 2) {
                    ctrl->rx_buffer[ctrl->data_index - 1] = data;
                }
                
                if (ctrl->data_index >= ctrl->transfer_count) {
                    ctrl->state = CONTROLLER_STATE_COMPLETE;
                }
            }
            break;
            
        case CONTROLLER_STATE_COMPLETE:
            // Process any final data and return to idle
            if (ctrl->current_cmd == CONTROLLER_CMD_VIBRATE) {
                // Apply vibration settings
                if (ctrl->config.vibration_enabled) {
                    ctrl->vibration.small_motor = ctrl->rx_buffer[0];
                    ctrl->vibration.large_motor = ctrl->rx_buffer[1];
                }
            }
            
            ctrl->state = CONTROLLER_STATE_IDLE;
            ctrl->packet_count++;
            response = CONTROLLER_RESP_NONE;
            break;
            
        default:
            ctrl->state = CONTROLLER_STATE_IDLE;
            break;
    }
    
    return response;
}

/**
 * @brief Begin transfer (SIO CS asserted)
 */
void controller_begin_transfer(Controller* ctrl) {
    ctrl->state = CONTROLLER_STATE_IDLE;
    ctrl->data_index = 0;
    ctrl->transfer_count = 0;
}

/**
 * @brief End transfer (SIO CS deasserted)
 */
void controller_end_transfer(Controller* ctrl) {
    if (ctrl->state != CONTROLLER_STATE_IDLE && ctrl->state != CONTROLLER_STATE_COMPLETE) {
        LOG_WARN("Controller %d: Transfer ended prematurely (state=%d)", 
                 ctrl->port, ctrl->state);
        ctrl->error = true;
        ctrl->error_count++;
    }
    ctrl->state = CONTROLLER_STATE_IDLE;
}

// Command Processing Functions
void controller_process_read_command(Controller* ctrl) {
    // Prepare button data (16 bits, active low)
    ctrl->tx_buffer[0] = (ctrl->button_state) & 0xFF;        // Low byte
    ctrl->tx_buffer[1] = (ctrl->button_state >> 8) & 0xFF;   // High byte
    ctrl->transfer_count = 2;
    
    // Add analog data if in analog mode
    if (ctrl->config.analog_mode && (ctrl->type == CONTROLLER_TYPE_ANALOG_RED || 
                                     ctrl->type == CONTROLLER_TYPE_ANALOG_GREEN)) {
        ctrl->tx_buffer[2] = ctrl->analog.right_x;
        ctrl->tx_buffer[3] = ctrl->analog.right_y;
        ctrl->tx_buffer[4] = ctrl->analog.left_x;
        ctrl->tx_buffer[5] = ctrl->analog.left_y;
        ctrl->transfer_count = 6;
    }
    
    LOG_TRACE("Controller %d: Read command processed (%d bytes)", 
              ctrl->port, ctrl->transfer_count);
}

void controller_process_config_command(Controller* ctrl, uint8_t data) {
    if (data == 0x01) {
        controller_enter_config_mode(ctrl);
    } else if (data == 0x00) {
        controller_exit_config_mode(ctrl);
    }
    
    LOG_TRACE("Controller %d: Config command processed (data=0x%02X)", 
              ctrl->port, data);
}

void controller_process_analog_command(Controller* ctrl, uint8_t data) {
    if (ctrl->config.config_mode && !ctrl->config.analog_locked) {
        ctrl->config.analog_mode = (data == 0x01);
        
        // Update controller type based on analog mode
        if (ctrl->config.analog_mode) {
            ctrl->type = CONTROLLER_TYPE_ANALOG_RED;
        } else {
            ctrl->type = CONTROLLER_TYPE_DIGITAL;
        }
    }
    
    LOG_TRACE("Controller %d: Analog command processed (mode=%s)", 
              ctrl->port, ctrl->config.analog_mode ? "enabled" : "disabled");
}

void controller_process_vibration_command(Controller* ctrl, uint8_t data) {
    // Prepare to receive vibration motor settings
    ctrl->transfer_count = 6; // Standard vibration response length
    
    // Prepare response data (controller status/capabilities)
    memset(ctrl->tx_buffer, 0, ctrl->transfer_count);
    ctrl->tx_buffer[0] = (ctrl->button_state) & 0xFF;
    ctrl->tx_buffer[1] = (ctrl->button_state >> 8) & 0xFF;
    
    if (ctrl->config.analog_mode) {
        ctrl->tx_buffer[2] = ctrl->analog.right_x;
        ctrl->tx_buffer[3] = ctrl->analog.right_y;
        ctrl->tx_buffer[4] = ctrl->analog.left_x;
        ctrl->tx_buffer[5] = ctrl->analog.left_y;
    }
    
    LOG_TRACE("Controller %d: Vibration command processed", ctrl->port);
}

// Configuration Functions
void controller_set_analog_mode(Controller* ctrl, bool enabled, bool locked) {
    if (!ctrl->config.analog_locked) {
        ctrl->config.analog_mode = enabled;
        ctrl->config.analog_locked = locked;
        
        // Update controller type
        if (enabled) {
            ctrl->type = CONTROLLER_TYPE_ANALOG_RED;
        } else {
            ctrl->type = CONTROLLER_TYPE_DIGITAL;
        }
        
        LOG_INFO("Controller %d: Analog mode %s (%s)", 
                 ctrl->port, enabled ? "enabled" : "disabled",
                 locked ? "locked" : "unlocked");
    }
}

void controller_set_vibration(Controller* ctrl, bool enabled) {
    ctrl->config.vibration_enabled = enabled;
    
    if (!enabled) {
        ctrl->vibration.small_motor = 0;
        ctrl->vibration.large_motor = 0;
    }
    
    LOG_INFO("Controller %d: Vibration %s", 
             ctrl->port, enabled ? "enabled" : "disabled");
}

void controller_enter_config_mode(Controller* ctrl) {
    ctrl->config.config_mode = true;
    LOG_TRACE("Controller %d: Entered config mode", ctrl->port);
}

void controller_exit_config_mode(Controller* ctrl) {
    ctrl->config.config_mode = false;
    LOG_TRACE("Controller %d: Exited config mode", ctrl->port);
}

// Vibration Control
void controller_set_vibration_motors(Controller* ctrl, uint8_t small, uint8_t large) {
    if (ctrl->config.vibration_enabled) {
        ctrl->vibration.small_motor = small;
        ctrl->vibration.large_motor = large;
        ctrl->vibration.small_enabled = (small > 0);
        ctrl->vibration.large_enabled = (large > 0);
    }
}

void controller_update_vibration(Controller* ctrl) {
    // TODO: Interface with actual vibration hardware/SDL haptic
    // For now this is just a stub
}

// Status and Query Functions
bool controller_is_connected(Controller* ctrl) {
    return ctrl->connected;
}

bool controller_is_analog(Controller* ctrl) {
    return ctrl->config.analog_mode;
}

bool controller_button_pressed(Controller* ctrl, uint16_t button_mask) {
    return (ctrl->button_state & button_mask) == 0; // Active low
}

bool controller_button_just_pressed(Controller* ctrl, uint16_t button_mask) {
    return ((ctrl->prev_button_state & button_mask) != 0) && 
           ((ctrl->button_state & button_mask) == 0);
}

bool controller_button_just_released(Controller* ctrl, uint16_t button_mask) {
    return ((ctrl->prev_button_state & button_mask) == 0) && 
           ((ctrl->button_state & button_mask) != 0);
}

uint8_t controller_get_status(Controller* ctrl) {
    uint8_t status = 0;
    
    if (ctrl->connected) status |= 0x01;
    if (ctrl->config.analog_mode) status |= 0x02;
    if (ctrl->config.vibration_enabled) status |= 0x04;
    if (ctrl->error) status |= 0x80;
    
    return status;
}

// Multitap Functions (stubs)
void multitap_init(Multitap* tap) {
    memset(tap, 0, sizeof(Multitap));
    tap->connected = false;
    tap->active_slot = 0;
    
    for (int i = 0; i < 4; i++) {
        controller_init(&tap->controllers[i], i);
        tap->slot_enable[i] = true;
    }
    
    LOG_TRACE("Multitap initialized");
}

void multitap_connect(Multitap* tap) {
    tap->connected = true;
    LOG_INFO("Multitap connected");
}

Controller* multitap_get_controller(Multitap* tap, uint8_t slot) {
    if (slot < 4) {
        return &tap->controllers[slot];
    }
    return NULL;
}

uint8_t multitap_exchange_byte(Multitap* tap, uint8_t data) {
    // TODO: Implement multitap communication protocol
    (void)tap;
    (void)data;
    return 0xFF; // Stub
}

// Utility Functions
const char* controller_type_to_string(ControllerType type) {
    switch (type) {
        case CONTROLLER_TYPE_NONE: return "None";
        case CONTROLLER_TYPE_DIGITAL: return "Digital";
        case CONTROLLER_TYPE_ANALOG_RED: return "Analog (Red)";
        case CONTROLLER_TYPE_ANALOG_GREEN: return "Analog (Green)";
        case CONTROLLER_TYPE_MULTITAP: return "Multitap";
        default: return "Unknown";
    }
}

const char* controller_button_to_string(uint16_t button_mask) {
    // Return string for first matching button (for debugging)
    if (button_mask & CONTROLLER_SELECT) return "SELECT";
    if (button_mask & CONTROLLER_START) return "START";
    if (button_mask & CONTROLLER_UP) return "UP";
    if (button_mask & CONTROLLER_RIGHT) return "RIGHT";
    if (button_mask & CONTROLLER_DOWN) return "DOWN";
    if (button_mask & CONTROLLER_LEFT) return "LEFT";
    if (button_mask & CONTROLLER_L2) return "L2";
    if (button_mask & CONTROLLER_R2) return "R2";
    if (button_mask & CONTROLLER_L1) return "L1";
    if (button_mask & CONTROLLER_R1) return "R1";
    if (button_mask & CONTROLLER_TRIANGLE) return "TRIANGLE";
    if (button_mask & CONTROLLER_CIRCLE) return "CIRCLE";
    if (button_mask & CONTROLLER_CROSS) return "CROSS";
    if (button_mask & CONTROLLER_SQUARE) return "SQUARE";
    if (button_mask & CONTROLLER_L3) return "L3";
    if (button_mask & CONTROLLER_R3) return "R3";
    return "NONE";
}

void controller_print_state(Controller* ctrl) {
    LOG_INFO("Controller %d: Type=%s, Buttons=0x%04X, Analog=(%d,%d,%d,%d), Packets=%u, Errors=%u",
             ctrl->port, controller_type_to_string(ctrl->type),
             ctrl->button_state, ctrl->analog.left_x, ctrl->analog.left_y,
             ctrl->analog.right_x, ctrl->analog.right_y,
             ctrl->packet_count, ctrl->error_count);
}

// Update and Maintenance
void controller_update(Controller* ctrl) {
    controller_update_input(ctrl);
    controller_update_vibration(ctrl);
}

void controller_save_state(Controller* ctrl, uint8_t* buffer) {
    // TODO: Save controller state for save states
    (void)ctrl;
    (void)buffer;
}

void controller_load_state(Controller* ctrl, uint8_t* buffer) {
    // TODO: Load controller state from save states
    (void)ctrl;
    (void)buffer;
}