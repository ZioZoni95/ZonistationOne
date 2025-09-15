#ifndef SIO_H
#define SIO_H

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief SIO (Serial I/O) Controller for PlayStation 1
 * 
 * Handles communication with controllers, memory cards, and other peripherals.
 * Memory map:
 * - 0x1F801040-0x1F80104F: SIO0 (Controller/Memory Card)
 * - 0x1F801050-0x1F80105F: SIO1 (Serial communication)
 */

typedef struct {
    // SIO0 Registers (0x1F801040-0x1F80104F)
    uint32_t sio0_data;         // 0x1F801040: TX/RX Data (8-bit)
    uint32_t sio0_stat;         // 0x1F801044: Status Register
    uint32_t sio0_mode;         // 0x1F801048: Mode Register
    uint32_t sio0_ctrl;         // 0x1F80104C: Control Register
    uint32_t sio0_baud;         // 0x1F801050: Baud Rate Register (actually part of SIO1 but grouped)
    
    // Internal state
    bool controller_connected;   // Simple controller connection state
    bool memcard_connected;     // Simple memory card connection state
    uint8_t rx_buffer[64];      // Receive buffer for controller/memcard responses
    uint8_t tx_buffer[64];      // Transmit buffer
    int rx_pos;                 // Current position in RX buffer
    int tx_pos;                 // Current position in TX buffer
} Sio;

// SIO Status Register bits
#define SIO_STAT_TX_READY    (1 << 0)  // Transmit ready
#define SIO_STAT_RX_READY    (1 << 1)  // Receive ready
#define SIO_STAT_TX_EMPTY    (1 << 2)  // Transmit buffer empty
#define SIO_STAT_PARITY_ERR  (1 << 3)  // Parity error
#define SIO_STAT_RX_OVERRUN  (1 << 4)  // Receive overrun
#define SIO_STAT_FRAMING_ERR (1 << 5)  // Framing error
#define SIO_STAT_DSR         (1 << 7)  // Data Set Ready
#define SIO_STAT_CTS         (1 << 8)  // Clear To Send
#define SIO_STAT_IRQ         (1 << 9)  // IRQ flag

// SIO Control Register bits
#define SIO_CTRL_TX_EN       (1 << 0)  // Transmit enable
#define SIO_CTRL_DTR         (1 << 1)  // Data Terminal Ready
#define SIO_CTRL_RX_EN       (1 << 2)  // Receive enable
#define SIO_CTRL_TX_OUTPUT   (1 << 3)  // Transmit output level
#define SIO_CTRL_ACK         (1 << 4)  // Acknowledge
#define SIO_CTRL_RTS         (1 << 5)  // Request To Send
#define SIO_CTRL_RESET       (1 << 6)  // Reset
#define SIO_CTRL_IRQ_EN      (1 << 8)  // IRQ enable

// Controller command responses
#define CONTROLLER_ID_DIGITAL   0x41    // Digital controller ID
#define CONTROLLER_ID_ANALOG    0x73    // Analog controller ID
#define MEMCARD_ID              0x5A    // Memory card ID

// Function declarations
void sio_init(Sio* sio);
uint32_t sio_load(Sio* sio, uint32_t offset);
void sio_store(Sio* sio, uint32_t offset, uint32_t value);
void sio_handle_command(Sio* sio, uint8_t command);
void sio_update(Sio* sio);

#endif // SIO_H