#include "memcard.h"
#include "log.h"
#include <string.h>
#include <stdio.h>

/**
 * @brief Initialize memory card to default state
 * Based on PSX-SPX specifications for Memory Card protocol
 */
void memcard_init(Memcard* card) {
    memset(card, 0, sizeof(Memcard));
    
    // Initialize status
    card->present = false;
    card->initialized = false;
    card->write_protected = false;
    card->status_flags = 0;
    
    // Clear memory
    memset(card->data, 0xFF, MEMCARD_TOTAL_SIZE); // Fresh card has 0xFF
    
    // Initialize communication state
    card->state = MEMCARD_STATE_IDLE;
    card->current_cmd = 0;
    card->current_addr = 0;
    card->frame_addr = 0;
    
    // Clear buffers
    memset(card->tx_buffer, 0, MEMCARD_FRAME_SIZE);
    memset(card->rx_buffer, 0, MEMCARD_FRAME_SIZE);
    card->tx_pos = 0;
    card->rx_pos = 0;
    card->transfer_count = 0;
    
    // Reset checksum
    card->checksum = 0;
    card->expected_checksum = 0;
    
    // Clear error state
    card->error = false;
    card->error_code = 0;
    
    // Clear statistics
    card->read_count = 0;
    card->write_count = 0;
    card->error_count = 0;
    
    // Clear file path
    memset(card->file_path, 0, sizeof(card->file_path));
    card->file_dirty = false;
    
    LOG_INFO("Memory Card initialized (128KB, stubbed implementation)");
}

/**
 * @brief Reset memory card
 */
void memcard_reset(Memcard* card) {
    bool was_present = card->present;
    char saved_path[256];
    strcpy(saved_path, card->file_path);
    
    memcard_init(card);
    
    // Restore presence and file path
    card->present = was_present;
    strcpy(card->file_path, saved_path);
    
    LOG_INFO("Memory Card reset");
}

/**
 * @brief Insert memory card
 */
void memcard_insert(Memcard* card) {
    card->present = true;
    card->status_flags |= MEMCARD_STATUS_PRESENT | MEMCARD_STATUS_READY;
    memcard_init_directory(card);
    LOG_INFO("Memory Card inserted");
}

/**
 * @brief Remove memory card
 */
void memcard_remove(Memcard* card) {
    if (card->file_dirty) {
        memcard_flush_to_file(card);
    }
    
    card->present = false;
    card->status_flags = 0;
    card->state = MEMCARD_STATE_IDLE;
    
    LOG_INFO("Memory Card removed");
}

/**
 * @brief Set write protection
 */
void memcard_set_write_protect(Memcard* card, bool protected) {
    card->write_protected = protected;
    if (protected) {
        card->status_flags |= MEMCARD_STATUS_WRITE_PROTECT;
    } else {
        card->status_flags &= ~MEMCARD_STATUS_WRITE_PROTECT;
    }
    
    LOG_INFO("Memory Card write protection %s", protected ? "enabled" : "disabled");
}

/**
 * @brief Load memory card data from .mcr file
 */
bool memcard_load_from_file(Memcard* card, const char* filepath) {
    FILE* file = fopen(filepath, "rb");
    if (!file) {
        LOG_WARN("Memory Card: Could not open file '%s' for reading", filepath);
        return false;
    }
    
    size_t bytes_read = fread(card->data, 1, MEMCARD_TOTAL_SIZE, file);
    fclose(file);
    
    if (bytes_read != MEMCARD_TOTAL_SIZE) {
        LOG_WARN("Memory Card: File '%s' has wrong size (%zu bytes, expected %d)", 
                 filepath, bytes_read, MEMCARD_TOTAL_SIZE);
        return false;
    }
    
    strcpy(card->file_path, filepath);
    card->file_dirty = false;
    
    LOG_INFO("Memory Card: Loaded from '%s'", filepath);
    return true;
}

/**
 * @brief Save memory card data to .mcr file
 */
bool memcard_save_to_file(Memcard* card, const char* filepath) {
    FILE* file = fopen(filepath, "wb");
    if (!file) {
        LOG_WARN("Memory Card: Could not open file '%s' for writing", filepath);
        return false;
    }
    
    size_t bytes_written = fwrite(card->data, 1, MEMCARD_TOTAL_SIZE, file);
    fclose(file);
    
    if (bytes_written != MEMCARD_TOTAL_SIZE) {
        LOG_WARN("Memory Card: Could not write complete file '%s'", filepath);
        return false;
    }
    
    card->file_dirty = false;
    
    LOG_INFO("Memory Card: Saved to '%s'", filepath);
    return true;
}

/**
 * @brief Set file path for automatic saving
 */
void memcard_set_file_path(Memcard* card, const char* filepath) {
    strcpy(card->file_path, filepath);
    LOG_TRACE("Memory Card: File path set to '%s'", filepath);
}

/**
 * @brief Flush dirty data to file
 */
void memcard_flush_to_file(Memcard* card) {
    if (card->file_dirty && card->file_path[0]) {
        memcard_save_to_file(card, card->file_path);
    }
}

/**
 * @brief Exchange byte with memory card (main SIO interface)
 */
uint8_t memcard_exchange_byte(Memcard* card, uint8_t data) {
    if (!card->present) {
        return 0xFF; // No card inserted
    }
    
    uint8_t response = 0xFF;
    
    switch (card->state) {
        case MEMCARD_STATE_IDLE:
            // Waiting for initial command
            if (data == MEMCARD_CMD_READ || data == MEMCARD_CMD_WRITE || data == MEMCARD_CMD_ID) {
                card->current_cmd = data;
                card->state = MEMCARD_STATE_WAIT_CMD;
                response = MEMCARD_RESP_ID1;
            }
            break;
            
        case MEMCARD_STATE_WAIT_CMD:
            // Send second ID byte
            card->state = MEMCARD_STATE_WAIT_ADDR_HI;
            response = MEMCARD_RESP_ID2;
            break;
            
        case MEMCARD_STATE_WAIT_ADDR_HI:
            // Receive high byte of address
            card->current_addr = (data << 8);
            card->state = MEMCARD_STATE_WAIT_ADDR_LO;
            response = MEMCARD_RESP_ACK;
            break;
            
        case MEMCARD_STATE_WAIT_ADDR_LO:
            // Receive low byte of address
            card->current_addr |= data;
            card->frame_addr = card->current_addr * MEMCARD_FRAME_SIZE;
            
            if (card->current_cmd == MEMCARD_CMD_READ) {
                memcard_process_read_command(card);
                card->state = MEMCARD_STATE_READ_DATA;
                card->tx_pos = 0;
            } else if (card->current_cmd == MEMCARD_CMD_WRITE) {
                card->state = MEMCARD_STATE_WRITE_DATA;
                card->rx_pos = 0;
            } else if (card->current_cmd == MEMCARD_CMD_ID) {
                memcard_process_id_command(card);
                card->state = MEMCARD_STATE_COMPLETE;
            }
            response = MEMCARD_RESP_ACK;
            break;
            
        case MEMCARD_STATE_READ_DATA:
            // Send data bytes
            if (card->tx_pos < MEMCARD_FRAME_SIZE) {
                response = card->tx_buffer[card->tx_pos++];
                memcard_update_checksum(card, response);
                if (card->tx_pos >= MEMCARD_FRAME_SIZE) {
                    card->state = MEMCARD_STATE_CHECKSUM;
                }
            }
            break;
            
        case MEMCARD_STATE_WRITE_DATA:
            // Receive data bytes
            if (card->rx_pos < MEMCARD_FRAME_SIZE) {
                card->rx_buffer[card->rx_pos++] = data;
                memcard_update_checksum(card, data);
                if (card->rx_pos >= MEMCARD_FRAME_SIZE) {
                    card->state = MEMCARD_STATE_CHECKSUM;
                }
            }
            response = MEMCARD_RESP_ACK;
            break;
            
        case MEMCARD_STATE_CHECKSUM:
            if (card->current_cmd == MEMCARD_CMD_READ) {
                // Send checksum
                response = card->checksum;
                card->state = MEMCARD_STATE_COMPLETE;
            } else if (card->current_cmd == MEMCARD_CMD_WRITE) {
                // Receive and verify checksum
                if (data == card->checksum) {
                    memcard_process_write_command(card);
                    response = MEMCARD_RESP_COMPLETE;
                } else {
                    card->error = true;
                    card->error_code = 0xFF; // Checksum error
                    response = 0xFF;
                }
                card->state = MEMCARD_STATE_COMPLETE;
            }
            break;
            
        case MEMCARD_STATE_COMPLETE:
            // Transaction complete
            response = MEMCARD_RESP_COMPLETE;
            card->state = MEMCARD_STATE_IDLE;
            break;
            
        default:
            response = 0xFF;
            break;
    }
    
    return response;
}

/**
 * @brief Begin transfer (called when SIO CS is asserted)
 */
void memcard_begin_transfer(Memcard* card) {
    card->state = MEMCARD_STATE_IDLE;
    card->checksum = 0;
    card->error = false;
}

/**
 * @brief End transfer (called when SIO CS is deasserted)
 */
void memcard_end_transfer(Memcard* card) {
    if (card->state != MEMCARD_STATE_COMPLETE && card->state != MEMCARD_STATE_IDLE) {
        LOG_WARN("Memory Card: Transfer ended prematurely (state=%d)", card->state);
        card->error = true;
        card->error_count++;
    }
    card->state = MEMCARD_STATE_IDLE;
}

// Command Processing Functions (stubs)
void memcard_process_read_command(Memcard* card) {
    if (card->frame_addr + MEMCARD_FRAME_SIZE <= MEMCARD_TOTAL_SIZE) {
        memcpy(card->tx_buffer, &card->data[card->frame_addr], MEMCARD_FRAME_SIZE);
        card->read_count++;
        LOG_TRACE("Memory Card: Read frame at 0x%04X", card->frame_addr);
    } else {
        memset(card->tx_buffer, 0xFF, MEMCARD_FRAME_SIZE);
        card->error = true;
        LOG_WARN("Memory Card: Read out of bounds at 0x%04X", card->frame_addr);
    }
}

void memcard_process_write_command(Memcard* card) {
    if (card->write_protected) {
        card->error = true;
        LOG_WARN("Memory Card: Write attempt to write-protected card");
        return;
    }
    
    if (card->frame_addr + MEMCARD_FRAME_SIZE <= MEMCARD_TOTAL_SIZE) {
        memcpy(&card->data[card->frame_addr], card->rx_buffer, MEMCARD_FRAME_SIZE);
        card->file_dirty = true;
        card->write_count++;
        LOG_TRACE("Memory Card: Write frame at 0x%04X", card->frame_addr);
    } else {
        card->error = true;
        LOG_WARN("Memory Card: Write out of bounds at 0x%04X", card->frame_addr);
    }
}

void memcard_process_id_command(Memcard* card) {
    // Return memory card identification
    memset(card->tx_buffer, 0, MEMCARD_FRAME_SIZE);
    card->tx_buffer[0] = 0x5A; // Memory card ID
    card->tx_buffer[1] = 0x5D; // Memory card ID
    LOG_TRACE("Memory Card: ID command processed");
}

// Directory Management (stubs)
void memcard_init_directory(Memcard* card) {
    // Initialize directory in first block
    memset(&card->directory, 0xFF, sizeof(MemcardDirectory));
    LOG_TRACE("Memory Card: Directory initialized");
}

int memcard_find_free_slot(Memcard* card) {
    // TODO: Search for free directory slot
    (void)card;
    return -1; // No free slots (stub)
}

// Checksum Functions
uint8_t memcard_calculate_checksum(uint8_t* data, int length) {
    uint8_t checksum = 0;
    for (int i = 0; i < length; i++) {
        checksum ^= data[i];
    }
    return checksum;
}

void memcard_update_checksum(Memcard* card, uint8_t data) {
    card->checksum ^= data;
}

bool memcard_verify_checksum(Memcard* card) {
    return card->checksum == card->expected_checksum;
}

// Status Functions
uint8_t memcard_get_status(Memcard* card) {
    memcard_update_status(card);
    return card->status_flags;
}

void memcard_update_status(Memcard* card) {
    card->status_flags = 0;
    
    if (card->present) {
        card->status_flags |= MEMCARD_STATUS_PRESENT | MEMCARD_STATUS_READY;
    }
    
    if (card->write_protected) {
        card->status_flags |= MEMCARD_STATUS_WRITE_PROTECT;
    }
    
    if (card->error) {
        card->status_flags |= MEMCARD_STATUS_ERROR;
    }
    
    if (card->state != MEMCARD_STATE_IDLE) {
        card->status_flags |= MEMCARD_STATUS_BUSY;
    }
}

// Query Functions
bool memcard_is_ready(Memcard* card) {
    return card->present && !card->error && (card->state == MEMCARD_STATE_IDLE);
}

bool memcard_is_busy(Memcard* card) {
    return card->state != MEMCARD_STATE_IDLE;
}

int memcard_get_free_blocks(Memcard* card) {
    // TODO: Count free blocks in directory
    (void)card;
    return MEMCARD_TOTAL_BLOCKS - 1; // Stub: all but directory block
}

int memcard_get_used_blocks(Memcard* card) {
    // TODO: Count used blocks in directory
    (void)card;
    return 1; // Stub: just directory block
}

// Update and Maintenance
void memcard_update(Memcard* card) {
    // TODO: Handle timing, auto-save, etc.
    (void)card;
}

void memcard_format(Memcard* card) {
    memset(card->data, 0xFF, MEMCARD_TOTAL_SIZE);
    memcard_init_directory(card);
    card->file_dirty = true;
    LOG_INFO("Memory Card formatted");
}