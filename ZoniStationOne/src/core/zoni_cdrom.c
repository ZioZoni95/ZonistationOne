/**
 * @file zoni_cdrom.c
 * @brief PlayStation CD-ROM emulation implementation
 * 
 * This file implements the CD-ROM emulation for the PlayStation's CD-ROM controller.
 */

#include "zoni_cdrom.h"
#include "zoni_common.h"
#include <string.h>

// CD-ROM register addresses
#define CDROM_BASE_ADDR 0x1F801800
#define CDROM_DATA 0x1F801800
#define CDROM_STATUS 0x1F801801
#define CDROM_MODE 0x1F801802
#define CDROM_CONTROL 0x1F801803

zoni_error_t zoni_cdrom_init(zoni_cdrom_t* cdrom, const zoni_cdrom_config_t* config) {
    if (!cdrom) {
        return ZONI_ERROR_INVALID_PARAMETER;
    }
    
    // Clear CD-ROM structure
    memset(cdrom, 0, sizeof(zoni_cdrom_t));
    
    // Set configuration
    if (config) {
        cdrom->config = *config;
    } else {
        // Default configuration
        cdrom->config.enable_cdrom = true;
        cdrom->config.enable_audio = true;
        cdrom->config.enable_video = false;
        cdrom->config.iso_path = NULL;
    }
    
    // Initialize registers
    cdrom->status = ZONI_CDROM_STATUS_READY | ZONI_CDROM_STATUS_DISC;
    cdrom->mode = 0x00;
    cdrom->control = 0x00;
    cdrom->interrupt = 0x00;
    
    // Initialize command state
    cdrom->command = 0x00;
    memset(cdrom->response, 0, sizeof(cdrom->response));
    cdrom->response_count = 0;
    cdrom->response_index = 0;
    
    // Initialize drive state
    cdrom->disc_present = true;  // Assume disc is present
    cdrom->motor_on = false;
    cdrom->reading = false;
    cdrom->playing = false;
    cdrom->paused = false;
    
    // Initialize position
    cdrom->current_sector = 0;
    cdrom->start_sector = 0;
    cdrom->end_sector = 0;
    
    // Initialize buffer
    memset(cdrom->sector_buffer, 0, PSX_CDROM_SECTOR_SIZE);
    cdrom->buffer_pos = 0;
    cdrom->buffer_size = 0;
    
    // Initialize state
    cdrom->initialized = true;
    cdrom->busy = false;
    
    // Initialize timing
    cdrom->last_update = 0;
    cdrom->sector_time = 0;
    
    zoni_log(ZONI_LOG_INFO, "CD-ROM initialized successfully");
    
    return ZONI_SUCCESS;
}

void zoni_cdrom_shutdown(zoni_cdrom_t* cdrom) {
    if (!cdrom) {
        return;
    }
    
    cdrom->initialized = false;
    
    zoni_log(ZONI_LOG_INFO, "CD-ROM shutdown");
}

void zoni_cdrom_reset(zoni_cdrom_t* cdrom) {
    if (!cdrom || !cdrom->initialized) {
        return;
    }
    
    // Reset registers
    cdrom->status = ZONI_CDROM_STATUS_READY | ZONI_CDROM_STATUS_DISC;
    cdrom->mode = 0x00;
    cdrom->control = 0x00;
    cdrom->interrupt = 0x00;
    
    // Reset command state
    cdrom->command = 0x00;
    memset(cdrom->response, 0, sizeof(cdrom->response));
    cdrom->response_count = 0;
    cdrom->response_index = 0;
    
    // Reset drive state
    cdrom->motor_on = false;
    cdrom->reading = false;
    cdrom->playing = false;
    cdrom->paused = false;
    
    // Reset position
    cdrom->current_sector = 0;
    cdrom->start_sector = 0;
    cdrom->end_sector = 0;
    
    // Reset buffer
    memset(cdrom->sector_buffer, 0, PSX_CDROM_SECTOR_SIZE);
    cdrom->buffer_pos = 0;
    cdrom->buffer_size = 0;
    
    cdrom->busy = false;
    
    zoni_log(ZONI_LOG_DEBUG, "CD-ROM reset");
}

zoni_error_t zoni_cdrom_write_register(zoni_cdrom_t* cdrom, u32 address, u8 value) {
    if (!cdrom || !cdrom->initialized) {
        return ZONI_ERROR_INVALID_PARAMETER;
    }
    
    u32 offset = address - CDROM_BASE_ADDR;
    
            zoni_log(ZONI_LOG_INFO, "CD-ROM write: 0x%08X = 0x%02X", address, value);
    
    switch (offset) {
        case 0x00: // Data register
            // This is where commands are sent
            return zoni_cdrom_execute_command(cdrom, value);
            
        case 0x01: // Status register
            // Status is read-only, ignore writes
            break;
            
        case 0x02: // Mode register
            cdrom->mode = value;
            break;
            
        case 0x03: // Control register
            cdrom->control = value;
            break;
            
        default:
            zoni_log(ZONI_LOG_DEBUG, "CD-ROM unknown write: 0x%08X = 0x%02X", address, value);
            break;
    }
    
    return ZONI_SUCCESS;
}

u8 zoni_cdrom_read_register(zoni_cdrom_t* cdrom, u32 address) {
    if (!cdrom || !cdrom->initialized) {
        return 0xFF;
    }
    
    u32 offset = address - CDROM_BASE_ADDR;
    
    switch (offset) {
        case 0x00: // Data register
            return zoni_cdrom_get_response(cdrom);
            
        case 0x01: // Status register
            return cdrom->status;
            
        case 0x02: // Mode register
            return cdrom->mode;
            
        case 0x03: // Control register
            return cdrom->control;
            
        default:
            zoni_log(ZONI_LOG_DEBUG, "CD-ROM unknown read: 0x%08X", address);
            break;
    }
    
    return 0xFF;
}

zoni_error_t zoni_cdrom_execute_command(zoni_cdrom_t* cdrom, u8 command) {
    if (!cdrom || !cdrom->initialized) {
        return ZONI_ERROR_INVALID_PARAMETER;
    }
    
    cdrom->command = command;
    cdrom->busy = true;
    cdrom->response_count = 0;
    cdrom->response_index = 0;
    
    zoni_log(ZONI_LOG_DEBUG, "CD-ROM command: 0x%02X", command);
    
    switch (command) {
        case ZONI_CDROM_CMD_SYNC:
            // Sync command - respond with 0x00
            zoni_cdrom_send_response(cdrom, 0x00);
            break;
            
        case ZONI_CDROM_CMD_GETSTAT:
            // Get status - respond with current status
            zoni_cdrom_send_response(cdrom, cdrom->status);
            break;
            
        case ZONI_CDROM_CMD_INIT:
            // Initialize drive
            cdrom->status = ZONI_CDROM_STATUS_READY | ZONI_CDROM_STATUS_DISC;
            cdrom->motor_on = true;
            zoni_cdrom_send_response(cdrom, 0x00);
            break;
            
        case ZONI_CDROM_CMD_RESET:
            // Reset drive
            zoni_cdrom_reset(cdrom);
            zoni_cdrom_send_response(cdrom, 0x00);
            break;
            
        case ZONI_CDROM_CMD_STOP:
            // Stop drive
            cdrom->reading = false;
            cdrom->playing = false;
            cdrom->paused = false;
            zoni_cdrom_send_response(cdrom, 0x00);
            break;
            
        case ZONI_CDROM_CMD_PAUSE:
            // Pause drive
            cdrom->paused = true;
            zoni_cdrom_send_response(cdrom, 0x00);
            break;
            
        case ZONI_CDROM_CMD_READN:
            // Read next sector
            cdrom->reading = true;
            cdrom->current_sector++;
            zoni_cdrom_send_response(cdrom, 0x00);
            break;
            
        case ZONI_CDROM_CMD_READTOC:
            // Read table of contents
            zoni_cdrom_send_response(cdrom, 0x00);
            zoni_cdrom_send_response(cdrom, 0x01);
            zoni_cdrom_send_response(cdrom, 0x00);
            zoni_cdrom_send_response(cdrom, 0x00);
            zoni_cdrom_send_response(cdrom, 0x00);
            zoni_cdrom_send_response(cdrom, 0x00);
            zoni_cdrom_send_response(cdrom, 0x00);
            zoni_cdrom_send_response(cdrom, 0x00);
            break;
            
        default:
            // Unknown command - respond with error
            zoni_log(ZONI_LOG_DEBUG, "CD-ROM unknown command: 0x%02X", command);
            zoni_cdrom_send_response(cdrom, 0xFF);
            break;
    }
    
    cdrom->busy = false;
    
    return ZONI_SUCCESS;
}

zoni_error_t zoni_cdrom_send_response(zoni_cdrom_t* cdrom, u8 response) {
    if (!cdrom || !cdrom->initialized) {
        return ZONI_ERROR_INVALID_PARAMETER;
    }
    
    if (cdrom->response_count < sizeof(cdrom->response)) {
        cdrom->response[cdrom->response_count] = response;
        cdrom->response_count++;
    }
    
    return ZONI_SUCCESS;
}

u8 zoni_cdrom_get_response(zoni_cdrom_t* cdrom) {
    if (!cdrom || !cdrom->initialized) {
        return 0xFF;
    }
    
    if (cdrom->response_index < cdrom->response_count) {
        u8 response = cdrom->response[cdrom->response_index];
        cdrom->response_index++;
        return response;
    }
    
    return 0xFF;
}

zoni_error_t zoni_cdrom_read_sector(zoni_cdrom_t* cdrom, u32 sector, u8* buffer) {
    if (!cdrom || !cdrom->initialized || !buffer) {
        return ZONI_ERROR_INVALID_PARAMETER;
    }
    
    // For now, just return empty sector data
    memset(buffer, 0, PSX_CDROM_SECTOR_SIZE);
    
    cdrom->current_sector = sector;
    
    return ZONI_SUCCESS;
}

zoni_error_t zoni_cdrom_seek_sector(zoni_cdrom_t* cdrom, u32 sector) {
    if (!cdrom || !cdrom->initialized) {
        return ZONI_ERROR_INVALID_PARAMETER;
    }
    
    cdrom->current_sector = sector;
    
    return ZONI_SUCCESS;
}

zoni_error_t zoni_cdrom_update(zoni_cdrom_t* cdrom) {
    if (!cdrom || !cdrom->initialized) {
        return ZONI_ERROR_INVALID_PARAMETER;
    }
    
    // Update CD-ROM state (for now, just a placeholder)
    
    return ZONI_SUCCESS;
}

// Debug functions
void zoni_cdrom_dump_registers(zoni_cdrom_t* cdrom) {
    if (!cdrom || !cdrom->initialized) {
        return;
    }
    
    zoni_log(ZONI_LOG_INFO, "CD-ROM Registers:");
    zoni_log(ZONI_LOG_INFO, "  Status: 0x%02X", cdrom->status);
    zoni_log(ZONI_LOG_INFO, "  Mode: 0x%02X", cdrom->mode);
    zoni_log(ZONI_LOG_INFO, "  Control: 0x%02X", cdrom->control);
    zoni_log(ZONI_LOG_INFO, "  Interrupt: 0x%02X", cdrom->interrupt);
    zoni_log(ZONI_LOG_INFO, "  Command: 0x%02X", cdrom->command);
}

void zoni_cdrom_dump_status(zoni_cdrom_t* cdrom) {
    if (!cdrom || !cdrom->initialized) {
        return;
    }
    
    zoni_log(ZONI_LOG_INFO, "CD-ROM Status:");
    zoni_log(ZONI_LOG_INFO, "  Disc Present: %s", cdrom->disc_present ? "Yes" : "No");
    zoni_log(ZONI_LOG_INFO, "  Motor On: %s", cdrom->motor_on ? "Yes" : "No");
    zoni_log(ZONI_LOG_INFO, "  Reading: %s", cdrom->reading ? "Yes" : "No");
    zoni_log(ZONI_LOG_INFO, "  Playing: %s", cdrom->playing ? "Yes" : "No");
    zoni_log(ZONI_LOG_INFO, "  Paused: %s", cdrom->paused ? "Yes" : "No");
    zoni_log(ZONI_LOG_INFO, "  Current Sector: %u", cdrom->current_sector);
    zoni_log(ZONI_LOG_INFO, "  Busy: %s", cdrom->busy ? "Yes" : "No");
} 