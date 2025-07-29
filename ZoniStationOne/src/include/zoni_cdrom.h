/**
 * @file zoni_cdrom.h
 * @brief PlayStation CD-ROM emulation for ZoniStationOne
 * 
 * This file defines the CD-ROM structure and emulation interface
 * for the PlayStation's CD-ROM controller.
 */

#ifndef ZONI_CDROM_H
#define ZONI_CDROM_H

#include "zoni_common.h"

// PlayStation CD-ROM constants
#define PSX_CDROM_SECTOR_SIZE 2048
#define PSX_CDROM_MAX_SECTORS 1000000  // ~2GB CD-ROM

// CD-ROM status register bits
typedef enum {
    ZONI_CDROM_STATUS_READY = 0x00000001,
    ZONI_CDROM_STATUS_SEEK = 0x00000002,
    ZONI_CDROM_STATUS_READ = 0x00000004,
    ZONI_CDROM_STATUS_PLAY = 0x00000008,
    ZONI_CDROM_STATUS_PAUSE = 0x00000010,
    ZONI_CDROM_STATUS_STOP = 0x00000020,
    ZONI_CDROM_STATUS_DISC = 0x00000040,
    ZONI_CDROM_STATUS_ERROR = 0x00000080
} zoni_cdrom_status_t;

// CD-ROM commands
typedef enum {
    ZONI_CDROM_CMD_SYNC = 0x00,
    ZONI_CDROM_CMD_GETSTAT = 0x01,
    ZONI_CDROM_CMD_SETLOC = 0x02,
    ZONI_CDROM_CMD_PLAY = 0x03,
    ZONI_CDROM_CMD_FORWARD = 0x04,
    ZONI_CDROM_CMD_BACKWARD = 0x05,
    ZONI_CDROM_CMD_READN = 0x06,
    ZONI_CDROM_CMD_STANDBY = 0x07,
    ZONI_CDROM_CMD_STOP = 0x08,
    ZONI_CDROM_CMD_PAUSE = 0x09,
    ZONI_CDROM_CMD_INIT = 0x0A,
    ZONI_CDROM_CMD_MUTE = 0x0B,
    ZONI_CDROM_CMD_DEMUTE = 0x0C,
    ZONI_CDROM_CMD_RESET = 0x0D,
    ZONI_CDROM_CMD_OPEN = 0x0E,
    ZONI_CDROM_CMD_CLOSE = 0x0F,
    ZONI_CDROM_CMD_SEEKL = 0x10,
    ZONI_CDROM_CMD_SEEKP = 0x11,
    ZONI_CDROM_CMD_READTOC = 0x12,
    ZONI_CDROM_CMD_VIDEO = 0x13,
    ZONI_CDROM_CMD_MUSIC = 0x14,
    ZONI_CDROM_CMD_FILTER = 0x15,
    ZONI_CDROM_CMD_SCAN = 0x16,
    ZONI_CDROM_CMD_ERASE = 0x17,
    ZONI_CDROM_CMD_WRITE = 0x18,
    ZONI_CDROM_CMD_READ = 0x19,
    ZONI_CDROM_CMD_TEST = 0x1A,
    ZONI_CDROM_CMD_ID = 0x1B,
    ZONI_CDROM_CMD_VERSION = 0x1C,
    ZONI_CDROM_CMD_DRIVESTATE = 0x1D,
    ZONI_CDROM_CMD_DISKNEW = 0x1E,
    ZONI_CDROM_CMD_CHDIR = 0x1F,
    ZONI_CDROM_CMD_READ1 = 0x20,
    ZONI_CDROM_CMD_READ2 = 0x21,
    ZONI_CDROM_CMD_READ3 = 0x22,
    ZONI_CDROM_CMD_GETTOC = 0x23,
    ZONI_CDROM_CMD_GETTOC2 = 0x24
} zoni_cdrom_command_t;

// CD-ROM configuration
typedef struct {
    bool enable_cdrom;
    bool enable_audio;
    bool enable_video;
    char* iso_path;  // Path to ISO file
} zoni_cdrom_config_t;

// CD-ROM state
typedef struct zoni_cdrom_s {
    // CD-ROM registers
    u8 status;
    u8 mode;
    u8 control;
    u8 interrupt;
    
    // Command state
    u8 command;
    u8 response[16];
    u8 response_count;
    u8 response_index;
    
    // Drive state
    bool disc_present;
    bool motor_on;
    bool reading;
    bool playing;
    bool paused;
    
    // Position
    u32 current_sector;
    u32 start_sector;
    u32 end_sector;
    
    // Data buffer
    u8 sector_buffer[PSX_CDROM_SECTOR_SIZE];
    u32 buffer_pos;
    u32 buffer_size;
    
    // Configuration
    zoni_cdrom_config_t config;
    
    // State
    bool initialized;
    bool busy;
    
    // Timing
    u64 last_update;
    u64 sector_time;
    
} zoni_cdrom_t;

// CD-ROM functions
zoni_error_t zoni_cdrom_init(zoni_cdrom_t* cdrom, const zoni_cdrom_config_t* config);
void zoni_cdrom_shutdown(zoni_cdrom_t* cdrom);
void zoni_cdrom_reset(zoni_cdrom_t* cdrom);

// CD-ROM control
zoni_error_t zoni_cdrom_write_register(zoni_cdrom_t* cdrom, u32 address, u8 value);
u8 zoni_cdrom_read_register(zoni_cdrom_t* cdrom, u32 address);

// CD-ROM commands
zoni_error_t zoni_cdrom_execute_command(zoni_cdrom_t* cdrom, u8 command);
zoni_error_t zoni_cdrom_send_response(zoni_cdrom_t* cdrom, u8 response);
u8 zoni_cdrom_get_response(zoni_cdrom_t* cdrom);

// CD-ROM data
zoni_error_t zoni_cdrom_read_sector(zoni_cdrom_t* cdrom, u32 sector, u8* buffer);
zoni_error_t zoni_cdrom_seek_sector(zoni_cdrom_t* cdrom, u32 sector);

// CD-ROM update
zoni_error_t zoni_cdrom_update(zoni_cdrom_t* cdrom);

// Debug functions
void zoni_cdrom_dump_registers(zoni_cdrom_t* cdrom);
void zoni_cdrom_dump_status(zoni_cdrom_t* cdrom);

#endif // ZONI_CDROM_H 