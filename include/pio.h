#ifndef PIO_H
#define PIO_H

#include <stdint.h>
#include <stdbool.h>

// PIO (Parallel I/O) Memory Map based on PSX-SPX documentation
// The parallel port is mainly used for development hardware and homebrew devices

#define PIO_BASE_ADDR     0x1F000000  // Parallel port base address (Expansion 1)
#define PIO_SIZE          0x800000    // 8MB address space
#define PIO_END_ADDR      (PIO_BASE_ADDR + PIO_SIZE - 1)

// PIO Register offsets (based on common parallel port implementations)
#define PIO_DATA_REG      0x0000      // Data register (8-bit)
#define PIO_STATUS_REG    0x0001      // Status register (8-bit)
#define PIO_CTRL_REG      0x0002      // Control register (8-bit)

// PIO Status Register bits
#define PIO_STATUS_ERROR    (1 << 3)  // Error signal
#define PIO_STATUS_SELECT   (1 << 4)  // Select signal
#define PIO_STATUS_PAPER    (1 << 5)  // Paper out signal
#define PIO_STATUS_ACK      (1 << 6)  // Acknowledge signal
#define PIO_STATUS_BUSY     (1 << 7)  // Busy signal (inverted)

// PIO Control Register bits
#define PIO_CTRL_STROBE     (1 << 0)  // Strobe signal
#define PIO_CTRL_AUTOFEED   (1 << 1)  // Auto line feed
#define PIO_CTRL_INIT       (1 << 2)  // Initialize printer
#define PIO_CTRL_SELECT_IN  (1 << 3)  // Select input
#define PIO_CTRL_IRQ_ENABLE (1 << 4)  // IRQ enable
#define PIO_CTRL_DIRECTION  (1 << 5)  // Direction (0=out, 1=in)

// PIO Device Types (for emulation purposes)
typedef enum {
    PIO_DEVICE_NONE = 0,      // No device connected
    PIO_DEVICE_PRINTER,       // Parallel printer
    PIO_DEVICE_CHEAT_CART,    // Action Replay/GameShark type device
    PIO_DEVICE_HOMEBREW,      // Generic homebrew device
    PIO_DEVICE_DEBUG          // Development/debug hardware
} PioDeviceType;

// PIO Buffer for data transfer
#define PIO_BUFFER_SIZE   1024

// Main PIO State Structure
typedef struct {
    // Register state
    uint8_t data_reg;           // Data register value
    uint8_t status_reg;         // Status register value
    uint8_t control_reg;        // Control register value
    
    // Device configuration
    PioDeviceType device_type;  // Type of connected device
    bool device_present;        // Device presence flag
    bool irq_enabled;           // IRQ enable flag
    bool initialized;           // Initialization flag
    
    // Data buffer for device communication
    uint8_t tx_buffer[PIO_BUFFER_SIZE];  // Transmit buffer
    uint8_t rx_buffer[PIO_BUFFER_SIZE];  // Receive buffer
    int tx_pos;                 // Current transmit position
    int rx_pos;                 // Current receive position
    int tx_count;               // Number of bytes in TX buffer
    int rx_count;               // Number of bytes in RX buffer
    
    // Timing and state
    bool busy;                  // Busy transferring data
    bool error;                 // Error condition
    uint32_t last_access_time;  // Last access timestamp
    
    // Statistics
    uint32_t bytes_sent;        // Total bytes sent
    uint32_t bytes_received;    // Total bytes received
    uint32_t access_count;      // Total register accesses
} Pio;

// Function Prototypes

// Initialization and Control
void pio_init(Pio* pio);
void pio_reset(Pio* pio);
void pio_set_device_type(Pio* pio, PioDeviceType device_type);

// Register Access Functions
uint8_t pio_load8(Pio* pio, uint32_t offset);
uint16_t pio_load16(Pio* pio, uint32_t offset);
uint32_t pio_load32(Pio* pio, uint32_t offset);
void pio_store8(Pio* pio, uint32_t offset, uint8_t value);
void pio_store16(Pio* pio, uint32_t offset, uint16_t value);
void pio_store32(Pio* pio, uint32_t offset, uint32_t value);

// Device Emulation Functions
void pio_handle_data_write(Pio* pio, uint8_t data);
uint8_t pio_handle_data_read(Pio* pio);
void pio_update_status(Pio* pio);
void pio_handle_control_write(Pio* pio, uint8_t value);

// Buffer Management
bool pio_tx_buffer_full(Pio* pio);
bool pio_tx_buffer_empty(Pio* pio);
bool pio_rx_buffer_full(Pio* pio);
bool pio_rx_buffer_empty(Pio* pio);
void pio_push_tx_data(Pio* pio, uint8_t data);
uint8_t pio_pop_rx_data(Pio* pio);
void pio_clear_buffers(Pio* pio);

// Device-specific handlers (stubs for now)
void pio_handle_printer_data(Pio* pio, uint8_t data);
void pio_handle_cheat_cart_data(Pio* pio, uint8_t data);
void pio_handle_homebrew_data(Pio* pio, uint8_t data);
void pio_handle_debug_data(Pio* pio, uint8_t data);

// Update and Maintenance
void pio_update(Pio* pio);
bool pio_is_device_ready(Pio* pio);
bool pio_has_pending_data(Pio* pio);

#endif // PIO_H