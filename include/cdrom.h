/*
 * PSX CD-ROM Controller Emulation
 * 
 * Architecture based on PSX-SPX documentation and duckstation reference.
 * Uses asynchronous command execution with proper timing delays.
 * 
 * Key concepts:
 * - Commands are queued, not executed immediately
 * - Interrupts block new commands until acknowledged
 * - DriveState tracks physical drive operations (seeking, reading, etc.)
 */

#ifndef CDROM_H
#define CDROM_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

// Forward declarations
struct Interconnect;

// =============================================================================
// Constants
// =============================================================================

#define CDROM_PARAM_FIFO_SIZE   16
#define CDROM_RESPONSE_FIFO_SIZE 16
#define CDROM_DATA_BUFFER_SIZE  2352
#define CDROM_SECTOR_SIZE       2352

// Timing constants (in CPU cycles at 33.8688 MHz)
#define CDROM_ACK_DELAY         25000    // ~0.74ms - typical command ack delay
#define CDROM_FAST_ACK_DELAY    5000     // ~0.15ms - for simple commands
#define CDROM_ID_READ_DELAY     33868    // ~1ms
#define CDROM_INIT_DELAY        1000000  // ~30ms
#define CDROM_SEEK_DELAY        150000   // Variable, this is minimum
#define CDROM_READ_DELAY        50000    // ~1.5ms per sector (1x speed)
#define CDROM_SPINUP_DELAY      400000   // ~12ms

// IRQ types (I_STAT bit 2 = CDROM)
#define IRQ_CDROM 2

// =============================================================================
// CDROM Interrupt Types (INT1-5)
// =============================================================================

typedef enum {
    CDROM_INT_NONE       = 0,   // No interrupt
    CDROM_INT_DATA_READY = 1,   // INT1: Data ready (sector read complete)
    CDROM_INT_COMPLETE   = 2,   // INT2: Command complete (2nd response)
    CDROM_INT_ACK        = 3,   // INT3: Acknowledge (1st response)
    CDROM_INT_DATA_END   = 4,   // INT4: Data end (end of file/track)
    CDROM_INT_ERROR      = 5    // INT5: Error
} CdromInterrupt;

// =============================================================================
// CDROM Commands
// =============================================================================

typedef enum {
    CDC_SYNC      = 0x00,
    CDC_GETSTAT   = 0x01,
    CDC_SETLOC    = 0x02,
    CDC_PLAY      = 0x03,
    CDC_FORWARD   = 0x04,
    CDC_BACKWARD  = 0x05,
    CDC_READN     = 0x06,
    CDC_MOTORON   = 0x07,
    CDC_STOP      = 0x08,
    CDC_PAUSE     = 0x09,
    CDC_INIT      = 0x0A,
    CDC_MUTE      = 0x0B,
    CDC_DEMUTE    = 0x0C,
    CDC_SETFILTER = 0x0D,
    CDC_SETMODE   = 0x0E,
    CDC_GETPARAM  = 0x0F,
    CDC_GETLOCL   = 0x10,
    CDC_GETLOCP   = 0x11,
    CDC_READT     = 0x12,
    CDC_GETTN     = 0x13,
    CDC_GETTD     = 0x14,
    CDC_SEEKL     = 0x15,
    CDC_SEEKP     = 0x16,
    CDC_SETCLOCK  = 0x17,
    CDC_GETCLOCK  = 0x18,
    CDC_TEST      = 0x19,
    CDC_GETID     = 0x1A,
    CDC_READS     = 0x1B,
    CDC_RESET     = 0x1C,
    CDC_GETQ      = 0x1D,
    CDC_READTOC   = 0x1E,
    CDC_NONE      = 0xFF
} CdromCommand;

// =============================================================================
// Drive State (Physical drive operations)
// =============================================================================

typedef enum {
    DRIVE_IDLE,
    DRIVE_SPINUP,
    DRIVE_SEEKING,
    DRIVE_READING,
    DRIVE_PLAYING,
    DRIVE_PAUSING,
    DRIVE_STOPPING
} DriveState;

// =============================================================================
// Status Register Bits (0x1F801800 read)
// =============================================================================

#define STAT_INDEX_MASK  0x03  // Bits 0-1: Index
#define STAT_ADPBUSY     0x04  // Bit 2: XA-ADPCM busy
#define STAT_PRMEMPT     0x08  // Bit 3: Parameter FIFO empty
#define STAT_PRMWRDY     0x10  // Bit 4: Parameter FIFO not full (can write)
#define STAT_RSLRRDY     0x20  // Bit 5: Response FIFO not empty (can read)
#define STAT_DRQSTS      0x40  // Bit 6: Data FIFO not empty
#define STAT_BUSYSTS     0x80  // Bit 7: Command busy

// =============================================================================
// Secondary Status Byte (stat_byte returned in responses)
// =============================================================================

#define STAT_BYTE_ERROR      0x01  // Bit 0: Error
#define STAT_BYTE_MOTOR_ON   0x02  // Bit 1: Motor on
#define STAT_BYTE_SEEK_ERROR 0x04  // Bit 2: Seek error
#define STAT_BYTE_ID_ERROR   0x08  // Bit 3: ID error
#define STAT_BYTE_SHELL_OPEN 0x10  // Bit 4: Shell open
#define STAT_BYTE_READING    0x20  // Bit 5: Reading data
#define STAT_BYTE_SEEKING    0x40  // Bit 6: Seeking
#define STAT_BYTE_PLAYING    0x80  // Bit 7: Playing CD-DA

// =============================================================================
// Error Codes
// =============================================================================

#define ERROR_INVALID_ARGUMENT   0x10
#define ERROR_WRONG_NUM_PARAMS   0x20
#define ERROR_INVALID_COMMAND    0x40
#define ERROR_NOT_READY          0x80

// =============================================================================
// FIFO Structure
// =============================================================================

typedef struct {
    uint8_t data[CDROM_RESPONSE_FIFO_SIZE];
    uint8_t head;
    uint8_t tail;
    uint8_t count;
} CdromFifo;

// =============================================================================
// Main CDROM State Structure
// =============================================================================

typedef struct Cdrom {
    // Parent reference
    struct Interconnect* inter;
    
    // -------------------------------------------------------------------------
    // Register State
    // -------------------------------------------------------------------------
    uint8_t index;              // Current register bank (0-3)
    uint8_t status;             // Status register (0x1F801800)
    uint8_t stat_byte;          // Secondary status (returned in responses)
    uint8_t interrupt_enable;   // Interrupt enable mask (bits 0-4 for INT1-5)
    uint8_t interrupt_flag;     // Current interrupt code (0-5)
    
    // -------------------------------------------------------------------------
    // Command State
    // -------------------------------------------------------------------------
    CdromCommand pending_command;    // Command waiting to execute
    CdromCommand current_command;    // Currently executing command  
    CdromCommand second_response_cmd;// Command awaiting 2nd response
    uint8_t pending_params[16];      // Saved parameters for pending command
    uint8_t pending_param_count;     // Number of saved parameters
    
    // -------------------------------------------------------------------------
    // Drive State
    // -------------------------------------------------------------------------
    DriveState drive_state;
    bool motor_on;
    bool disc_present;
    bool shell_open;
    
    // -------------------------------------------------------------------------
    // Position and Seek State
    // -------------------------------------------------------------------------
    uint32_t current_lba;       // Current head position
    uint32_t target_lba;        // Target for seek/read
    uint32_t setloc_lba;        // Position set by Setloc command
    bool setloc_pending;        // Setloc was issued, pending seek
    
    // -------------------------------------------------------------------------
    // Mode Settings
    // -------------------------------------------------------------------------
    uint8_t mode;               // Mode register
    bool double_speed;          // 2x speed mode
    bool xa_adpcm_enable;       // XA-ADPCM enable
    bool whole_sector;          // Read whole sector (2340 bytes) vs data only
    bool xa_filter_enable;      // XA filter enable
    uint8_t xa_filter_file;     // XA filter file number
    uint8_t xa_filter_channel;  // XA filter channel number
    bool report_enable;         // Report interrupts during play
    bool auto_pause;            // Auto-pause at track end
    bool cdda_enable;           // CD-DA mode
    bool muted;                 // Audio muted
    
    // -------------------------------------------------------------------------
    // FIFOs
    // -------------------------------------------------------------------------
    CdromFifo param_fifo;       // Parameter FIFO (write)
    CdromFifo response_fifo;    // Response FIFO (read)
    
    // -------------------------------------------------------------------------
    // Data Buffer
    // -------------------------------------------------------------------------
    uint8_t data_buffer[CDROM_DATA_BUFFER_SIZE];
    uint32_t data_buffer_size;
    uint32_t data_buffer_index;
    bool data_buffer_valid;
    
    // -------------------------------------------------------------------------
    // Disc Image
    // -------------------------------------------------------------------------
    FILE* disc_file;
    uint32_t disc_size_sectors;
    
    // -------------------------------------------------------------------------
    // TOC (Table of Contents)
    // -------------------------------------------------------------------------
    uint8_t first_track;
    uint8_t last_track;
    uint32_t track_lba[100];    // LBA for each track (1-99)
    
    // -------------------------------------------------------------------------
    // Async Interrupt Queue (for INT1 during reading)
    // -------------------------------------------------------------------------
    uint8_t async_interrupt;    // Pending async interrupt
    uint8_t async_response[8];  // Pending async response data
    uint8_t async_response_size;
    
} Cdrom;

// =============================================================================
// Public Functions
// =============================================================================

// Initialization
void cdrom_init(Cdrom* cdrom, struct Interconnect* inter);
void cdrom_reset(Cdrom* cdrom);

// Disc Management
bool cdrom_load_disc(Cdrom* cdrom, const char* cue_path);
void cdrom_eject_disc(Cdrom* cdrom);

// Register Access
uint8_t cdrom_read8(Cdrom* cdrom, uint32_t addr);
void cdrom_write8(Cdrom* cdrom, uint32_t addr, uint8_t value);

// Event-driven execution (called by event scheduler)
void cdrom_execute_command(Cdrom* cdrom);
void cdrom_execute_drive(Cdrom* cdrom);
void cdrom_execute_second_response(Cdrom* cdrom);
void cdrom_deliver_async_interrupt(Cdrom* cdrom);

// Status queries
bool cdrom_has_pending_command(Cdrom* cdrom);
bool cdrom_has_pending_interrupt(Cdrom* cdrom);

// =============================================================================
// FIFO Inline Functions
// =============================================================================

static inline void fifo_init(CdromFifo* fifo) {
    fifo->head = 0;
    fifo->tail = 0;
    fifo->count = 0;
}

static inline void fifo_clear(CdromFifo* fifo) {
    fifo->head = 0;
    fifo->tail = 0;
    fifo->count = 0;
}

static inline bool fifo_is_empty(CdromFifo* fifo) {
    return fifo->count == 0;
}

static inline bool fifo_is_full(CdromFifo* fifo) {
    return fifo->count >= CDROM_RESPONSE_FIFO_SIZE;
}

static inline void fifo_push(CdromFifo* fifo, uint8_t value) {
    if (fifo->count < CDROM_RESPONSE_FIFO_SIZE) {
        fifo->data[fifo->tail] = value;
        fifo->tail = (fifo->tail + 1) % CDROM_RESPONSE_FIFO_SIZE;
        fifo->count++;
    }
}

static inline uint8_t fifo_pop(CdromFifo* fifo) {
    if (fifo->count > 0) {
        uint8_t value = fifo->data[fifo->head];
        fifo->head = (fifo->head + 1) % CDROM_RESPONSE_FIFO_SIZE;
        fifo->count--;
        return value;
    }
    return 0;
}

static inline uint8_t fifo_peek(CdromFifo* fifo, uint8_t index) {
    if (index < fifo->count) {
        return fifo->data[(fifo->head + index) % CDROM_RESPONSE_FIFO_SIZE];
    }
    return 0;
}

#endif // CDROM_H
