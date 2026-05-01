#ifndef CDROM_H
#define CDROM_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#include "cdrom_disc.h"
#include "cdrom_audio.h"

struct Interconnect;

/* =========================================================================
 * Constants
 * ========================================================================= */

#define CDROM_PARAM_FIFO_SIZE    16
#define CDROM_RESPONSE_FIFO_SIZE 16
#define CDROM_SECTOR_BUFFERS      8

/* Timing (CPU cycles @ 33.8688 MHz) */
#define CDROM_ACK_DELAY          25000
#define CDROM_FAST_ACK_DELAY      5000
#define CDROM_ID_READ_DELAY      33868
#define CDROM_INIT_DELAY       4000000
#define CDROM_RESET_DELAY      4000000
#define CDROM_SEEK_MIN_DELAY     30000
#define CDROM_READ_DELAY_1X      50000
#define CDROM_SPINUP_DELAY      400000
#define CDROM_MIN_INT_DELAY       1000

#define IRQ_CDROM 2

/* =========================================================================
 * Interrupt Types (INT1-5)
 * ========================================================================= */

typedef enum {
    CDROM_INT_NONE       = 0,
    CDROM_INT_DATA_READY = 1,   /* INT1 */
    CDROM_INT_COMPLETE   = 2,   /* INT2 */
    CDROM_INT_ACK        = 3,   /* INT3 */
    CDROM_INT_DATA_END   = 4,   /* INT4 */
    CDROM_INT_ERROR      = 5    /* INT5 */
} CdromInterrupt;

/* =========================================================================
 * Command Codes
 * ========================================================================= */

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
    CDC_VIDEOCD   = 0x1F,
    CDC_NONE      = 0xFF
} CdromCommand;

/* =========================================================================
 * Drive State
 * ========================================================================= */

typedef enum {
    DRIVE_IDLE,
    DRIVE_SPINUP,
    DRIVE_SEEKING,
    DRIVE_READING,
    DRIVE_PLAYING,
    DRIVE_PAUSING,
    DRIVE_STOPPING
} DriveState;

/* =========================================================================
 * Status Register Bits
 * ========================================================================= */

#define STAT_INDEX_MASK  0x03
#define STAT_ADPBUSY     0x04
#define STAT_PRMEMPT     0x08
#define STAT_PRMWRDY     0x10
#define STAT_RSLRRDY     0x20
#define STAT_DRQSTS      0x40
#define STAT_BUSYSTS     0x80

/* Secondary status byte flags */
#define STAT_BYTE_ERROR      0x01
#define STAT_BYTE_MOTOR_ON   0x02
#define STAT_BYTE_SEEK_ERROR 0x04
#define STAT_BYTE_ID_ERROR   0x08
#define STAT_BYTE_SHELL_OPEN 0x10
#define STAT_BYTE_READING    0x20
#define STAT_BYTE_SEEKING    0x40
#define STAT_BYTE_PLAYING    0x80

/* Error codes */
#define ERROR_INVALID_ARGUMENT  0x10
#define ERROR_WRONG_NUM_PARAMS  0x20
#define ERROR_INVALID_COMMAND   0x40
#define ERROR_NOT_READY         0x80

/* =========================================================================
 * FIFO
 * ========================================================================= */

typedef struct {
    uint8_t data[CDROM_RESPONSE_FIFO_SIZE];
    uint8_t head, tail, count;
} CdromFifo;

/* =========================================================================
 * Sector Buffer Ring
 * ========================================================================= */

typedef struct {
    uint8_t  raw[2352];     /* full raw sector */
    uint32_t data_start;    /* offset for data FIFO */
    uint32_t data_size;     /* exposed byte count */
    uint32_t position;      /* current read position (0..data_size) */
    uint32_t lba;
    bool     valid;
} SectorBuffer;

/* =========================================================================
 * Main CDROM Struct
 * ========================================================================= */

typedef struct Cdrom {
    struct Interconnect *inter;

    /* --- Register state --- */
    uint8_t index;
    uint8_t interrupt_enable;
    uint8_t interrupt_flag;

    /* --- Command state --- */
    CdromCommand pending_command;
    CdromCommand current_command;
    CdromCommand second_response_cmd;
    uint8_t      pending_params[16];
    uint8_t      pending_param_count;

    /* Second response data buffer (for async responses) */
    uint8_t second_response_data[16];
    uint8_t second_response_size;

    /* --- Drive state --- */
    DriveState drive_state;
    bool       motor_on;
    bool       disc_present;
    bool       shell_open;
    bool       read_after_seek;
    bool       play_after_seek;

    /* --- Position --- */
    uint32_t current_lba;
    uint32_t target_lba;
    uint32_t setloc_lba;
    bool     setloc_pending;
    uint32_t current_subq_lba;
    SubQ     last_subq;

    /* CDDA fast-forward/rewind speed (1=normal, 2/4=ff/rew) */
    uint8_t cdda_speed;

    /* --- Mode --- */
    uint8_t mode;
    bool double_speed;
    bool xa_adpcm_enable;
    bool whole_sector;
    bool xa_filter_enable;
    uint8_t xa_filter_file;
    uint8_t xa_filter_channel;
    bool report_enable;
    bool auto_pause;
    bool cdda_enable;
    bool muted;

    /* --- FIFOs --- */
    CdromFifo param_fifo;
    CdromFifo response_fifo;

    /* --- Sector buffer ring --- */
    SectorBuffer sector_buffers[CDROM_SECTOR_BUFFERS];
    uint8_t current_read_buffer;
    uint8_t current_write_buffer;
    bool    data_buffer_armed;     /* request register bit 7 */

    /* --- Disc & async reader --- */
    CdromDisc        disc;
    CdromAsyncReader async_reader;

    /* --- Audio --- */
    AudioFifo    audio_fifo;
    XaAdpcmState xa_adpcm_state;

    /* Volume matrix (L←CDL, L←CDR, R←CDL, R←CDR) — default 0x80 each */
    uint8_t vol_ll, vol_lr, vol_rl, vol_rr;

} Cdrom;

/* =========================================================================
 * Public API
 * ========================================================================= */

void    cdrom_init(Cdrom *cdrom, struct Interconnect *inter);
void    cdrom_reset(Cdrom *cdrom);
bool    cdrom_load_disc(Cdrom *cdrom, const char *cue_path);
void    cdrom_eject_disc(Cdrom *cdrom);

uint8_t cdrom_read8(Cdrom *cdrom, uint32_t addr);
void    cdrom_write8(Cdrom *cdrom, uint32_t addr, uint8_t value);

/* Event-driven execution (called by interconnect event scheduler) */
void cdrom_execute_command(Cdrom *cdrom);
void cdrom_execute_drive(Cdrom *cdrom);
void cdrom_execute_second_response(Cdrom *cdrom);

bool cdrom_has_pending_command(Cdrom *cdrom);
bool cdrom_has_pending_interrupt(Cdrom *cdrom);

/* Audio frame for SPU/SDL (one stereo pair) */
void cdrom_get_audio_frame(Cdrom *cdrom, int16_t *left, int16_t *right);

/* =========================================================================
 * FIFO Inline Functions
 * ========================================================================= */

static inline void fifo_init(CdromFifo *f) { f->head=f->tail=f->count=0; }
static inline void fifo_clear(CdromFifo *f) { f->head=f->tail=f->count=0; }
static inline bool fifo_is_empty(const CdromFifo *f) { return f->count==0; }
static inline bool fifo_is_full(const CdromFifo *f) { return f->count>=CDROM_RESPONSE_FIFO_SIZE; }

static inline void fifo_push(CdromFifo *f, uint8_t v) {
    if (f->count < CDROM_RESPONSE_FIFO_SIZE) {
        f->data[f->tail] = v;
        f->tail = (f->tail+1) % CDROM_RESPONSE_FIFO_SIZE;
        f->count++;
    }
}

static inline uint8_t fifo_pop(CdromFifo *f) {
    if (f->count > 0) {
        uint8_t v = f->data[f->head];
        f->head = (f->head+1) % CDROM_RESPONSE_FIFO_SIZE;
        f->count--;
        return v;
    }
    return 0;
}

static inline uint8_t fifo_peek(const CdromFifo *f, uint8_t i) {
    return (i < f->count) ? f->data[(f->head+i) % CDROM_RESPONSE_FIFO_SIZE] : 0;
}

#endif /* CDROM_H */
