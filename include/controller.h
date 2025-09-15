#ifndef CONTROLLER_H
#define CONTROLLER_H

#include <stdint.h>
#include <stdbool.h>

/**
 * PlayStation 1 Controller (GamePad) Emulation
 * Based on PSX-SPX documentation and hardware specifications
 * 
 * Features:
 * - Digital Pad support (standard controller)
 * - Analog Pad support (DualShock style)
 * - Multitap support (4-player games)
 * - Communication protocol handling
 * - Button state management
 * - SIO interface compliance
 */

// Controller Types
typedef enum {
    CONTROLLER_TYPE_NONE       = 0x00,
    CONTROLLER_TYPE_DIGITAL    = 0x41,  // Standard digital pad
    CONTROLLER_TYPE_ANALOG_RED = 0x73,  // Analog pad (red mode)
    CONTROLLER_TYPE_ANALOG_GREEN = 0x53, // Analog pad (green mode)
    CONTROLLER_TYPE_MULTITAP   = 0x80,  // Multitap device
} ControllerType;

// Controller Communication States
typedef enum {
    CONTROLLER_STATE_IDLE,
    CONTROLLER_STATE_WAIT_CMD,
    CONTROLLER_STATE_SEND_ID,
    CONTROLLER_STATE_SEND_DATA,
    CONTROLLER_STATE_CONFIG,
    CONTROLLER_STATE_COMPLETE,
} ControllerState;

// Controller Commands (PSX-SPX specification)
#define CONTROLLER_CMD_READ     0x42  // Read controller state
#define CONTROLLER_CMD_CONFIG   0x43  // Enter/exit config mode
#define CONTROLLER_CMD_ANALOG   0x44  // Set analog mode
#define CONTROLLER_CMD_STATUS   0x45  // Get controller status
#define CONTROLLER_CMD_UNKNOWN  0x46  // Unknown command
#define CONTROLLER_CMD_VIBRATE  0x4D  // Set vibration motors

// Controller Responses
#define CONTROLLER_RESP_ACK     0x5A  // Acknowledgment
#define CONTROLLER_RESP_NONE    0xFF  // No response (disconnected)

// Button Bit Masks (active low)
#define CONTROLLER_SELECT   (1 << 0)
#define CONTROLLER_L3       (1 << 1)  // Left stick button (analog)
#define CONTROLLER_R3       (1 << 2)  // Right stick button (analog)
#define CONTROLLER_START    (1 << 3)
#define CONTROLLER_UP       (1 << 4)
#define CONTROLLER_RIGHT    (1 << 5)
#define CONTROLLER_DOWN     (1 << 6)
#define CONTROLLER_LEFT     (1 << 7)
#define CONTROLLER_L2       (1 << 8)
#define CONTROLLER_R2       (1 << 9)
#define CONTROLLER_L1       (1 << 10)
#define CONTROLLER_R1       (1 << 11)
#define CONTROLLER_TRIANGLE (1 << 12)
#define CONTROLLER_CIRCLE   (1 << 13)
#define CONTROLLER_CROSS    (1 << 14)
#define CONTROLLER_SQUARE   (1 << 15)

// Analog stick ranges
#define ANALOG_MIN          0x00
#define ANALOG_CENTER       0x80
#define ANALOG_MAX          0xFF

// Controller Configuration
typedef struct {
    bool config_mode;           // Configuration mode active
    bool analog_mode;           // Analog mode enabled
    bool analog_locked;         // Analog mode lock
    bool vibration_enabled;     // Vibration feature enabled
    uint8_t vibration_map[6];   // Vibration motor mapping
} ControllerConfig;

// Analog Stick State
typedef struct {
    uint8_t right_x;           // Right stick X (0-255)
    uint8_t right_y;           // Right stick Y (0-255)
    uint8_t left_x;            // Left stick X (0-255)
    uint8_t left_y;            // Left stick Y (0-255)
} AnalogState;

// Vibration Motors
typedef struct {
    uint8_t small_motor;       // Small motor intensity (0-255)
    uint8_t large_motor;       // Large motor intensity (0-255)
    bool small_enabled;        // Small motor enabled
    bool large_enabled;        // Large motor enabled
} VibrationState;

// Main Controller Structure
typedef struct {
    // Basic Properties
    bool connected;            // Controller connected
    ControllerType type;       // Controller type
    uint8_t port;             // Port number (0-3 for multitap)
    
    // Communication State
    ControllerState state;     // Current communication state
    uint8_t current_cmd;       // Current command being processed
    uint8_t data_index;        // Current data byte index
    uint8_t transfer_count;    // Number of bytes in current transfer
    
    // Input State
    uint16_t button_state;     // Button states (active low)
    uint16_t prev_button_state; // Previous button states
    AnalogState analog;        // Analog stick positions
    
    // Configuration
    ControllerConfig config;   // Configuration settings
    
    // Vibration
    VibrationState vibration;  // Vibration motor state
    
    // Communication Buffers
    uint8_t tx_buffer[32];     // Transmission buffer
    uint8_t rx_buffer[32];     // Reception buffer
    uint8_t tx_pos;            // Transmission position
    uint8_t rx_pos;            // Reception position
    
    // Status
    bool error;                // Error flag
    uint8_t error_code;        // Last error code
    uint32_t packet_count;     // Total packets processed
    uint32_t error_count;      // Total errors
    
} Controller;

// Multitap Structure (4 controller support)
typedef struct {
    bool connected;            // Multitap connected
    Controller controllers[4]; // 4 controller slots
    uint8_t active_slot;       // Currently active slot
    bool slot_enable[4];       // Slot enable flags
    
    // Communication State
    ControllerState state;     // Multitap state
    uint8_t current_slot;      // Current slot being accessed
    uint8_t transfer_buffer[64]; // Transfer buffer
    
} Multitap;

// Function Prototypes

// Initialization and Reset
void controller_init(Controller* ctrl, uint8_t port);
void controller_reset(Controller* ctrl);
void controller_connect(Controller* ctrl, ControllerType type);
void controller_disconnect(Controller* ctrl);

// Input Management
void controller_set_button(Controller* ctrl, uint16_t button_mask, bool pressed);
void controller_set_button_state(Controller* ctrl, uint16_t button_state);
void controller_set_analog_stick(Controller* ctrl, uint8_t left_x, uint8_t left_y, 
                                 uint8_t right_x, uint8_t right_y);
void controller_update_input(Controller* ctrl);

// Communication Interface
uint8_t controller_exchange_byte(Controller* ctrl, uint8_t data);
void controller_begin_transfer(Controller* ctrl);
void controller_end_transfer(Controller* ctrl);

// Command Processing
void controller_process_read_command(Controller* ctrl);
void controller_process_config_command(Controller* ctrl, uint8_t data);
void controller_process_analog_command(Controller* ctrl, uint8_t data);
void controller_process_vibration_command(Controller* ctrl, uint8_t data);

// Configuration
void controller_set_analog_mode(Controller* ctrl, bool enabled, bool locked);
void controller_set_vibration(Controller* ctrl, bool enabled);
void controller_enter_config_mode(Controller* ctrl);
void controller_exit_config_mode(Controller* ctrl);

// Vibration Control
void controller_set_vibration_motors(Controller* ctrl, uint8_t small, uint8_t large);
void controller_update_vibration(Controller* ctrl);

// Status and Query
bool controller_is_connected(Controller* ctrl);
bool controller_is_analog(Controller* ctrl);
bool controller_button_pressed(Controller* ctrl, uint16_t button_mask);
bool controller_button_just_pressed(Controller* ctrl, uint16_t button_mask);
bool controller_button_just_released(Controller* ctrl, uint16_t button_mask);
uint8_t controller_get_status(Controller* ctrl);

// Multitap Functions
void multitap_init(Multitap* tap);
void multitap_reset(Multitap* tap);
void multitap_connect(Multitap* tap);
void multitap_disconnect(Multitap* tap);
Controller* multitap_get_controller(Multitap* tap, uint8_t slot);
uint8_t multitap_exchange_byte(Multitap* tap, uint8_t data);
void multitap_set_slot_enable(Multitap* tap, uint8_t slot, bool enabled);

// Utility Functions
const char* controller_type_to_string(ControllerType type);
const char* controller_button_to_string(uint16_t button_mask);
void controller_print_state(Controller* ctrl);

// Update and Maintenance
void controller_update(Controller* ctrl);
void controller_save_state(Controller* ctrl, uint8_t* buffer);
void controller_load_state(Controller* ctrl, uint8_t* buffer);

#endif // CONTROLLER_H