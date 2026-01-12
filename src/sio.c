#include "sio.h"
#include "controller.h"
#include "log.h"
#include <string.h>
#include <stdio.h>

// Forward declarations
static uint8_t handle_controller_transfer(Sio* sio, uint8_t tx_byte);
static uint8_t handle_memcard_transfer(Sio* sio, uint8_t tx_byte);

// SIO Status Register bits
#define STAT_TX_RDY_1      (1 << 0)  // Ready to send (TX FIFO not full)
#define STAT_RX_NOT_EMPTY  (1 << 1)  // RX FIFO not empty
#define STAT_TX_RDY_2      (1 << 2)  // Ready to send (TX finished)
#define STAT_RX_PARITY_ERR (1 << 3)  // RX Parity error
#define STAT_ACK           (1 << 7)  // ACK input level
#define STAT_IRQ           (1 << 9)  // Interrupt request

// SIO Control Register bits
#define CTRL_TX_ENABLE     (1 << 0)  // TX Enable
#define CTRL_SELECT        (1 << 1)  // /JOYn output (0=High/Idle, 1=Low/Select)
#define CTRL_RX_ENABLE     (1 << 2)  // RX Enable
#define CTRL_ACK_IRQ_ENABLE (1 << 10) // Acknowledge IRQ
#define CTRL_RESET         (1 << 6)  // Reset
#define CTRL_RX_IRQ_MODE   (1 << 8)  // RX Interrupt mode
#define CTRL_TX_IRQ_ENABLE (1 << 10) // TX Interrupt enable
#define CTRL_RX_IRQ_ENABLE (1 << 11) // RX Interrupt enable
#define CTRL_ACK_IRQ       (1 << 12) // Acknowledge IRQ enable

// Memory Card commands
#define MEMCARD_CMD_ACCESS    0x81  // Memory card access command
#define MEMCARD_CMD_READ      0x52  // Read sector
#define MEMCARD_CMD_WRITE     0x57  // Write sector
#define MEMCARD_CMD_GET_ID    0x53  // Get ID

// Memory Card responses
#define MEMCARD_FLAG_READY    0x5A  // Card ready
#define MEMCARD_FLAG_ERROR    0xFF  // Card error

void sio_init(Sio* sio) {
    memset(sio, 0, sizeof(Sio));
    
    // Initialize registers to default state
    sio->stat = STAT_TX_RDY_1 | STAT_TX_RDY_2;  // Ready to transmit
    sio->mode = 0x000D;  // Default mode: 8-bit, no parity
    sio->ctrl = 0x0000;  // Idle
    sio->baud = 0x0088;  // Default baud rate
    
    // No devices selected initially
    sio->selected_device = 0;
    sio->transfer_step = 0;
    
    // Initialize controller system
    controller_init();
    
    // Memory cards not present by default
    sio->card_slot1.present = false;
    sio->card_slot2.present = false;
    
    LOG_INFO("SIO initialized (controller ENABLED, memory card interface ready)");
}

bool sio_load_memcard(MemoryCard* card, const char* filepath) {
    FILE* file = fopen(filepath, "rb");
    if (!file) {
        LOG_WARN("Memory card file not found: %s (will create on first save)", filepath);
        return false;
    }
    
    size_t bytes_read = fread(card->data, 1, MEMCARD_SIZE, file);
    fclose(file);
    
    if (bytes_read != MEMCARD_SIZE) {
        LOG_ERROR("Invalid memory card file size: %zu bytes (expected %d)", 
                  bytes_read, MEMCARD_SIZE);
        return false;
    }
    
    strncpy(card->filepath, filepath, sizeof(card->filepath) - 1);
    card->filepath[sizeof(card->filepath) - 1] = '\0';
    card->present = true;
    card->dirty = false;
    
    LOG_INFO("Memory card loaded: %s (%d KB)", filepath, MEMCARD_SIZE / 1024);
    return true;
}

bool sio_save_memcard(MemoryCard* card) {
    if (!card->dirty || !card->present) {
        return true;  // Nothing to save
    }
    
    FILE* file = fopen(card->filepath, "wb");
    if (!file) {
        LOG_ERROR("Failed to open memory card file for writing: %s", card->filepath);
        return false;
    }
    
    size_t bytes_written = fwrite(card->data, 1, MEMCARD_SIZE, file);
    fclose(file);
    
    if (bytes_written != MEMCARD_SIZE) {
        LOG_ERROR("Failed to write complete memory card file: %zu/%d bytes", 
                  bytes_written, MEMCARD_SIZE);
        return false;
    }
    
    card->dirty = false;
    LOG_INFO("Memory card saved: %s", card->filepath);
    return true;
}

void sio_create_memcard(MemoryCard* card, const char* filepath) {
    // Initialize with formatted card data (proper PSX memory card format)
    memset(card->data, 0x00, MEMCARD_SIZE);
    
    // Memory card header (first 128 bytes of first sector)
    card->data[0] = 'M';
    card->data[1] = 'C';
    
    strncpy(card->filepath, filepath, sizeof(card->filepath) - 1);
    card->filepath[sizeof(card->filepath) - 1] = '\0';
    card->present = true;
    card->dirty = true;
    
    // Save immediately to create file
    sio_save_memcard(card);
    
    LOG_INFO("Created new memory card: %s", filepath);
}

// Handle data transfer when a byte is written to JOY_DATA
static void sio_handle_transfer(Sio* sio, uint8_t tx_byte) {
    uint8_t response = 0xFF;  // Default response (no device)
    
    // Check if device is selected
    if (!(sio->ctrl & CTRL_SELECT)) {
        // Not selected, reset state
        sio->selected_device = 0;
        sio->transfer_step = 0;
        sio->rx_data = 0xFF;
        return;
    }
    
    // First byte determines device type
    if (sio->transfer_step == 0) {
        if (tx_byte == 0x01) {
            // Controller command
            sio->selected_device = 1;
            response = 0xFF;  // Controller ID (digital pad)
            if (sio->controller_connected) {
                response = 0x41;  // Digital controller ID
            }
        } else if (tx_byte == MEMCARD_CMD_ACCESS) {
            // Memory card command
            sio->selected_device = 2;
            response = MEMCARD_FLAG_READY;  // Card present
            // Check if card is present in slot 1 (we'll use slot 1 for now)
            if (!sio->card_slot1.present) {
                response = MEMCARD_FLAG_ERROR;
            }
        } else {
            // Unknown command
            sio->selected_device = 0;
            response = 0xFF;
        }
        sio->transfer_step = 1;
    } else {
        // Subsequent bytes depend on device type
        if (sio->selected_device == 1) {
            // Controller transfer
            response = handle_controller_transfer(sio, tx_byte);
        } else if (sio->selected_device == 2) {
            // Memory card transfer
            response = handle_memcard_transfer(sio, tx_byte);
        }
        sio->transfer_step++;
    }
    
    // Store response
    sio->rx_data = response;
    sio->stat |= STAT_RX_NOT_EMPTY;
    
    // Set ACK bit (device acknowledged)
    if (sio->selected_device != 0) {
        sio->stat |= STAT_ACK;
    }
    
    LOG_SYSTEM_TRACE("SIO transfer: TX=0x%02x, RX=0x%02x, step=%d, device=%d", 
                  tx_byte, response, sio->transfer_step, sio->selected_device);
}

static uint8_t handle_controller_transfer(Sio* sio, uint8_t tx_byte) {
    // Digital controller protocol (PS1 standard)
    // Command 0x42: Read Digital Controller
    // Response format: 0x5A (ID) + button states (2 bytes)
    
    switch (sio->transfer_step) {
        case 1:  // Command byte
            if (tx_byte == 0x42) {  // Read Digital command
                return 0x5A;  // Controller ready ID
            }
            return 0xFF;  // Unknown command
            
        case 2:  // Tap mode (should be 0x00 for digital)
            return (controller_get_buttons() >> 8) & 0xFF;  // High byte of buttons
            
        case 3:  // Button data
            return controller_get_buttons() & 0xFF;  // Low byte of buttons
            
        default:
            return 0xFF;  // End of transfer
    }
}

static uint8_t handle_memcard_transfer(Sio* sio, uint8_t tx_byte) {
    // Memory card protocol (simplified)
    // This is a complex protocol - implementing basic read/write
    
    MemoryCard* card = &sio->card_slot1;  // Use slot 1
    
    if (!card->present) {
        return MEMCARD_FLAG_ERROR;
    }
    
    switch (sio->transfer_step) {
        case 1:  // Command
            sio->tx_buffer[0] = tx_byte;
            return MEMCARD_FLAG_READY;  // Card ready
            
        case 2:  // Acknowledge
            return 0x5A;  // ID
            
        case 3:  // MSB of address/sector
            sio->tx_buffer[1] = tx_byte;
            return 0x5D;  // Acknowledge
            
        case 4:  // LSB of address/sector
            sio->tx_buffer[2] = tx_byte;
            return 0x00;  // Acknowledge
            
        default:
            // Data transfer phase
            if (sio->tx_buffer[0] == MEMCARD_CMD_READ) {
                // Read command - return card data
                uint16_t sector = (sio->tx_buffer[1] << 8) | sio->tx_buffer[2];
                uint32_t offset = sector * MEMCARD_SECTOR_SIZE + (sio->transfer_step - 5);
                if (offset < MEMCARD_SIZE) {
                    return card->data[offset];
                }
            } else if (sio->tx_buffer[0] == MEMCARD_CMD_WRITE) {
                // Write command - store data
                uint16_t sector = (sio->tx_buffer[1] << 8) | sio->tx_buffer[2];
                uint32_t offset = sector * MEMCARD_SECTOR_SIZE + (sio->transfer_step - 5);
                if (offset < MEMCARD_SIZE) {
                    card->data[offset] = tx_byte;
                    card->dirty = true;
                }
                return 0x5A;  // Acknowledge
            }
            return 0xFF;
    }
}

// Register access implementations
uint8_t sio_read8(Sio* sio, uint32_t offset) {
    switch (offset) {
        case 0x00:  // JOY_DATA (1F801040h)
            sio->stat &= ~STAT_RX_NOT_EMPTY;  // Clear RX flag
            return sio->rx_data;
            
        case 0x04:  // JOY_STAT (1F801044h) - low byte
            return sio->stat & 0xFF;
            
        case 0x05:  // JOY_STAT high byte
            return (sio->stat >> 8) & 0xFF;
            
        default:
            LOG_WARN("SIO read8 from unknown offset: 0x%02x", offset);
            return 0xFF;
    }
}

uint16_t sio_read16(Sio* sio, uint32_t offset) {
    switch (offset) {
        case 0x04:  // JOY_STAT (1F801044h)
            return sio->stat;
            
        case 0x08:  // JOY_MODE (1F801048h)
            return sio->mode;
            
        case 0x0A:  // JOY_CTRL (1F80104Ah)
            return sio->ctrl;
            
        case 0x0E:  // JOY_BAUD (1F80104Eh)
            return sio->baud;
            
        default:
            LOG_WARN("SIO read16 from unknown offset: 0x%02x", offset);
            return 0xFFFF;
    }
}

uint32_t sio_read32(Sio* sio, uint32_t offset) {
    // Read 32-bit value (combine two 16-bit reads)
    uint16_t low = sio_read16(sio, offset);
    uint16_t high = sio_read16(sio, offset + 2);
    return (high << 16) | low;
}

void sio_write8(Sio* sio, uint32_t offset, uint8_t value) {
    switch (offset) {
        case 0x00:  // JOY_DATA (1F801040h)
            sio->tx_data = value;
            sio_handle_transfer(sio, value);
            break;
            
        default:
            LOG_WARN("SIO write8 to unknown offset: 0x%02x = 0x%02x", offset, value);
            break;
    }
}

void sio_write16(Sio* sio, uint32_t offset, uint16_t value) {
    switch (offset) {
        case 0x08:  // JOY_MODE (1F801048h)
            sio->mode = value;
            LOG_SYSTEM_DEBUG("SIO MODE = 0x%04x", value);
            break;
            
        case 0x0A:  // JOY_CTRL (1F80104Ah)
            sio->ctrl = value;
            
            // Handle reset
            if (value & CTRL_RESET) {
                sio->selected_device = 0;
                sio->transfer_step = 0;
                sio->stat = STAT_TX_RDY_1 | STAT_TX_RDY_2;
            }
            
            // Handle deselect
            if (!(value & CTRL_SELECT)) {
                sio->selected_device = 0;
                sio->transfer_step = 0;
            }
            
            LOG_SYSTEM_DEBUG("SIO CTRL = 0x%04x (select=%d, reset=%d)", 
                         value, !!(value & CTRL_SELECT), !!(value & CTRL_RESET));
            break;
            
        case 0x0E:  // JOY_BAUD (1F80104Eh)
            sio->baud = value;
            LOG_SYSTEM_DEBUG("SIO BAUD = 0x%04x", value);
            break;
            
        default:
            LOG_WARN("SIO write16 to unknown offset: 0x%02x = 0x%04x", offset, value);
            break;
    }
}

void sio_write32(Sio* sio, uint32_t offset, uint32_t value) {
    // Write 32-bit value (split into two 16-bit writes)
    sio_write16(sio, offset, value & 0xFFFF);
    sio_write16(sio, offset + 2, (value >> 16) & 0xFFFF);
}

void sio_set_button_state(Sio* sio, uint16_t buttons) {
    sio->button_state = buttons;
}
