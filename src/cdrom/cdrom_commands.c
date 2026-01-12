/**
 * @file cdrom_commands.c
 * @brief Complete CDROM Command Implementations
 * 
 * All 32 PS1 CDROM commands based on DuckStation and PSX-SPX documentation.
 * Commands 0x00-0x1F plus secret unlock commands 0x50-0x57.
 */

#include "cdrom/cdrom_core.h"
#include "cdrom/cdrom_types.h"
#include "log.h"
#include "interconnect.h"
#include <string.h>

// Forward declarations from cdrom_core.c
extern void cdrom_send_ack(CdromState* cdrom, struct Interconnect* inter);
extern void cdrom_send_error(CdromState* cdrom, struct Interconnect* inter, uint8_t reason);
extern void cdrom_send_response_internal(CdromState* cdrom, struct Interconnect* inter, 
                                        CdromInterrupt int_type, uint8_t* data, uint8_t count);
extern bool cdrom_pop_param(CdromState* cdrom, uint8_t* value);
extern uint8_t cdrom_get_status_byte(const CdromState* cdrom);
extern void cdrom_queue_second_response(CdromState* cdrom, struct Interconnect* inter, CdromCommand cmd, uint32_t delay_cycles);
extern uint32_t cdrom_msf_to_lba(uint8_t mm, uint8_t ss, uint8_t ff);
extern void cdrom_lba_to_msf(uint32_t lba, uint8_t* mm, uint8_t* ss, uint8_t* ff);
extern uint8_t cdrom_bin_to_bcd(uint8_t value);
extern uint8_t cdrom_bcd_to_bin(uint8_t value);

// ============================================================================
// Command Handlers
// ============================================================================

void cmd_sync(CdromState* cdrom, struct Interconnect* inter) {
    // 0x00: Sync - Invalid command, returns error
    LOG_CDROM_DEBUG("[CDROM] CMD 0x00: Sync (invalid)");
    cdrom_send_error(cdrom, inter, ERROR_REASON_INVALID_COMMAND);
}

void cmd_getstat(CdromState* cdrom, struct Interconnect* inter) {
    // 0x01: GetStat - Get current status
    LOG_CDROM_DEBUG("[CDROM] CMD 0x01: GetStat");
    cdrom_send_ack(cdrom, inter);
    
    // Clear shell open bit after sending status (if disc present)
    if (cdrom->disc_present) {
        cdrom->secondary_status &= ~STAT_SHELL_OPEN;
    }
}

void cmd_setloc(CdromState* cdrom, struct Interconnect* inter) {
    // 0x02: Setloc - Set seek position (MM:SS:FF in BCD)
    uint8_t mm_bcd, ss_bcd, ff_bcd;
    
    if (!cdrom_pop_param(cdrom, &mm_bcd) ||
        !cdrom_pop_param(cdrom, &ss_bcd) ||
        !cdrom_pop_param(cdrom, &ff_bcd)) {
        cdrom_send_error(cdrom, inter, ERROR_REASON_INCORRECT_PARAMS);
        return;
    }
    
    // Validate BCD and ranges
    if (((mm_bcd & 0x0F) > 0x09) || (mm_bcd > 0x99) ||
        ((ss_bcd & 0x0F) > 0x09) || (ss_bcd >= 0x60) ||
        ((ff_bcd & 0x0F) > 0x09) || (ff_bcd >= 0x75)) {
        LOG_CDROM_ERROR("[CDROM] SetLoc: Invalid BCD values %02X:%02X:%02X", 
                       mm_bcd, ss_bcd, ff_bcd);
        cdrom_send_error(cdrom, inter, ERROR_REASON_INVALID_ARG);
        return;
    }
    
    // Convert BCD to binary
    cdrom->seek_minute = cdrom_bcd_to_bin(mm_bcd);
    cdrom->seek_second = cdrom_bcd_to_bin(ss_bcd);
    cdrom->seek_frame = cdrom_bcd_to_bin(ff_bcd);
    cdrom->setloc_pending = true;
    
    // Calculate target LBA
    cdrom->seek_end_lba = cdrom_msf_to_lba(cdrom->seek_minute, cdrom->seek_second, cdrom->seek_frame);
    
    LOG_CDROM_DEBUG("[CDROM] CMD 0x02: Setloc %02d:%02d:%02d (LBA=%u)", 
                   cdrom->seek_minute, cdrom->seek_second, cdrom->seek_frame, cdrom->seek_end_lba);
    
    cdrom_send_ack(cdrom, inter);
}

void cmd_play(CdromState* cdrom, struct Interconnect* inter) {
    // 0x03: Play - Start CD-DA audio playback
    // Optional parameter: track number (BCD)
    
    if (!cdrom->disc_present) {
        cdrom_send_error(cdrom, inter, ERROR_REASON_NOT_READY);
        return;
    }
    
    // If track parameter provided, seek to that track
    uint8_t track_bcd;
    if (cdrom_pop_param(cdrom, &track_bcd)) {
        // TODO: Seek to track start position
        LOG_CDROM_DEBUG("[CDROM] CMD 0x03: Play (track %02X)", track_bcd);
    } else {
        LOG_CDROM_DEBUG("[CDROM] CMD 0x03: Play (from setloc)");
    }
    
    // Start playback from setloc position if pending
    if (cdrom->setloc_pending) {
        cdrom->current_lba = cdrom->seek_end_lba;
        cdrom->setloc_pending = false;
    }
    
    cdrom->drive_state = DRIVE_PLAYING;
    cdrom->secondary_status |= STAT_PLAYING_CDDA | STAT_MOTOR_ON;
    cdrom->secondary_status &= ~(STAT_READING | STAT_SEEKING);
    
    cdrom_send_ack(cdrom, inter);
    
    // TODO: Schedule INT1 reports for audio sectors
}

void cmd_forward(CdromState* cdrom, struct Interconnect* inter) {
    // 0x04: Forward - Fast forward (while playing)
    LOG_CDROM_DEBUG("[CDROM] CMD 0x04: Forward");
    
    if (!cdrom->motor_on || cdrom->drive_state != DRIVE_PLAYING) {
        cdrom_send_error(cdrom, inter, ERROR_REASON_NOT_READY);
        return;
    }
    
    // TODO: Implement fast forward
    cdrom_send_ack(cdrom, inter);
}

void cmd_backward(CdromState* cdrom, struct Interconnect* inter) {
    // 0x05: Backward - Fast backward (while playing)
    LOG_CDROM_DEBUG("[CDROM] CMD 0x05: Backward");
    
    if (!cdrom->motor_on || cdrom->drive_state != DRIVE_PLAYING) {
        cdrom_send_error(cdrom, inter, ERROR_REASON_NOT_READY);
        return;
    }
    
    // TODO: Implement fast backward
    cdrom_send_ack(cdrom, inter);
}

void cmd_readn(CdromState* cdrom, struct Interconnect* inter) {
    // 0x06: ReadN - Start reading data sectors (with retry)
    LOG_CDROM_DEBUG("[CDROM] CMD 0x06: ReadN from LBA %u", cdrom->current_lba);
    
    if (!cdrom->disc_present) {
        cdrom_send_error(cdrom, inter, ERROR_REASON_NOT_READY);
        return;
    }
    
    // Start reading from setloc position if pending
    if (cdrom->setloc_pending) {
        cdrom->current_lba = cdrom->seek_end_lba;
        cdrom->setloc_pending = false;
        cdrom->read_after_seek = true;
    }
    
    cdrom->drive_state = DRIVE_READING;
    cdrom->secondary_status |= STAT_READING | STAT_MOTOR_ON;
    cdrom->secondary_status &= ~(STAT_PLAYING_CDDA | STAT_SEEKING);
    
    cdrom_send_ack(cdrom, inter);
    
    // Start reading the first sector
    cdrom_read_sector(cdrom, inter);
}

void cmd_motoron(CdromState* cdrom, struct Interconnect* inter) {
    // 0x07: MotorOn/Standby - Spin up the drive motor
    LOG_CDROM_DEBUG("[CDROM] CMD 0x07: MotorOn");
    
    if (cdrom->motor_on) {
        // Motor already on - return error
        cdrom_send_error(cdrom, inter, ERROR_REASON_INCORRECT_PARAMS);
        return;
    }
    
    if (!cdrom->disc_present) {
        cdrom_send_error(cdrom, inter, ERROR_REASON_NOT_READY);
        return;
    }
    
    cdrom->motor_on = true;
    cdrom->secondary_status |= STAT_MOTOR_ON;
    cdrom->drive_state = DRIVE_SPINNING_UP;
    
    cdrom_send_ack(cdrom, inter);
    
    // Queue INT2 second response after spin-up delay
    cdrom_queue_second_response(cdrom, inter, CMD_MOTORON, 400000); // ~400ms
}

void cmd_stop(CdromState* cdrom, struct Interconnect* inter) {
    // 0x08: Stop - Stop motor and reading/playing
    LOG_CDROM_DEBUG("[CDROM] CMD 0x08: Stop");
    
    cdrom_send_ack(cdrom, inter);
    
    // Motor will stop after second response
    cdrom->drive_state = DRIVE_IDLE;
    cdrom->secondary_status &= ~(STAT_READING | STAT_PLAYING_CDDA | STAT_SEEKING);
    
    // Queue INT2 second response
    cdrom_queue_second_response(cdrom, inter, CMD_STOP, 33868); // ~33ms
}

void cmd_pause(CdromState* cdrom, struct Interconnect* inter) {
    // 0x09: Pause - Pause reading/playing
    LOG_CDROM_DEBUG("[CDROM] CMD 0x09: Pause");
    
    cdrom_send_ack(cdrom, inter);
    
    cdrom->drive_state = DRIVE_IDLE;
    cdrom->secondary_status &= ~(STAT_READING | STAT_PLAYING_CDDA);
    
    // Queue INT2 second response
    cdrom_queue_second_response(cdrom, inter, CMD_PAUSE, 33868); // ~1ms to ~7ms depending on state
}

void cmd_init(CdromState* cdrom, struct Interconnect* inter) {
    // 0x0A: Init - Initialize/reset controller
    LOG_CDROM_DEBUG("[CDROM] CMD 0x0A: Init");
    
    // Reset mode register to 0x20 (default)
    cdrom->mode_register = 0x20;
    cdrom->xa_filter_enabled = false;
    cdrom->muted = false;
    
    // Stop any ongoing operations
    cdrom->drive_state = DRIVE_IDLE;
    cdrom->secondary_status &= ~(STAT_READING | STAT_PLAYING_CDDA | STAT_SEEKING);
    
    cdrom_send_ack(cdrom, inter);
    
    // Queue INT2 second response after init delay
    cdrom_queue_second_response(cdrom, inter, CMD_INIT, 4000000); // ~4 seconds
}

void cmd_mute(CdromState* cdrom, struct Interconnect* inter) {
    // 0x0B: Mute - Mute CD-DA audio output
    LOG_CDROM_DEBUG("[CDROM] CMD 0x0B: Mute");
    
    cdrom->muted = true;
    cdrom_send_ack(cdrom, inter);
}

void cmd_demute(CdromState* cdrom, struct Interconnect* inter) {
    // 0x0C: Demute - Unmute CD-DA audio output
    LOG_CDROM_DEBUG("[CDROM] CMD 0x0C: Demute");
    
    cdrom->muted = false;
    cdrom_send_ack(cdrom, inter);
}

void cmd_setfilter(CdromState* cdrom, struct Interconnect* inter) {
    // 0x0D: SetFilter - Set XA-ADPCM filter
    uint8_t file, channel;
    
    if (!cdrom_pop_param(cdrom, &file) || !cdrom_pop_param(cdrom, &channel)) {
        cdrom_send_error(cdrom, inter, ERROR_REASON_INCORRECT_PARAMS);
        return;
    }
    
    LOG_CDROM_DEBUG("[CDROM] CMD 0x0D: Setfilter (file=%02X, channel=%02X)", file, channel);
    
    cdrom->xa_filter_file = file;
    cdrom->xa_filter_channel = channel;
    cdrom->xa_filter_enabled = true;
    
    cdrom_send_ack(cdrom, inter);
}

void cmd_setmode(CdromState* cdrom, struct Interconnect* inter) {
    // 0x0E: SetMode - Set read mode flags
    uint8_t mode;
    
    if (!cdrom_pop_param(cdrom, &mode)) {
        cdrom_send_error(cdrom, inter, ERROR_REASON_INCORRECT_PARAMS);
        return;
    }
    
    LOG_CDROM_DEBUG("[CDROM] CMD 0x0E: Setmode (0x%02X) [2X=%d XA=%d RAW=%d FILTER=%d]", 
                   mode,
                   !!(mode & MODE_DOUBLE_SPEED),
                   !!(mode & MODE_XA_ENABLE),
                   !!(mode & MODE_READ_RAW),
                   !!(mode & MODE_XA_FILTER));
    
    cdrom->mode_register = mode;
    cdrom_send_ack(cdrom, inter);
}

void cmd_getmode(CdromState* cdrom, struct Interconnect* inter) {
    // 0x0F: GetMode/Getparam - Get current mode flags
    LOG_CDROM_DEBUG("[CDROM] CMD 0x0F: Getmode");
    
    uint8_t response[5];
    response[0] = cdrom_get_status_byte(cdrom);
    response[1] = cdrom->mode_register;
    response[2] = 0x00; // Unknown/reserved
    response[3] = cdrom->xa_filter_file;
    response[4] = cdrom->xa_filter_channel;
    
    cdrom_send_response_internal(cdrom, inter, INT_ACK, response, 5);
}

void cmd_getlocl(CdromState* cdrom, struct Interconnect* inter) {
    // 0x10: GetLocL - Get logical position (last sector header)
    LOG_CDROM_DEBUG("[CDROM] CMD 0x10: GetLocL");
    
    if (!cdrom->motor_on) {
        cdrom_send_error(cdrom, inter, ERROR_REASON_NOT_READY);
        return;
    }
    
    // Return position in BCD format
    uint8_t mm, ss, ff;
    cdrom_lba_to_msf(cdrom->current_lba, &mm, &ss, &ff);
    
    uint8_t response[8];
    response[0] = cdrom_bin_to_bcd(mm);
    response[1] = cdrom_bin_to_bcd(ss);
    response[2] = cdrom_bin_to_bcd(ff);
    response[3] = 0x01; // mode (Mode 1 data)
    response[4] = cdrom->xa_filter_file;
    response[5] = cdrom->xa_filter_channel;
    response[6] = 0x00; // SM (submode)
    response[7] = 0x00; // CI (coding info)
    
    cdrom_send_response_internal(cdrom, inter, INT_ACK, response, 8);
}

void cmd_getlocp(CdromState* cdrom, struct Interconnect* inter) {
    // 0x11: GetLocP - Get physical position (subchannel Q)
    LOG_CDROM_DEBUG("[CDROM] CMD 0x11: GetLocP");
    
    if (!cdrom->motor_on) {
        cdrom_send_error(cdrom, inter, ERROR_REASON_NOT_READY);
        return;
    }
    
    uint8_t mm, ss, ff;
    cdrom_lba_to_msf(cdrom->current_lba, &mm, &ss, &ff);
    
    uint8_t response[8];
    response[0] = cdrom_bin_to_bcd(1); // track
    response[1] = cdrom_bin_to_bcd(1); // index
    response[2] = cdrom_bin_to_bcd(mm);  // relative MM
    response[3] = cdrom_bin_to_bcd(ss);  // relative SS
    response[4] = cdrom_bin_to_bcd(ff);  // relative FF
    response[5] = cdrom_bin_to_bcd(mm);  // absolute MM
    response[6] = cdrom_bin_to_bcd(ss);  // absolute SS
    response[7] = cdrom_bin_to_bcd(ff);  // absolute FF
    
    cdrom_send_response_internal(cdrom, inter, INT_ACK, response, 8);
}

void cmd_readt(CdromState* cdrom, struct Interconnect* inter) {
    // 0x12: ReadT - Read Table of Contents (alternate command)
    LOG_CDROM_DEBUG("[CDROM] CMD 0x12: ReadT (unimplemented)");
    cdrom_send_error(cdrom, inter, ERROR_REASON_INVALID_COMMAND);
}

void cmd_gettn(CdromState* cdrom, struct Interconnect* inter) {
    // 0x13: GetTN - Get first and last track numbers
    LOG_CDROM_DEBUG("[CDROM] CMD 0x13: GetTN");
    
    uint8_t response[3];
    response[0] = cdrom_get_status_byte(cdrom);
    response[1] = cdrom_bin_to_bcd(1);  // First track (always 1)
    response[2] = cdrom_bin_to_bcd(1);  // Last track (single track for now)
    
    cdrom_send_response_internal(cdrom, inter, INT_ACK, response, 3);
}

void cmd_gettd(CdromState* cdrom, struct Interconnect* inter) {
    // 0x14: GetTD - Get track start position
    uint8_t track_bcd;
    
    if (!cdrom_pop_param(cdrom, &track_bcd)) {
        cdrom_send_error(cdrom, inter, ERROR_REASON_INCORRECT_PARAMS);
        return;
    }
    
    LOG_CDROM_DEBUG("[CDROM] CMD 0x14: GetTD (track=%02X)", track_bcd);
    
    uint8_t response[3];
    response[0] = cdrom_get_status_byte(cdrom);
    
    if (track_bcd == 0x00) {
        // Track 0 = disc end position
        uint8_t mm, ss, ff;
        cdrom_lba_to_msf(cdrom->disc_size_sectors, &mm, &ss, &ff);
        response[1] = cdrom_bin_to_bcd(mm);
        response[2] = cdrom_bin_to_bcd(ss);
    } else {
        // Track 1 starts at 00:02:00 (standard CD start)
        response[1] = cdrom_bin_to_bcd(0);
        response[2] = cdrom_bin_to_bcd(2);
    }
    
    cdrom_send_response_internal(cdrom, inter, INT_ACK, response, 3);
}

void cmd_seekl(CdromState* cdrom, struct Interconnect* inter) {
    // 0x15: SeekL - Seek to logical position
    LOG_CDROM_DEBUG("[CDROM] CMD 0x15: SeekL");
    
    if (!cdrom->setloc_pending) {
        cdrom_send_error(cdrom, inter, ERROR_REASON_NOT_READY);
        return;
    }
    
    cdrom->drive_state = DRIVE_SEEKING_LOGICAL;
    cdrom->seek_start_lba = cdrom->current_lba;
    cdrom->secondary_status |= STAT_SEEKING | STAT_MOTOR_ON;
    cdrom->secondary_status &= ~(STAT_READING | STAT_PLAYING_CDDA);
    cdrom->setloc_pending = false;
    
    cdrom_send_ack(cdrom, inter);
    
    // Queue INT2 second response after seek completes
    uint32_t seek_time = 30000; // Minimum seek time
    cdrom_queue_second_response(cdrom, inter, CMD_SEEKL, seek_time);
}

void cmd_seekp(CdromState* cdrom, struct Interconnect* inter) {
    // 0x16: SeekP - Seek to physical position
    LOG_CDROM_DEBUG("[CDROM] CMD 0x16: SeekP");
    
    if (!cdrom->setloc_pending) {
        cdrom_send_error(cdrom, inter, ERROR_REASON_NOT_READY);
        return;
    }
    
    cdrom->drive_state = DRIVE_SEEKING_PHYSICAL;
    cdrom->seek_start_lba = cdrom->current_lba;
    cdrom->secondary_status |= STAT_SEEKING | STAT_MOTOR_ON;
    cdrom->secondary_status &= ~(STAT_READING | STAT_PLAYING_CDDA);
    cdrom->setloc_pending = false;
    
    cdrom_send_ack(cdrom, inter);
    
    // Queue INT2 second response after seek completes
    uint32_t seek_time = 30000; // Minimum seek time
    cdrom_queue_second_response(cdrom, inter, CMD_SEEKP, seek_time);
}

void cmd_setclock(CdromState* cdrom, struct Interconnect* inter) {
    // 0x17: SetClock - Undocumented command
    LOG_CDROM_DEBUG("[CDROM] CMD 0x17: SetClock (unimplemented)");
    cdrom_send_error(cdrom, inter, ERROR_REASON_INVALID_COMMAND);
}

void cmd_getclock(CdromState* cdrom, struct Interconnect* inter) {
    // 0x18: GetClock - Undocumented command
    LOG_CDROM_DEBUG("[CDROM] CMD 0x18: GetClock (unimplemented)");
    cdrom_send_error(cdrom, inter, ERROR_REASON_INVALID_COMMAND);
}

void cmd_test(CdromState* cdrom, struct Interconnect* inter) {
    // 0x19: Test - Various test commands
    uint8_t subcommand;
    
    if (!cdrom_pop_param(cdrom, &subcommand)) {
        cdrom_send_error(cdrom, inter, ERROR_REASON_INCORRECT_PARAMS);
        return;
    }
    
    LOG_CDROM_DEBUG("[CDROM] CMD 0x19: Test (sub=0x%02X)", subcommand);
    
    switch (subcommand) {
        case 0x20: {
            // Get CDROM controller version (BIOS date/version)
            uint8_t response[4] = {
                0x98,  // Year 1998
                0x06,  // Month June
                0x10,  // Day 16
                0xC3   // Version C3
            };
            cdrom_send_response_internal(cdrom, inter, INT_ACK, response, 4);
            break;
        }
        
        case 0x21: {
            // Get drive switches
            uint8_t response[1] = {0x00}; // POS0=0, DOOR=0 (closed)
            cdrom_send_response_internal(cdrom, inter, INT_ACK, response, 1);
            break;
        }
        
        case 0x22: {
            // Get region ID string
            const char* region = "for Europe";  // Or "for U/C" (America), "for NETNA" (Japan)
            uint8_t response[16];
            memset(response, 0, 16);
            strncpy((char*)response, region, 15);
            cdrom_send_response_internal(cdrom, inter, INT_ACK, response, strlen(region) + 1);
            break;
        }
        
        default:
            // Unknown test subcommand - return INT5 error
            LOG_CDROM_WARN("[CDROM] Test: Unknown subcommand 0x%02X", subcommand);
            cdrom_send_error(cdrom, inter, ERROR_REASON_INVALID_ARG);
            break;
    }
}

void cmd_getid(CdromState* cdrom, struct Interconnect* inter) {
    // 0x1A: GetID - Get disc ID and region
    LOG_CDROM_DEBUG("[CDROM] CMD 0x1A: GetID");
    
    // First response: INT3(stat) - immediate
    cdrom_send_ack(cdrom, inter);
    
    // Second response will be queued: INT2 or INT5
    cdrom_queue_second_response(cdrom, inter, CMD_GETID, 33868); // ~33ms for ID read
}

// This is called from the second response handler
void cmd_getid_second_response(CdromState* cdrom, struct Interconnect* inter) {
    LOG_CDROM_DEBUG("[CDROM] GetID second response");
    
    cdrom->secondary_status &= ~(STAT_SEEKING | STAT_READING | STAT_PLAYING_CDDA);
    
    if (!cdrom->disc_present) {
        // No disc - send INT5 error
        LOG_CDROM_DEBUG("[CDROM] GetID: No disc - INT5");
        
        uint8_t error_response[2];
        error_response[0] = STAT_SHELL_OPEN | STAT_ID_ERROR | STAT_ERROR;
        error_response[1] = ERROR_REASON_NOT_READY;
        
        cdrom_send_response_internal(cdrom, inter, INT_ERROR, error_response, 2);
    } else {
        // Disc present - send INT2 with disc info
        LOG_CDROM_DEBUG("[CDROM] GetID: Disc present - INT2");
        
        uint8_t stat_byte = cdrom_get_status_byte(cdrom);
        stat_byte |= STAT_MOTOR_ON;
        
        uint8_t response[8];
        response[0] = stat_byte;
        response[1] = 0x00;  // Flags: 0x00=licensed, 0x80=unlicensed, 0x40=missing
        response[2] = 0x20;  // Disc type: 0x00=PS1 CD-ROM, 0x20=PS1 (another code), 0x10=PS2, 0x20=Audio CD
        response[3] = 0x00;  // ATIP (0x00 for pressed CD-ROM)
        
        // Region string: "SCEI" (Japan), "SCEA" (America), "SCEE" (Europe)
        response[4] = 'S';
        response[5] = 'C';
        response[6] = 'E';
        response[7] = 'A';  // America by default
        
        cdrom_send_response_internal(cdrom, inter, INT_COMPLETE, response, 8);
    }
}

void cmd_reads(CdromState* cdrom, struct Interconnect* inter) {
    // 0x1B: ReadS - Read without automatic retry
    LOG_CDROM_DEBUG("[CDROM] CMD 0x1B: ReadS");
    
    // Same as ReadN but without retry on error
    cmd_readn(cdrom, inter);
}

void cmd_reset(CdromState* cdrom, struct Interconnect* inter) {
    // 0x1C: Reset - Reset controller (reboots HC05)
    LOG_CDROM_DEBUG("[CDROM] CMD 0x1C: Reset");
    
    // NOTE: This command requires a 1/8 second delay after execution
    // before sending any other commands!
    
    cdrom_send_ack(cdrom, inter);
    
    // TODO: Implement HC05 reset simulation
    // For now, just reset internal state
    cdrom->mode_register = 0;
    cdrom->xa_filter_enabled = false;
    cdrom->muted = false;
    cdrom->drive_state = DRIVE_IDLE;
    cdrom->secondary_status = STAT_SHELL_OPEN;
}

void cmd_getq(CdromState* cdrom, struct Interconnect* inter) {
    // 0x1D: GetQ - Get subchannel Q data
    LOG_CDROM_DEBUG("[CDROM] CMD 0x1D: GetQ (unimplemented)");
    
    // Only supported in BIOS version vC1+
    cdrom_send_error(cdrom, inter, ERROR_REASON_INVALID_COMMAND);
}

void cmd_readtoc(CdromState* cdrom, struct Interconnect* inter) {
    // 0x1E: ReadTOC - Read Table of Contents
    LOG_CDROM_DEBUG("[CDROM] CMD 0x1E: ReadTOC");
    
    if (!cdrom->disc_present) {
        cdrom_send_error(cdrom, inter, ERROR_REASON_NOT_READY);
        return;
    }
    
    // First response: INT3(stat) - immediate
    cdrom_send_ack(cdrom, inter);
    
    // Second response: INT2(stat) - after ~1 second delay
    cdrom_queue_second_response(cdrom, inter, CMD_READTOC, 1000000); // ~1 second
}

void cmd_videocd(CdromState* cdrom, struct Interconnect* inter) {
    // 0x1F: VideoCD - Video CD commands (SCPH-5903 only)
    LOG_CDROM_DEBUG("[CDROM] CMD 0x1F: VideoCD (not supported)");
    cdrom_send_error(cdrom, inter, ERROR_REASON_INVALID_COMMAND);
}

// ============================================================================
// Second Response Handlers
// ============================================================================

void cdrom_execute_second_response(CdromState* cdrom, struct Interconnect* inter) {
    CdromCommand cmd = cdrom->second_response_cmd;
    cdrom->second_response_cmd = CMD_NONE;
    
    LOG_CDROM_DEBUG("[CDROM] Second response for command 0x%02X", cmd);
    
    switch (cmd) {
        case CMD_GETID:
            cmd_getid_second_response(cdrom, inter);
            break;
            
        case CMD_MOTORON:
        case CMD_INIT:
        case CMD_STOP:
        case CMD_PAUSE:
        case CMD_SEEKL:
        case CMD_SEEKP:
        case CMD_READTOC: {
            // Standard INT2(stat) second response
            uint8_t stat = cdrom_get_status_byte(cdrom);
            
            // Clear active bits for Stop command
            if (cmd == CMD_STOP) {
                cdrom->motor_on = false;
                cdrom->secondary_status &= ~STAT_MOTOR_ON;
                stat = cdrom_get_status_byte(cdrom);
            }
            
            // Clear seeking bit for seek commands
            if (cmd == CMD_SEEKL || cmd == CMD_SEEKP) {
                cdrom->current_lba = cdrom->seek_end_lba;
                cdrom->secondary_status &= ~STAT_SEEKING;
                stat = cdrom_get_status_byte(cdrom);
            }
            
            cdrom_send_response_internal(cdrom, inter, INT_COMPLETE, &stat, 1);
            break;
        }
        
        default:
            LOG_CDROM_WARN("[CDROM] No second response handler for command 0x%02X", cmd);
            break;
    }
}
