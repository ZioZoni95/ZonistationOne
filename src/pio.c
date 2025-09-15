#include "pio.h"
#include "log.h"
#include <string.h>

/**
 * @brief Initialize the PIO (Parallel I/O) port
 * Based on PSX-SPX specifications and typical parallel port behavior
 */
void pio_init(Pio* pio) {
    memset(pio, 0, sizeof(Pio));
    
    // Initialize registers to default state
    pio->data_reg = 0x00;
    pio->status_reg = PIO_STATUS_ERROR | PIO_STATUS_SELECT; // Default status
    pio->control_reg = PIO_CTRL_SELECT_IN;  // Default control
    
    // Device configuration
    pio->device_type = PIO_DEVICE_NONE;
    pio->device_present = false;
    pio->irq_enabled = false;
    pio->initialized = true;
    
    // Clear buffers
    pio->tx_pos = 0;
    pio->rx_pos = 0;
    pio->tx_count = 0;
    pio->rx_count = 0;
    
    // Clear state flags
    pio->busy = false;
    pio->error = false;
    pio->last_access_time = 0;
    
    // Clear statistics
    pio->bytes_sent = 0;
    pio->bytes_received = 0;
    pio->access_count = 0;
    
    LOG_INFO("PIO initialized (Parallel I/O port, no device connected)");
}

/**
 * @brief Reset PIO to power-on state
 */
void pio_reset(Pio* pio) {
    LOG_INFO("PIO reset requested");
    PioDeviceType saved_device = pio->device_type; // Preserve device type across reset
    pio_init(pio);
    pio->device_type = saved_device;
    pio->device_present = (saved_device != PIO_DEVICE_NONE);
}

/**
 * @brief Set the type of device connected to the parallel port
 */
void pio_set_device_type(Pio* pio, PioDeviceType device_type) {
    pio->device_type = device_type;
    pio->device_present = (device_type != PIO_DEVICE_NONE);
    
    const char* device_names[] = {
        "None", "Printer", "Cheat Cart", "Homebrew", "Debug Hardware"
    };
    
    LOG_INFO("PIO: Device set to %s", 
             (device_type < 5) ? device_names[device_type] : "Unknown");
    
    // Update status based on device type
    pio_update_status(pio);
}

/**
 * @brief Read 8-bit value from PIO register
 */
uint8_t pio_load8(Pio* pio, uint32_t offset) {
    pio->access_count++;
    
    switch (offset) {
        case PIO_DATA_REG:
            if (pio->device_present) {
                return pio_handle_data_read(pio);
            } else {
                LOG_TRACE("PIO: Data register read (no device) = 0xFF");
                return 0xFF; // Open bus value
            }
            
        case PIO_STATUS_REG:
            pio_update_status(pio);
            LOG_TRACE("PIO: Status register read = 0x%02X", pio->status_reg);
            return pio->status_reg;
            
        case PIO_CTRL_REG:
            LOG_TRACE("PIO: Control register read = 0x%02X", pio->control_reg);
            return pio->control_reg;
            
        default:
            if (offset < PIO_SIZE) {
                // Other areas of expansion region - return open bus
                LOG_TRACE("PIO: Read from offset 0x%08X (open bus) = 0xFF", offset);
                return 0xFF;
            } else {
                LOG_WARN("PIO: Read from invalid offset 0x%08X", offset);
                return 0xFF;
            }
    }
}

/**
 * @brief Read 16-bit value from PIO register
 */
uint16_t pio_load16(Pio* pio, uint32_t offset) {
    // Combine two 8-bit reads
    uint8_t low = pio_load8(pio, offset);
    uint8_t high = pio_load8(pio, offset + 1);
    return ((uint16_t)high << 8) | low;
}

/**
 * @brief Read 32-bit value from PIO register
 */
uint32_t pio_load32(Pio* pio, uint32_t offset) {
    // Combine four 8-bit reads
    uint8_t byte0 = pio_load8(pio, offset);
    uint8_t byte1 = pio_load8(pio, offset + 1);
    uint8_t byte2 = pio_load8(pio, offset + 2);
    uint8_t byte3 = pio_load8(pio, offset + 3);
    return ((uint32_t)byte3 << 24) | ((uint32_t)byte2 << 16) | 
           ((uint32_t)byte1 << 8) | byte0;
}

/**
 * @brief Write 8-bit value to PIO register
 */
void pio_store8(Pio* pio, uint32_t offset, uint8_t value) {
    pio->access_count++;
    
    switch (offset) {
        case PIO_DATA_REG:
            if (pio->device_present) {
                pio_handle_data_write(pio, value);
            } else {
                LOG_TRACE("PIO: Data register write 0x%02X (no device, ignored)", value);
            }
            pio->data_reg = value;
            break;
            
        case PIO_STATUS_REG:
            // Status register is mostly read-only, but some bits might be writable
            LOG_TRACE("PIO: Status register write 0x%02X (mostly read-only)", value);
            break;
            
        case PIO_CTRL_REG:
            LOG_TRACE("PIO: Control register write 0x%02X", value);
            pio_handle_control_write(pio, value);
            break;
            
        default:
            if (offset < PIO_SIZE) {
                // Other areas of expansion region - ignore writes
                LOG_TRACE("PIO: Write to offset 0x%08X = 0x%02X (ignored)", offset, value);
            } else {
                LOG_WARN("PIO: Write to invalid offset 0x%08X = 0x%02X", offset, value);
            }
            break;
    }
}

/**
 * @brief Write 16-bit value to PIO register
 */
void pio_store16(Pio* pio, uint32_t offset, uint16_t value) {
    // Split into two 8-bit writes
    pio_store8(pio, offset, (uint8_t)(value & 0xFF));
    pio_store8(pio, offset + 1, (uint8_t)((value >> 8) & 0xFF));
}

/**
 * @brief Write 32-bit value to PIO register
 */
void pio_store32(Pio* pio, uint32_t offset, uint32_t value) {
    // Split into four 8-bit writes
    pio_store8(pio, offset, (uint8_t)(value & 0xFF));
    pio_store8(pio, offset + 1, (uint8_t)((value >> 8) & 0xFF));
    pio_store8(pio, offset + 2, (uint8_t)((value >> 16) & 0xFF));
    pio_store8(pio, offset + 3, (uint8_t)((value >> 24) & 0xFF));
}

/**
 * @brief Handle data write to parallel port
 */
void pio_handle_data_write(Pio* pio, uint8_t data) {
    pio->bytes_sent++;
    
    // Route to appropriate device handler
    switch (pio->device_type) {
        case PIO_DEVICE_PRINTER:
            pio_handle_printer_data(pio, data);
            break;
        case PIO_DEVICE_CHEAT_CART:
            pio_handle_cheat_cart_data(pio, data);
            break;
        case PIO_DEVICE_HOMEBREW:
            pio_handle_homebrew_data(pio, data);
            break;
        case PIO_DEVICE_DEBUG:
            pio_handle_debug_data(pio, data);
            break;
        default:
            LOG_TRACE("PIO: Data write 0x%02X to unknown device", data);
            break;
    }
    
    // Update status after data transfer
    pio_update_status(pio);
}

/**
 * @brief Handle data read from parallel port
 */
uint8_t pio_handle_data_read(Pio* pio) {
    uint8_t data = 0xFF; // Default open bus value
    
    if (!pio_rx_buffer_empty(pio)) {
        data = pio_pop_rx_data(pio);
        pio->bytes_received++;
    }
    
    LOG_TRACE("PIO: Data read = 0x%02X", data);
    return data;
}

/**
 * @brief Update status register based on current state
 */
void pio_update_status(Pio* pio) {
    pio->status_reg = 0;
    
    if (pio->device_present) {
        pio->status_reg |= PIO_STATUS_SELECT; // Device selected
        
        if (!pio->busy && !pio_rx_buffer_empty(pio)) {
            pio->status_reg |= PIO_STATUS_ACK; // Data available
        }
        
        if (pio->busy) {
            // Clear busy bit (status is inverted for busy)
        } else {
            pio->status_reg |= PIO_STATUS_BUSY; // Not busy (inverted)
        }
        
        if (pio->error) {
            pio->status_reg |= PIO_STATUS_ERROR;
        }
    } else {
        // No device connected
        pio->status_reg |= PIO_STATUS_ERROR | PIO_STATUS_PAPER | PIO_STATUS_BUSY;
    }
}

/**
 * @brief Handle control register write
 */
void pio_handle_control_write(Pio* pio, uint8_t value) {
    pio->control_reg = value;
    pio->irq_enabled = (value & PIO_CTRL_IRQ_ENABLE) != 0;
    
    if (value & PIO_CTRL_INIT) {
        LOG_TRACE("PIO: Initialize signal asserted");
        pio_clear_buffers(pio);
        pio->error = false;
    }
    
    if (value & PIO_CTRL_STROBE) {
        LOG_TRACE("PIO: Strobe signal asserted");
        // Strobe typically triggers data transfer
    }
}

// Buffer Management Functions
bool pio_tx_buffer_full(Pio* pio) {
    return pio->tx_count >= PIO_BUFFER_SIZE;
}

bool pio_tx_buffer_empty(Pio* pio) {
    return pio->tx_count == 0;
}

bool pio_rx_buffer_full(Pio* pio) {
    return pio->rx_count >= PIO_BUFFER_SIZE;
}

bool pio_rx_buffer_empty(Pio* pio) {
    return pio->rx_count == 0;
}

void pio_push_tx_data(Pio* pio, uint8_t data) {
    if (pio_tx_buffer_full(pio)) return;
    
    int write_pos = (pio->tx_pos + pio->tx_count) % PIO_BUFFER_SIZE;
    pio->tx_buffer[write_pos] = data;
    pio->tx_count++;
}

uint8_t pio_pop_rx_data(Pio* pio) {
    if (pio_rx_buffer_empty(pio)) return 0xFF;
    
    uint8_t data = pio->rx_buffer[pio->rx_pos];
    pio->rx_pos = (pio->rx_pos + 1) % PIO_BUFFER_SIZE;
    pio->rx_count--;
    return data;
}

void pio_clear_buffers(Pio* pio) {
    pio->tx_count = 0;
    pio->rx_count = 0;
    pio->tx_pos = 0;
    pio->rx_pos = 0;
    memset(pio->tx_buffer, 0, PIO_BUFFER_SIZE);
    memset(pio->rx_buffer, 0, PIO_BUFFER_SIZE);
}

// Device-specific handlers (stubs)
void pio_handle_printer_data(Pio* pio, uint8_t data) {
    LOG_TRACE("PIO: Printer data 0x%02X ('%c') (stub)", data, (data >= 32 && data < 127) ? data : '.');
    pio_push_tx_data(pio, data);
}

void pio_handle_cheat_cart_data(Pio* pio, uint8_t data) {
    LOG_TRACE("PIO: Cheat cart data 0x%02X (stub)", data);
    pio_push_tx_data(pio, data);
    // TODO: Implement Action Replay/GameShark protocol
}

void pio_handle_homebrew_data(Pio* pio, uint8_t data) {
    LOG_TRACE("PIO: Homebrew device data 0x%02X (stub)", data);
    pio_push_tx_data(pio, data);
    // TODO: Implement homebrew device protocol
}

void pio_handle_debug_data(Pio* pio, uint8_t data) {
    LOG_TRACE("PIO: Debug hardware data 0x%02X (stub)", data);
    pio_push_tx_data(pio, data);
    // TODO: Implement debug protocol
}

// Update and Status Functions
void pio_update(Pio* pio) {
    // TODO: Process pending transfers, update timing
    pio_update_status(pio);
}

bool pio_is_device_ready(Pio* pio) {
    return pio->device_present && !pio->busy && !pio->error;
}

bool pio_has_pending_data(Pio* pio) {
    return !pio_rx_buffer_empty(pio) || !pio_tx_buffer_empty(pio);
}