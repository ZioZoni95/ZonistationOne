#ifndef CDROM_TYPES_H
#define CDROM_TYPES_H

/**
 * @file cdrom_types.h
 * @brief CDROM Controller Type Definitions
 * 
 * Modular CDROM system for PlayStation 1 emulator.
 * Thread-safe implementation based on DuckStation and PSX-SPX documentation.
 * 
 * Hardware Context:
 * - CDROM controller at 0x1F801800-0x1F801803 (4 8-bit registers)
 * - Bank-switched registers (4 banks controlled by low 2 bits of ADDRESS)
 * - Command/response FIFO system with HC05 microcontroller
 * - Sector buffer for data transfers (2048 bytes)
 * - XA-ADPCM audio decoding capability
 * 
 * Computational Complexity:
 * - Register access: O(1) - direct array indexing
 * - FIFO operations: O(1) - circular buffer with head/tail pointers
 * - Command dispatch: O(1) - direct function pointer lookup
 * - Sector read: O(1) - direct file seek + read
 * 
 * Thread Safety:
 * - All CDROM operations protected by recursive mutex
 * - Safe to call from CPU thread, DMA thread, and event scheduler
 */

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include "threading.h"

// ============================================================================
// CDROM Commands (PSX-SPX Section: CDROM Controller Command Summary)
// ============================================================================

typedef enum CdromCommand {
    CMD_SYNC       = 0x00,  ///< Get status
    CMD_GETSTAT    = 0x01,  ///< Get status
    CMD_SETLOC     = 0x02,  ///< Set seek position (minute, second, frame)
    CMD_PLAY       = 0x03,  ///< Play audio CD from current position
    CMD_FORWARD    = 0x04,  ///< Fast forward (hold)
    CMD_BACKWARD   = 0x05,  ///< Fast backward (hold)
    CMD_READN      = 0x06,  ///< Read with retry (data sectors)
    CMD_MOTORON    = 0x07,  ///< Spin up motor
    CMD_STOP       = 0x08,  ///< Stop motor
    CMD_PAUSE      = 0x09,  ///< Pause reading/playing
    CMD_INIT       = 0x0A,  ///< Initialize/reset controller
    CMD_MUTE       = 0x0B,  ///< Mute audio output
    CMD_DEMUTE     = 0x0C,  ///< Unmute audio output
    CMD_SETFILTER  = 0x0D,  ///< Set XA-ADPCM filter (file, channel)
    CMD_SETMODE    = 0x0E,  ///< Set read mode (speed, sector size, etc.)
    CMD_GETMODE    = 0x0F,  ///< Get current mode
    CMD_GETLOCL    = 0x10,  ///< Get logical position (last sector header)
    CMD_GETLOCP    = 0x11,  ///< Get physical position (subchannel Q)
    CMD_READT      = 0x12,  ///< Read Table of Contents
    CMD_GETTN      = 0x13,  ///< Get first/last track numbers
    CMD_GETTD      = 0x14,  ///< Get track start position
    CMD_SEEKL      = 0x15,  ///< Seek to logical position
    CMD_SEEKP      = 0x16,  ///< Seek to physical position
    CMD_SETCLOCK   = 0x17,  ///< Set clock (undocumented)
    CMD_GETCLOCK   = 0x18,  ///< Get clock (undocumented)
    CMD_TEST       = 0x19,  ///< Test commands (various subcodes)
    CMD_GETID      = 0x1A,  ///< Get disc ID and region
    CMD_READS      = 0x1B,  ///< Read without retry
    CMD_RESET      = 0x1C,  ///< Reset controller
    CMD_GETQ       = 0x1D,  ///< Get subchannel Q data
    CMD_READTOC    = 0x1E,  ///< Read TOC
    CMD_VIDEOCD    = 0x1F,  ///< Video CD commands
    CMD_NONE       = 0xFF   ///< No command pending
} CdromCommand;

// ============================================================================
// Drive State Machine
// ============================================================================

typedef enum DriveState {
    DRIVE_IDLE,                ///< Idle, motor may be spinning
    DRIVE_SHELL_OPENING,       ///< Shell is being opened
    DRIVE_SEEKING_LOGICAL,     ///< Seeking to logical position (SEEKL)
    DRIVE_SEEKING_PHYSICAL,    ///< Seeking to physical position (SEEKP)
    DRIVE_READING,             ///< Reading data sectors (READN/READS)
    DRIVE_PLAYING,             ///< Playing audio CD (PLAY)
    DRIVE_SPINNING_UP,         ///< Motor spinning up
    DRIVE_CHANGING_SESSION,    ///< Changing session (multi-session discs)
    DRIVE_SEEKING_IMPLICIT,    ///< Implicit seek before read/play
    DRIVE_SPEED_CHANGING       ///< Changing read speed or reading TOC
} DriveState;

// ============================================================================
// Interrupt Types (PSX-SPX Section: 0x1f801803 read banks 1/3 HINTSTS)
// ============================================================================

typedef enum CdromInterrupt {
    INT_NONE        = 0,  ///< No interrupt pending
    INT_DATA_READY  = 1,  ///< New sector or report available (INT1)
    INT_COMPLETE    = 2,  ///< Command complete (INT2)
    INT_ACK         = 3,  ///< Command acknowledged (INT3)
    INT_DATA_END    = 4,  ///< End of data reached (INT4)
    INT_ERROR       = 5   ///< Error occurred (INT5)
} CdromInterrupt;

// ============================================================================
// Status Register Bits (Secondary Status)
// ============================================================================

#define STAT_ERROR         0x01  ///< Error occurred
#define STAT_MOTOR_ON      0x02  ///< Motor is spinning
#define STAT_SEEK_ERROR    0x04  ///< Seek error
#define STAT_ID_ERROR      0x08  ///< GetID error (unlicensed disc)
#define STAT_SHELL_OPEN    0x10  ///< Shell is open
#define STAT_READING       0x20  ///< Reading data
#define STAT_SEEKING       0x40  ///< Seeking
#define STAT_PLAYING_CDDA  0x80  ///< Playing audio CD

// ============================================================================
// Mode Register Bits (SetMode command)
// ============================================================================

#define MODE_CDDA          0x01  ///< CD-DA mode (audio)
#define MODE_AUTO_PAUSE    0x02  ///< Auto-pause at end of track
#define MODE_REPORT_AUDIO  0x04  ///< Report audio position
#define MODE_XA_FILTER     0x08  ///< Enable XA-ADPCM filter
#define MODE_IGNORE_BIT    0x10  ///< Ignore sector size bit
#define MODE_READ_RAW      0x20  ///< Read raw 2340-byte sectors
#define MODE_XA_ENABLE     0x40  ///< Enable XA-ADPCM
#define MODE_DOUBLE_SPEED  0x80  ///< 2X speed (150 sectors/sec vs 75)

// ============================================================================
// Error Reasons (INT5 second byte)
// ============================================================================

#define ERROR_REASON_INVALID_ARG       0x10  ///< Invalid argument
#define ERROR_REASON_INCORRECT_PARAMS  0x20  ///< Wrong number of parameters
#define ERROR_REASON_INVALID_COMMAND   0x40  ///< Invalid command
#define ERROR_REASON_NOT_READY         0x80  ///< Drive not ready

// ============================================================================
// FIFO Structure (O(1) operations)
// ============================================================================

#define CDROM_FIFO_SIZE 16

typedef struct CdromFifo {
    uint8_t data[CDROM_FIFO_SIZE];
    uint8_t head;   ///< Read position (O(1) access)
    uint8_t tail;   ///< Write position (O(1) access)
    uint8_t count;  ///< Number of items (O(1) size check)
} CdromFifo;

// ============================================================================
// Sector Buffer (2352 bytes raw, 2048 bytes data)
// ============================================================================

#define CDROM_SECTOR_SIZE_RAW  2352  ///< Raw sector with sync + header + data + EDC/ECC
#define CDROM_SECTOR_SIZE_DATA 2048  ///< Data only (Mode 1)
#define CDROM_SECTOR_SYNC_SIZE 12    ///< Sync pattern size
#define CDROM_SECTOR_HEADER_SIZE 4   ///< Header size (minute:second:frame:mode)

typedef struct CdromSectorBuffer {
    uint8_t data[CDROM_SECTOR_SIZE_RAW];
    uint16_t position;  ///< Current read position in buffer
    uint16_t size;      ///< Valid data size in buffer
} CdromSectorBuffer;

// ============================================================================
// Main CDROM State Structure
// ============================================================================

typedef struct CdromState {
    // -------------------------------------------------------------------------
    // Hardware Registers (PSX-SPX Section: CDROM Controller I/O Ports)
    // -------------------------------------------------------------------------
    
    uint8_t status_register;         ///< 0x1f801800: HSTS/ADDRESS (bits 0-1 = bank)
    uint8_t interrupt_enable;        ///< 0x1f801802/3: HINTMSK
    uint8_t interrupt_flag;          ///< 0x1f801803: HINTSTS (bits 0-2)
    uint8_t mode_register;           ///< SetMode command state
    uint8_t request_register;        ///< BFRD, BFWR, SMEN flags
    
    // -------------------------------------------------------------------------
    // FIFOs (O(1) push/pop operations)
    // -------------------------------------------------------------------------
    
    CdromFifo param_fifo;            ///< Parameter FIFO (CPU -> CDROM)
    CdromFifo response_fifo;         ///< Response FIFO (CDROM -> CPU)
    CdromFifo async_response_fifo;   ///< Async response (for second responses)
    
    // -------------------------------------------------------------------------
    // Sector Buffers (double-buffered for streaming reads)
    // -------------------------------------------------------------------------
    
    CdromSectorBuffer sector_buffer; ///< Current sector being read by CPU
    uint8_t current_read_buffer;     ///< Active read buffer index (0-7)
    uint8_t current_write_buffer;    ///< Active write buffer index (0-7)
    
    // -------------------------------------------------------------------------
    // Command State
    // -------------------------------------------------------------------------
    
    CdromCommand current_command;    ///< Currently executing command
    CdromCommand pending_command;    ///< Next command to execute
    CdromCommand second_response_cmd;///< Command waiting for 2nd response
    uint8_t async_interrupt;         ///< Pending async interrupt type
    
    // -------------------------------------------------------------------------
    // Drive State Machine
    // -------------------------------------------------------------------------
    
    DriveState drive_state;          ///< Current drive state
    uint8_t secondary_status;        ///< STAT_* flags
    bool motor_on;                   ///< Motor spinning
    bool setloc_pending;             ///< Setloc position pending
    bool read_after_seek;            ///< Start reading after seek
    bool play_after_seek;            ///< Start playing after seek
    bool muted;                      ///< Audio output muted
    
    // -------------------------------------------------------------------------
    // Position Tracking (MSF format: Minute:Second:Frame)
    // -------------------------------------------------------------------------
    
    uint8_t seek_minute;             ///< Target seek position (minute)
    uint8_t seek_second;             ///< Target seek position (second)
    uint8_t seek_frame;              ///< Target seek position (frame)
    uint32_t current_lba;            ///< Current Logical Block Address
    uint32_t seek_start_lba;         ///< Seek start position
    uint32_t seek_end_lba;           ///< Seek end position
    
    // -------------------------------------------------------------------------
    // XA-ADPCM Filter (PSX-SPX Section: Setfilter command)
    // -------------------------------------------------------------------------
    
    bool xa_filter_enabled;          ///< XA filter active
    uint8_t xa_filter_file;          ///< Filter file number (0-255)
    uint8_t xa_filter_channel;       ///< Filter channel number (0-31)
    uint8_t xa_current_file;         ///< Current XA file number
    uint8_t xa_current_channel;      ///< Current XA channel number
    bool xa_current_set;             ///< XA current file/channel valid
    
    // -------------------------------------------------------------------------
    // Audio Volume Matrix (PSX-SPX: ATV0-ATV3 registers)
    // -------------------------------------------------------------------------
    
    uint8_t cd_audio_volume_l_to_l;  ///< Left input -> Left output
    uint8_t cd_audio_volume_l_to_r;  ///< Left input -> Right output
    uint8_t cd_audio_volume_r_to_r;  ///< Right input -> Right output
    uint8_t cd_audio_volume_r_to_l;  ///< Right input -> Left output
    
    // -------------------------------------------------------------------------
    // Disc Information
    // -------------------------------------------------------------------------
    
    bool disc_present;               ///< Disc inserted
    uint32_t disc_size_sectors;      ///< Total sectors on disc
    FILE* disc_file;                 ///< BIN file handle (NULL if no disc)
    char disc_path[256];             ///< Path to loaded disc
    
    // -------------------------------------------------------------------------
    // Timing (for event scheduler integration)
    // -------------------------------------------------------------------------
    
    uint64_t last_interrupt_time;    ///< Cycle counter of last interrupt
    uint64_t command_start_time;     ///< When current command started
    
    // -------------------------------------------------------------------------
    // Thread Safety
    // -------------------------------------------------------------------------
    
    Mutex lock;                      ///< Recursive mutex for thread-safe ops
    bool log_enabled;                ///< Enable debug logging
    
    // -------------------------------------------------------------------------
    // Statistics (O(1) increment)
    // -------------------------------------------------------------------------
    
    uint64_t command_count;          ///< Total commands processed
    uint64_t sector_reads;           ///< Total sectors read
    uint64_t interrupt_count;        ///< Total interrupts fired
    uint64_t error_count;            ///< Total errors encountered
    
} CdromState;

// ============================================================================
// FIFO Helper Macros (O(1) complexity)
// ============================================================================

#define cdrom_fifo_is_empty(fifo)  ((fifo)->count == 0)
#define cdrom_fifo_is_full(fifo)   ((fifo)->count >= CDROM_FIFO_SIZE)
#define cdrom_fifo_size(fifo)      ((fifo)->count)
#define cdrom_fifo_space(fifo)     (CDROM_FIFO_SIZE - (fifo)->count)

// ============================================================================
// MSF/LBA Conversion Helpers
// ============================================================================

/// Convert MSF (Minute:Second:Frame) to LBA (Logical Block Address)
/// Formula: LBA = ((minute * 60) + second) * 75 + frame - 150
/// Complexity: O(1)
static inline uint32_t cdrom_msf_to_lba(uint8_t minute, uint8_t second, uint8_t frame) {
    uint32_t lba = ((minute * 60) + second) * 75 + frame;
    return (lba >= 150) ? (lba - 150) : 0;
}

/// Convert LBA to MSF format
/// Complexity: O(1) - simple arithmetic divisions
static inline void cdrom_lba_to_msf(uint32_t lba, uint8_t* minute, uint8_t* second, uint8_t* frame) {
    lba += 150;  // Add pregap offset
    *minute = lba / (60 * 75);
    *second = (lba / 75) % 60;
    *frame = lba % 75;
}

/// Convert BCD to binary (for MSF values)
/// Complexity: O(1)
static inline uint8_t cdrom_bcd_to_bin(uint8_t bcd) {
    return ((bcd >> 4) * 10) + (bcd & 0x0F);
}

/// Convert binary to BCD
/// Complexity: O(1)
static inline uint8_t cdrom_bin_to_bcd(uint8_t bin) {
    return ((bin / 10) << 4) | (bin % 10);
}

#endif // CDROM_TYPES_H
