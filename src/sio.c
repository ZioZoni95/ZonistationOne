#include "sio.h"
#include "log.h"
#include <string.h>

/**
 * @brief Initialize SIO controller
 */
void sio_init(Sio* sio) {
    memset(sio, 0, sizeof(Sio));
    
    // Initialize to ready state
    sio->sio0_stat = SIO_STAT_TX_READY | SIO_STAT_TX_EMPTY;
    sio->sio0_mode = 0x000D;  // Default mode: 8-bit, no parity, 1 stop bit
    sio->sio0_ctrl = 0;
    sio->sio0_baud = 0x0088;  // Default baud rate
    
    // Simulate connected peripherals
    sio->controller_connected = true;
    sio->memcard_connected = true;
    sio->rx_pos = 0;
    sio->tx_pos = 0;
}

/**
 * @brief Load from SIO register
 */
uint32_t sio_load(Sio* sio, uint32_t offset) {
    switch (offset) {
        case 0x00: // SIO0_DATA (0x1F801040)
            // Return received data and mark as read
            if (sio->rx_pos > 0) {
                uint8_t data = sio->rx_buffer[0];
                // Shift buffer down
                for (int i = 0; i < sio->rx_pos - 1; i++) {
                    sio->rx_buffer[i] = sio->rx_buffer[i + 1];
                }
                sio->rx_pos--;
                
                // Update status - no more data ready if buffer empty
                if (sio->rx_pos == 0) {
                    sio->sio0_stat &= ~SIO_STAT_RX_READY;
                }
                
                return data;
            }
            return 0xFF; // No data available
            
        case 0x04: // SIO0_STAT (0x1F801044)
            return sio->sio0_stat;
            
        case 0x08: // SIO0_MODE (0x1F801048)  
            return sio->sio0_mode;
            
        case 0x0C: // SIO0_CTRL (0x1F80104C)
            return sio->sio0_ctrl;
            
        case 0x0E: // SIO0_BAUD (0x1F80104E) - 16-bit register
            return sio->sio0_baud;
            
        default:
            LOG_WARN("SIO: Unhandled read from offset 0x%02X", offset);
            return 0;
    }
}

/**
 * @brief Store to SIO register
 */
void sio_store(Sio* sio, uint32_t offset, uint32_t value) {
    switch (offset) {
        case 0x00: // SIO0_DATA (0x1F801040)
            // Handle controller/memory card communication
            sio_handle_command(sio, value & 0xFF);
            break;
            
        case 0x08: // SIO0_MODE (0x1F801048)
            sio->sio0_mode = value & 0xFFFF;
            break;
            
        case 0x0A: // SIO0_CTRL (0x1F80104A) - 16-bit register
            sio->sio0_ctrl = value & 0xFFFF;
            
            // Handle control commands
            if (value & SIO_CTRL_RESET) {
                sio_init(sio); // Reset SIO state
            }
            
            // Update status based on control settings
            if (value & SIO_CTRL_TX_EN) {
                sio->sio0_stat |= SIO_STAT_TX_READY | SIO_STAT_TX_EMPTY;
            }
            if (value & SIO_CTRL_RX_EN) {
                // RX ready status managed by data reception
            }
            break;
            
        case 0x0E: // SIO0_BAUD (0x1F80104E)
            sio->sio0_baud = value & 0xFFFF;
            break;
            
        default:
            LOG_WARN("SIO: Unhandled write to offset 0x%02X = 0x%08X", offset, value);
            break;
    }
}

/**
 * @brief Handle controller/memory card command
 */
void sio_handle_command(Sio* sio, uint8_t command) {
    // Simple controller/memory card command handling
    // This is a basic stub - real implementation would be much more complex
    
    switch (command) {
        case 0x01: // Controller access
            if (sio->controller_connected) {
                // Respond with digital controller presence
                sio->rx_buffer[sio->rx_pos++] = CONTROLLER_ID_DIGITAL;
                sio->rx_buffer[sio->rx_pos++] = 0x5A; // Standard response
                sio->sio0_stat |= SIO_STAT_RX_READY;
            } else {
                // No controller connected
                sio->rx_buffer[sio->rx_pos++] = 0xFF;
                sio->sio0_stat |= SIO_STAT_RX_READY;
            }
            break;
            
        case 0x81: // Memory card access
            if (sio->memcard_connected) {
                // Respond with memory card presence  
                sio->rx_buffer[sio->rx_pos++] = MEMCARD_ID;
                sio->rx_buffer[sio->rx_pos++] = 0x5A; // Standard response
                sio->sio0_stat |= SIO_STAT_RX_READY;
            } else {
                // No memory card connected
                sio->rx_buffer[sio->rx_pos++] = 0xFF;
                sio->sio0_stat |= SIO_STAT_RX_READY;
            }
            break;
            
        default:
            // Echo back unknown commands
            sio->rx_buffer[sio->rx_pos++] = command;
            sio->sio0_stat |= SIO_STAT_RX_READY;
            break;
    }
    
    // Ensure we don't overflow the buffer
    if (sio->rx_pos >= sizeof(sio->rx_buffer)) {
        sio->rx_pos = sizeof(sio->rx_buffer) - 1;
    }
    
    // Set transmit ready for next command
    sio->sio0_stat |= SIO_STAT_TX_READY | SIO_STAT_TX_EMPTY;
}

/**
 * @brief Update SIO state (called periodically)
 */
void sio_update(Sio* sio) {
    // Maintain basic ready states
    sio->sio0_stat |= SIO_STAT_TX_READY | SIO_STAT_TX_EMPTY;
    
    // Simple DSR/CTS simulation for connected peripherals
    if (sio->controller_connected || sio->memcard_connected) {
        sio->sio0_stat |= SIO_STAT_DSR | SIO_STAT_CTS;
    }
}