/*
 * Portions of this file are inspired by PCSX ReARMed (https://github.com/notaz/pcsx_rearmed)
 * Copyright (c) PCSX ReARMed authors. Used under the GNU GPL v2.
 * Logic and structure are adapted for clarity and uniqueness.
 */
// --- cdrom.c ---
#include "cdrom.h"
#include "interconnect.h" // For interrupt definitions/requests IRQ_CDROM
#include "event_scheduler.h" // For eventq_schedule
#include <stdio.h>
#include <string.h> // For memset
#include <stdlib.h> // For exit if needed
#include "log.h"

// --- Status Register Bits (nocash spec) ---
#define STAT_ADPBUSY    (1 << 2) // XA-ADPCM FIFO occupies the bus while playing
#define STAT_PRMEMPT    (1 << 3) // Parameter FIFO empty
#define STAT_PRMWRDY    (1 << 4) // Parameter FIFO not full
#define STAT_RSLRDY     (1 << 5) // Response FIFO not empty
#define STAT_DTEN       (1 << 6) // Data FIFO not empty (DRQ asserted)
#define STAT_BUSY       (1 << 7) // Command transfer busy

// Forward declaration for late enable IRQ2 logic (PCSX ReARMed style)
// REMOVED: static void cdrom_maybe_request_irq2(Cdrom* cdrom, uint8_t irq_bit);

// Logging: Only use LOG_ERROR/LOG_FATAL for unrecoverable errors, LOG_INFO for disc load success.

// 1. Log every CDROM command received
static void log_cdrom_command(uint8_t cmd) {
    LOG_CDROM_IMPORTANT("[CDROM] Command received: 0x%02x", cmd);
}
// 2. Log every read from the CDROM response register
static void log_cdrom_response_read(Cdrom* cdrom) {
    LOG_CDROM_IMPORTANT("[CDROM] Response register read. FIFO count: %d, Status: 0x%02x", cdrom->response_fifo.count, cdrom->status);
}
// 3. Log when the response FIFO is pushed or popped
static void log_fifo_push(uint8_t value) {
    LOG_CDROM_IMPORTANT("[CDROM] FIFO push: 0x%02x", value);
}
static void log_fifo_pop(uint8_t value) {
    LOG_CDROM_IMPORTANT("[CDROM] FIFO pop: 0x%02x", value);
}
// 4. Log when CDROM status changes (busy/response ready)
static void log_cdrom_status_change(uint8_t old_status, uint8_t new_status) {
    if ((old_status & STAT_BUSY) != (new_status & STAT_BUSY)) {
        LOG_CDROM_IMPORTANT("[CDROM] Status BUSY changed: %d -> %d", (old_status & STAT_BUSY) != 0, (new_status & STAT_BUSY) != 0);
    }
    if ((old_status & STAT_RSLRDY) != (new_status & STAT_RSLRDY)) {
        LOG_CDROM_IMPORTANT("[CDROM] Status RSLRDY changed: %d -> %d", (old_status & STAT_RSLRDY) != 0, (new_status & STAT_RSLRDY) != 0);
    }
}

// --- Sector Structure Constants ---
#define CD_RAW_SECTOR_SIZE 2352
#define CD_USER_DATA_SIZE 2048
#define CD_MODE2_FORM1_HEADER_SIZE 24
#define CD_MODE_RAWISH_SIZE 2340
#define CD_MODE_RAWISH_OFFSET 12

// --- CDROM Commands (from your header) ---
#define CDC_NOP         0x00
#define CDC_GETSTAT     0x01
#define CDC_SETLOC      0x02
#define CDC_PLAY        0x03
#define CDC_FORWARD     0x04
#define CDC_READN       0x06
#define CDC_STOP        0x08
#define CDC_STANDBY     0x07
#define CDC_PAUSE       0x09
#define CDC_INIT        0x0A
#define CDC_MUTE        0x0B
#define CDC_DEMUTE      0x0C
#define CDC_SETMODE     0x0E
#define CDC_GETPARAM    0x0F
#define CDC_GETLOCL     0x10
#define CDC_GETLOCP     0x11
#define CDC_GETTN       0x13
#define CDC_GETTD       0x14
#define CDC_READTOC     0x1E
#define CDC_RESET       0x1C
#define CDC_READS       0x1B
#define CDC_SETFILTER   0x0D
#define CDC_SEEKL       0x15
#define CDC_SEEKP       0x16
#define CDC_TEST        0x19
#define CDC_GETID       0x1A

// --- Forward Declarations for Command Handlers ---
static void cdrom_handle_command(Cdrom* cdrom, uint8_t command);
static void cmd_nop(Cdrom* cdrom);
static void cmd_get_stat(Cdrom* cdrom);
static void cmd_test(Cdrom* cdrom);
static void cmd_reset(Cdrom* cdrom);
static void cmd_get_id(Cdrom* cdrom);
static void cmd_read_s(Cdrom* cdrom);
static void cmd_set_loc(Cdrom* cdrom);
static void cmd_play(Cdrom* cdrom);
static void cmd_forward(Cdrom* cdrom);
static void cmd_read_n(Cdrom* cdrom);
static void cmd_standby(Cdrom* cdrom);
static void cmd_pause(Cdrom* cdrom);
static void cmd_seek_l(Cdrom* cdrom);
static void cmd_seek_p(Cdrom* cdrom);
static void cmd_set_mode(Cdrom* cdrom);
static void cmd_get_param(Cdrom* cdrom);
static void cmd_get_loc_l(Cdrom* cdrom);
static void cmd_get_loc_p(Cdrom* cdrom);
static void cmd_get_tn(Cdrom* cdrom);
static void cmd_get_td(Cdrom* cdrom);
static void cmd_read_toc(Cdrom* cdrom);
static void cmd_set_filter(Cdrom* cdrom);
static void cmd_mute(Cdrom* cdrom);
static void cmd_demute(Cdrom* cdrom);
static void cmd_stop(Cdrom* cdrom);

// --- NEW: Forward declarations for completion handlers ---
static void cmd_init_complete(Cdrom* cdrom);
static void cmd_get_id_complete(Cdrom* cdrom);
static void cmd_pause_complete(Cdrom* cdrom);
static void cmd_read_n_complete(Cdrom* cdrom);
static void cmd_read_s_complete(Cdrom* cdrom);
static void cmd_standby_complete(Cdrom* cdrom);
static void cmd_reset_complete(Cdrom* cdrom);
static void cmd_read_toc_complete(Cdrom* cdrom);
static void cmd_set_loc_complete(Cdrom* cdrom);
static void cmd_play_complete(Cdrom* cdrom);

// --- Internal Helper Function Declarations ---
static void fifo_init(Fifo8* fifo);
static bool fifo_push(Fifo8* fifo, uint8_t value);
static uint8_t fifo_pop(Fifo8* fifo);
static void fifo_clear(Fifo8* fifo);
static bool fifo_is_empty(Fifo8* fifo);
static bool fifo_is_full(Fifo8* fifo);
static uint8_t bcd_to_int(uint8_t bcd);
static void update_status_register(Cdrom* cdrom);
static void update_stat_byte(Cdrom* cdrom);
static void trigger_interrupt(Cdrom* cdrom, uint8_t int_code);
static void cdrom_schedule_event(Cdrom* cdrom, uint32_t cycles, void (*handler)(Cdrom*));
static void cdrom_set_irq_flag(Cdrom* cdrom, uint8_t flag);
// REMOVED: static void cdrom_maybe_request_irq2(Cdrom* cdrom, uint8_t irq_bit);

// --- Rate-limited log counters for CDROM register accesses ---
#if LOG_LEVEL >= LOG_LEVEL_INFO
static int cdrom_read8_count = 0;
static int cdrom_write8_count = 0;
#endif

// --- FIFO Helpers (Your implementation is great, no changes needed) ---
static void fifo_init(Fifo8* fifo) { memset(fifo, 0, sizeof(Fifo8)); }
static bool fifo_push(Fifo8* fifo, uint8_t value) {
    if (fifo->count >= FIFO_SIZE) return false;
    uint8_t write_ptr = (fifo->read_ptr + fifo->count) % FIFO_SIZE;
    fifo->data[write_ptr] = value;
    fifo->count++;
    log_fifo_push(value);
    return true;
}
static uint8_t fifo_pop(Fifo8* fifo) {
    if (fifo->count == 0) return 0;
    uint8_t value = fifo->data[fifo->read_ptr];
    fifo->read_ptr = (fifo->read_ptr + 1) % FIFO_SIZE;
    fifo->count--;
    log_fifo_pop(value);
    return value;
}
static void fifo_clear(Fifo8* fifo) {
    fifo->count = 0;
    fifo->read_ptr = 0;
}
static bool fifo_is_empty(Fifo8* fifo) {
    return fifo->count == 0;
}
static bool fifo_is_full(Fifo8* fifo) {
    return fifo->count >= FIFO_SIZE;
}

// Update stat byte (command response byte) based on drive state
static void update_stat_byte(Cdrom* cdrom) {
    cdrom->stat_byte = 0;
    
    // Set state bits based on current operation
    if (cdrom->current_state == CD_STATE_READING)
        cdrom->stat_byte |= STAT_BYTE_READING;
    else if (cdrom->current_state == CD_STATE_CMD_EXEC)
        cdrom->stat_byte |= STAT_BYTE_SEEKING;  // Commands like SeekL set this
    
    // Set motor bit if motor is spinning
    if (cdrom->motor_on)
        cdrom->stat_byte |= STAT_BYTE_MOTOR;
    
    // Set shell open bit if shell was opened (sticky until Getstat)
    if (cdrom->shell_was_open)
        cdrom->stat_byte |= STAT_BYTE_SHELLOPEN;
    
    // Set error bits if errors occurred
    if (cdrom->last_id_error)
        cdrom->stat_byte |= STAT_BYTE_IDERROR;
    if (cdrom->last_seek_error)
        cdrom->stat_byte |= STAT_BYTE_SEEKERROR;
}

// --- BCD Conversion Helpers ---
static uint8_t bcd_to_int(uint8_t bcd) {
    return ((bcd >> 4) * 10) + (bcd & 0x0F);
}

static uint8_t int_to_bcd(uint8_t value) {
    return ((value / 10) << 4) | (value % 10);
}

// --- Missing Helper Functions ---
static void update_status_register(Cdrom* cdrom) {
    // Update the status register based on current FIFO states and busy flag
    cdrom->status = 0;
    
    // Set busy flag if busy
    if (cdrom->status & STAT_BUSY) {
        cdrom->status |= STAT_BUSY;
    }
    
    // Set parameter FIFO empty/full flags
    if (fifo_is_empty(&cdrom->param_fifo)) {
        cdrom->status |= STAT_PRMEMPT;
    }
    if (fifo_is_full(&cdrom->param_fifo)) {
        cdrom->status |= STAT_PRMWRDY;
    }
    
    // Set response FIFO ready flag
    if (!fifo_is_empty(&cdrom->response_fifo)) {
        cdrom->status |= STAT_RSLRDY;
    }
    
    // Set data FIFO flags
    if (cdrom->data_buffer_count > 0) {
        cdrom->status |= STAT_DTEN;  // Data not empty
    }
}

static void trigger_interrupt(Cdrom* cdrom, uint8_t int_code) {
    // Set the interrupt flags and request IRQ
    cdrom->interrupt_flags |= (1 << (int_code - 1));  // INT codes are 1-based, flags are 0-based
    interconnect_request_irq(cdrom->inter, IRQ_CDROM, "CDROM");
}

static void cdrom_schedule_event(Cdrom* cdrom, uint32_t cycles, void (*handler)(Cdrom*)) {
    // Store the handler and schedule a CDROM event
    cdrom->pending_completion_handler = handler;
    eventq_schedule(cdrom->inter, EVQ_CDROM, cycles);
}

static void cdrom_set_irq_flag(Cdrom* cdrom, uint8_t flag) {
    cdrom->interrupt_flags |= flag;
    // Check if we should request IRQ based on enable flags
    for (uint8_t bit = 1; bit <= 0x10; bit <<= 1) {
        if ((cdrom->interrupt_flags & bit) && (cdrom->interrupt_enable & bit)) {
            interconnect_request_irq(cdrom->inter, IRQ_CDROM, "CDROM");
            break;
        }
    }
}

// --- Command Handlers (This is where the main logic is filled in) ---

static void cmd_nop(Cdrom* cdrom) {
    LOG_CDROM_INFO("~ CDROM CMD: Nop (0x00) - Invalid Command");
    
    // Nop command returns error - PSX-SPEX: INT5(11h,40h)
    cdrom->status &= ~STAT_BUSY;
    update_stat_byte(cdrom);
    update_status_register(cdrom);
    fifo_clear(&cdrom->response_fifo);
    fifo_push(&cdrom->response_fifo, cdrom->stat_byte | STAT_BYTE_ERROR);  // stat with error bit
    fifo_push(&cdrom->response_fifo, 0x40); // Error code: Invalid Command
    trigger_interrupt(cdrom, 5); // INT5: Error
}

static void cmd_get_stat(Cdrom* cdrom) {
    LOG_CDROM_INFO("~ CDROM CMD: GetStat (0x01)");
    
    // GetStat is instant - clear busy immediately
    cdrom->status &= ~STAT_BUSY;
    fifo_clear(&cdrom->response_fifo);
    
    // Special feature: Clear shell open flag (per PSX-SPEX)
    cdrom->shell_was_open = false;
    
    update_stat_byte(cdrom);
    update_status_register(cdrom);
    fifo_push(&cdrom->response_fifo, cdrom->stat_byte);  // Return stat byte, NOT status register!
    trigger_interrupt(cdrom, 3); // INT3: Response Ready
}

// <<< MODIFIED: Implemented two-stage Init >>>
static void cmd_init(Cdrom* cdrom) {
    LOG_CDROM_INFO("~ CDROM CMD: Init (0x0A) - Step 1\n");
    cdrom->current_state = CD_STATE_CMD_EXEC;
    cdrom->status |= STAT_BUSY;
    cdrom->motor_on = true;

    // First response is immediate (acknowledges command)
    update_stat_byte(cdrom);
    update_status_register(cdrom);
    fifo_clear(&cdrom->response_fifo);
    fifo_push(&cdrom->response_fifo, cdrom->stat_byte);  // Return stat byte
    trigger_interrupt(cdrom, 3);

    // Schedule the second part (actual init and completion response)
    cdrom_schedule_event(cdrom, 300000, cmd_init_complete);
}

static void cmd_init_complete(Cdrom* cdrom) {
    LOG_CDROM_INFO("  CDROM Init - Step 2 (Completion)\n");
    cdrom->status &= ~STAT_BUSY;

    // Reset internal state and set mode to 20h (sector size 2340)
    cdrom->interrupt_enable = 0;
    cdrom->interrupt_flags = 0;
    fifo_clear(&cdrom->param_fifo);
    cdrom->double_speed = false;
    cdrom->xa_adpcm_enable = false;
    cdrom->sector_size_is_2340 = true;  // Bit 5 of mode 20h
    cdrom->xa_filter_enable = false;
    cdrom->report_enable = false;
    cdrom->auto_pause_enable = false;
    cdrom->cdda_enable = false;
    cdrom->xa_filter_file = 0;
    cdrom->xa_filter_channel = 0;
    cdrom->current_state = CD_STATE_IDLE;
    
    // Second response (signals command is finished)
    update_stat_byte(cdrom);
    update_status_register(cdrom);
    fifo_push(&cdrom->response_fifo, cdrom->stat_byte);  // Return stat byte
    trigger_interrupt(cdrom, 2); // INT2: Command Complete
}

// <<< MODIFIED: Implemented two-stage GetID >>>
static void cmd_get_id(Cdrom* cdrom) {
    LOG_CDROM_INFO("~ CDROM CMD: GetID (0x1A) - Step 1\n");
    cdrom->current_state = CD_STATE_CMD_EXEC;
    cdrom->status |= STAT_BUSY;

    // First response is immediate (acknowledges command)
    update_stat_byte(cdrom);
    update_status_register(cdrom);
    fifo_clear(&cdrom->response_fifo);
    fifo_push(&cdrom->response_fifo, cdrom->stat_byte);  // Return stat byte
    trigger_interrupt(cdrom, 3);
    
    // Schedule the result
    cdrom_schedule_event(cdrom, 100000, cmd_get_id_complete);
}

static void cmd_get_id_complete(Cdrom* cdrom) {
    LOG_CDROM_INFO("  CDROM GetID - Step 2 (Completion)");
    cdrom->status &= ~STAT_BUSY;

    if (!cdrom->disc_present) {
        LOG_CDROM_IMPORTANT("[BOOT] BIOS is checking for disc: NO DISC PRESENT");
        // No Disc Error response - PSX-SPEX: INT5(08h,40h, 00h,00h, 00h,00h,00h,00h)
        cdrom->last_id_error = true;  // Set error flag
        update_stat_byte(cdrom);
        fifo_push(&cdrom->response_fifo, cdrom->stat_byte | STAT_BYTE_ERROR);  // stat with error bit
        fifo_push(&cdrom->response_fifo, 0x40); // flags: bit6=Missing (Disk Missing)
        for(int i = 0; i < 6; ++i) fifo_push(&cdrom->response_fifo, 0x00);
        LOG_CDROM_IMPORTANT("[BOOT] CDROM INT5 (error) triggered: No disc - should show BIOS menu");
        trigger_interrupt(cdrom, 5); // INT5: Error
    } else {
        LOG_CDROM_IMPORTANT("[BOOT] BIOS is checking for disc: DISC PRESENT");
        // Standard Licensed Disc response (SCEA)
        cdrom->last_id_error = false;  // Clear error flag
        update_stat_byte(cdrom);
        update_status_register(cdrom);
        fifo_push(&cdrom->response_fifo, cdrom->stat_byte);  // Return stat byte
        fifo_push(&cdrom->response_fifo, 0x02); // Status: Licensed
        fifo_push(&cdrom->response_fifo, 0x00); // Disc Type: CD-ROM
        fifo_push(&cdrom->response_fifo, 0x00);
        fifo_push(&cdrom->response_fifo, 'S');
        fifo_push(&cdrom->response_fifo, 'C');
        fifo_push(&cdrom->response_fifo, 'E');
        fifo_push(&cdrom->response_fifo, 'A');
        trigger_interrupt(cdrom, 2); // INT2: Command Complete
    }
    cdrom->current_state = CD_STATE_IDLE;
}

// <<< MODIFIED: Implemented Standby command >>>
static void cmd_standby(Cdrom* cdrom) {
    LOG_CDROM_INFO("~ CDROM CMD: Standby (0x07)");
    
    if (cdrom->motor_on) {
        // Motor already on - return error
        LOG_CDROM_WARN("  Standby: Motor already on - returning error");
        cdrom->status &= ~STAT_BUSY;
        update_stat_byte(cdrom);
        update_status_register(cdrom);
        fifo_push(&cdrom->response_fifo, cdrom->stat_byte | STAT_BYTE_ERROR);  // stat with error bit
        fifo_push(&cdrom->response_fifo, 0x20); // Error code: Motor already on
        trigger_interrupt(cdrom, 5); // INT5: Error
        return;
    }
    
    // Motor was off - activate it
    cdrom->motor_on = true;
    cdrom->current_state = CD_STATE_CMD_EXEC;
    cdrom->status |= STAT_BUSY;
    
    // First response is immediate (acknowledges command)
    update_stat_byte(cdrom);
    update_status_register(cdrom);
    fifo_clear(&cdrom->response_fifo);
    fifo_push(&cdrom->response_fifo, cdrom->stat_byte);  // Return stat byte
    trigger_interrupt(cdrom, 3);
    
    // Schedule the completion
    cdrom_schedule_event(cdrom, 300000, cmd_standby_complete);
}

static void cmd_standby_complete(Cdrom* cdrom) {
    LOG_CDROM_INFO("  CDROM Standby - Complete");
    cdrom->status &= ~STAT_BUSY;
    cdrom->current_state = CD_STATE_IDLE;
    
    // Second response (signals command is finished)
    update_stat_byte(cdrom);
    update_status_register(cdrom);
    fifo_push(&cdrom->response_fifo, cdrom->stat_byte);  // Return stat byte
    trigger_interrupt(cdrom, 2); // INT2: Command Complete
}

// Stubs for other commands - no changes needed yet
static void cmd_pause(Cdrom* cdrom) {
    LOG_CDROM_INFO("~ CDROM CMD: Pause (0x09)\n");
    cdrom->current_state = CD_STATE_CMD_EXEC;
    cdrom->status |= STAT_BUSY;
    update_stat_byte(cdrom);
    update_status_register(cdrom);
    fifo_push(&cdrom->response_fifo, cdrom->stat_byte);  // Return stat byte
    trigger_interrupt(cdrom, 3);
    cdrom_schedule_event(cdrom, 150000, cmd_pause_complete);
}

static void cmd_pause_complete(Cdrom* cdrom) {
    LOG_CDROM_INFO("  CDROM Pause - Complete\n");
    cdrom->status &= ~STAT_BUSY;
    cdrom->current_state = CD_STATE_IDLE;
    update_stat_byte(cdrom);
    update_status_register(cdrom);
    fifo_push(&cdrom->response_fifo, cdrom->stat_byte);  // Return stat byte
    trigger_interrupt(cdrom, 2);
}

// --- Main command dispatcher: Only block if busy, not if interrupt pending ---
static void cdrom_handle_command(Cdrom* cdrom, uint8_t command) {
    LOG_CDROM_DEBUG("[CDROM] Command received: 0x%02x", command);
    if (!fifo_is_empty(&cdrom->param_fifo)) {
        char param_str[64] = {0};
        for (int i = 0; i < cdrom->param_fifo.count; ++i) {
            int len = strlen(param_str);
            snprintf(param_str + len, sizeof(param_str) - len, "0x%02x ", cdrom->param_fifo.data[i]);
        }
        LOG_CDROM_DEBUG("[CDROM] Command parameters: %s", param_str);
    }
    static int first_cdrom_cmd_logged = 0;
    if (!first_cdrom_cmd_logged) {
        LOG_CDROM_INFO("[BOOT] First CDROM command received: 0x%02x", command);
        first_cdrom_cmd_logged = 1;
    }
    if (cdrom->status & STAT_BUSY) {
        LOG_CDROM_WARN("CDROM: Command 0x%02x IGNORED (busy)", command);
        return;
    }
    cdrom->pending_command = command;
    cdrom->status |= STAT_BUSY;
    update_status_register(cdrom);
    switch (command) {
        case CDC_NOP:     cmd_nop(cdrom); break;
        case CDC_GETSTAT: cmd_get_stat(cdrom); break;
        case CDC_SETLOC:  cmd_set_loc(cdrom); break;
        case CDC_PLAY:    cmd_play(cdrom); break;
        case CDC_FORWARD: cmd_forward(cdrom); break;
        case CDC_READN:   cmd_read_n(cdrom); break;
        case CDC_STANDBY: cmd_standby(cdrom); break;
        case CDC_PAUSE:   cmd_pause(cdrom); break;
        case CDC_INIT:    cmd_init(cdrom); break;
        case CDC_MUTE:    cmd_mute(cdrom); break;
        case CDC_DEMUTE:  cmd_demute(cdrom); break;
        case CDC_SETMODE: cmd_set_mode(cdrom); break;
        case CDC_GETPARAM: cmd_get_param(cdrom); break;
        case CDC_GETLOCL:  cmd_get_loc_l(cdrom); break;
        case CDC_GETLOCP:  cmd_get_loc_p(cdrom); break;
        case CDC_GETTN:    cmd_get_tn(cdrom); break;
        case CDC_GETTD:    cmd_get_td(cdrom); break;
        case CDC_READTOC:  cmd_read_toc(cdrom); break;
        case CDC_SETFILTER: cmd_set_filter(cdrom); break;
        case CDC_STOP:    cmd_stop(cdrom); break;
        case CDC_SEEKL:   cmd_seek_l(cdrom); break;
        case CDC_SEEKP:   cmd_seek_p(cdrom); break;
        case CDC_TEST:    cmd_test(cdrom); break;
        case CDC_RESET:   cmd_reset(cdrom); break;
        case CDC_GETID:   cmd_get_id(cdrom); break;
        case CDC_READS:   cmd_read_s(cdrom); break;
        default:
            LOG_CDROM_ERROR("CDROM Error: Unhandled command 0x%02x", command);
            cdrom->status &= ~STAT_BUSY;
            update_status_register(cdrom);
            break;
    }
}

// --- Core Public Functions ---

// cdrom_init: No changes needed
void cdrom_init(Cdrom* cdrom, struct Interconnect* inter) {
    LOG_CDROM_INFO("CDROM initialized");
    LOG_CDROM_INFO("Initializing CD-ROM...\n");
    memset(cdrom, 0, sizeof(Cdrom));
    cdrom->inter = inter;
    cdrom->status = STAT_PRMEMPT | STAT_PRMWRDY;
    cdrom->disc_present = false;
    cdrom->disc_file = NULL;
    cdrom->current_state = CD_STATE_IDLE;
    cdrom->motor_on = false;
    cdrom->xa_adpcm_playing = false;
    cdrom->stat_byte = 0x00;
    cdrom->shell_was_open = false;
    cdrom->last_id_error = false;
    cdrom->last_seek_error = false;
    fifo_init(&cdrom->param_fifo);
    fifo_init(&cdrom->response_fifo);
    LOG_CDROM_DEBUG("  CDROM Initial Status: 0x%02x\n", cdrom->status);
}

static void cmd_set_loc(Cdrom* cdrom) {
    LOG_CDROM_INFO("~ CDROM CMD: SetLoc (0x02)\n");
    if (cdrom->param_fifo.count < 3) {
        LOG_CDROM_WARN("  ERROR: SetLoc requires 3 parameters.\n");
        cdrom->status &= ~STAT_BUSY;
        update_status_register(cdrom);
        return;
    }
    uint8_t m = bcd_to_int(fifo_pop(&cdrom->param_fifo));
    uint8_t s = bcd_to_int(fifo_pop(&cdrom->param_fifo));
    uint8_t f = bcd_to_int(fifo_pop(&cdrom->param_fifo));
    cdrom->target_lba = (m * 60 * 75) + (s * 75) + f - 150;
    LOG_CDROM_INFO("  Set LBA to %u (M:%u S:%u F:%u)\n", cdrom->target_lba, m, s, f);
    cdrom->current_state = CD_STATE_CMD_EXEC;
    // Set busy flag already set by dispatcher
    cdrom_schedule_event(cdrom, 10000, cmd_set_loc_complete);
}

// ADDED completion handler
static void cmd_set_loc_complete(Cdrom* cdrom) {
    LOG_CDROM_INFO("~ CDROM CMD set_loc_complete)\n");
    cdrom->status &= ~STAT_BUSY;
    fifo_clear(&cdrom->param_fifo);
    update_stat_byte(cdrom);
    update_status_register(cdrom);
    fifo_push(&cdrom->response_fifo, cdrom->stat_byte);  // Return stat byte
    trigger_interrupt(cdrom, 3); // INT3
    cdrom->current_state = CD_STATE_IDLE;
}

static void cmd_play(Cdrom* cdrom) {
    LOG_CDROM_INFO("~ CDROM CMD: Play (0x03) - Start CD-DA playback\n");
    
    // Play command starts CD-DA audio playback from current position
    // For now, we'll just acknowledge the command - full audio implementation would be complex
    cdrom->current_state = CD_STATE_READING;  // Set to reading state for audio playback
    cdrom->status |= STAT_BUSY;
    update_stat_byte(cdrom);
    update_status_register(cdrom);
    fifo_push(&cdrom->response_fifo, cdrom->stat_byte);  // Return stat byte
    trigger_interrupt(cdrom, 3); // INT3: Command acknowledged
    
    // In a full implementation, this would start audio playback and send periodic INT1 reports
    // For now, just clear busy after a short delay
    cdrom_schedule_event(cdrom, 50000, cmd_play_complete);
}

static void cmd_play_complete(Cdrom* cdrom) {
    LOG_CDROM_INFO("  CDROM Play - Playback started\n");
    cdrom->status &= ~STAT_BUSY;
    // Set playing flag in stat byte
    cdrom->stat_byte |= STAT_BYTE_PLAYING;
    update_status_register(cdrom);
    // In full implementation, would continue sending INT1 with audio position
}

static void cmd_forward(Cdrom* cdrom) {
    LOG_CDROM_INFO("~ CDROM CMD: Forward (0x04) - Fast forward audio playback\n");
    
    // Forward command fast-forwards CD-DA audio playback
    // For now, just acknowledge the command
    cdrom->status &= ~STAT_BUSY;
    update_stat_byte(cdrom);
    update_status_register(cdrom);
    fifo_push(&cdrom->response_fifo, cdrom->stat_byte);  // Return stat byte
    trigger_interrupt(cdrom, 3); // INT3: Command acknowledged
}

// REMOVED cdrom_maybe_request_irq2

// cdrom_load_disc: No changes needed to your fixed version
bool cdrom_load_disc(Cdrom* cdrom, const char* bin_filename) {
    if (cdrom->disc_file) { fclose(cdrom->disc_file); cdrom->disc_file = NULL; }
    
    LOG_CDROM_INFO("CDROM: Attempting to load disc image '%s'\n", bin_filename);
    cdrom->disc_file = fopen(bin_filename, "rb");
    if (!cdrom->disc_file) {
        LOG_CDROM_ERROR("CDROM Error: Failed to open disc image: %s\n", bin_filename);
        cdrom->disc_present = false;
        return false;
    }
    
    // Your check for directory is good, keeping it
    fgetc(cdrom->disc_file);
    if (ferror(cdrom->disc_file)) {
        LOG_CDROM_ERROR("CDROM Error: Path is a directory or cannot be read: %s\n", bin_filename);
        fclose(cdrom->disc_file);
        cdrom->disc_file = NULL;
        cdrom->disc_present = false;
        return false;
    }
    rewind(cdrom->disc_file);

    LOG_CDROM_INFO("CDROM: Disc image loaded successfully.\n");
    cdrom->disc_present = true;
    cdrom->current_state = CD_STATE_IDLE;
    return true;
}

// cdrom_read_register: No changes needed
uint8_t cdrom_read_register(Cdrom* cdrom, uint32_t addr) {
    uint8_t offset = addr & 3;
    uint8_t reg_index = cdrom->index & 0x3;
    switch (offset) {
        case CDREG_INDEX:
            update_status_register(cdrom);
            return cdrom->status;
        case CDREG_RESPONSE:
            log_cdrom_response_read(cdrom);
            if (reg_index == 1) {
                uint8_t value = fifo_pop(&cdrom->response_fifo);
                update_status_register(cdrom);
                // Only re-assert IRQ2 if FIFO is not empty and INT3 is enabled, and INT3 is not already flagged
                if (!fifo_is_empty(&cdrom->response_fifo) && (cdrom->interrupt_enable & (1 << 2)) && !(cdrom->interrupt_flags & (1 << 2))) {
                    cdrom_set_irq_flag(cdrom, 1 << 2); // INT3: Response Ready
                }
                return value;
            }
            return 0;
        case CDREG_DATA:
            if (reg_index == 2 && cdrom->data_buffer_read_ptr < cdrom->data_buffer_count) {
                uint8_t value = cdrom->data_buffer[cdrom->data_buffer_read_ptr++];
                update_status_register(cdrom);
                return value;
            }
            return 0;
        case CDREG_IRQ_EN_FLAG:
            if (reg_index == 1) {
                return (cdrom->interrupt_enable & 0x1F) | ((cdrom->interrupt_flags & 0x7) << 5);
            }
            return 0;
        default:
            return 0;
    }
}

// cdrom_write_register: Real fix for register/index handling and command acceptance
void cdrom_write_register(Cdrom* cdrom, uint32_t addr, uint8_t value) {
    uint8_t offset = addr & 3;
    uint8_t reg_index = cdrom->index & 0x3;
    switch (offset) {
        case CDREG_INDEX:
            cdrom->index = value & 3;
            return;
        case CDREG_COMMAND:
            if (reg_index == 0) {
                if (!(cdrom->status & STAT_BUSY)) {
                    cdrom_handle_command(cdrom, value);
                }
            }
            return;
        case CDREG_PARAMETER:
            if (reg_index == 0) {
                fifo_push(&cdrom->param_fifo, value);
                update_status_register(cdrom);
            }
            return;
        case CDREG_REQUEST:
            if (reg_index == 0) {
                if (value & 0x80) {
                    fifo_clear(&cdrom->param_fifo);
                    update_status_register(cdrom);
                }
            } else if (reg_index == 1) {
                uint8_t prev_enable = cdrom->interrupt_enable;
                cdrom->interrupt_enable = value & 0x1F;
                cdrom->interrupt_flags &= ~(value & 0x1F);
                if (value & 0x40) {
                    cdrom->interrupt_flags = 0;
                }
                // Late enable logic: deliver IRQ if any flag is set and now enabled
                for (uint8_t bit = 1; bit <= 0x10; bit <<= 1) {
                    if ((cdrom->interrupt_flags & bit) && (cdrom->interrupt_enable & bit) && !(prev_enable & bit)) {
                        interconnect_request_irq(cdrom->inter, IRQ_CDROM, "CDROM late enable");
                    }
                }
            }
            return;
        default:
            return;
    }
}

static void cmd_read_n(Cdrom* cdrom) {
    LOG_CDROM_INFO("~ CDROM CMD: ReadN (0x06)\n");
    cdrom->current_state = CD_STATE_CMD_EXEC;
    cdrom->status |= STAT_BUSY;
    update_stat_byte(cdrom);
    update_status_register(cdrom);
    fifo_push(&cdrom->response_fifo, cdrom->stat_byte);  // Return stat byte
    trigger_interrupt(cdrom, 3); // First response
    cdrom_schedule_event(cdrom, 200000, cmd_read_n_complete);
}

// ADDED completion handler
static void cmd_read_n_complete(Cdrom* cdrom) {
    LOG_CDROM_INFO("  CDROM ReadN - Complete\n");
    cdrom->status &= ~STAT_BUSY;

    // --- Actual sector reading implementation ---
    if (!cdrom->disc_present || !cdrom->disc_file) {
        LOG_CDROM_WARN("  CDROM ReadN: No disc present!\n");
        cdrom->current_state = CD_STATE_ERROR;
        // Set error status and trigger INT5
        update_status_register(cdrom);
        fifo_push(&cdrom->response_fifo, (cdrom->status & ~STAT_RSLRDY) | 0x10); // Error status
        fifo_push(&cdrom->response_fifo, 0x80); // Error code: No Disc
        for (int i = 0; i < 6; ++i) fifo_push(&cdrom->response_fifo, 0);
        trigger_interrupt(cdrom, 5); // INT5: Error
        return;
    }

    // Seek to the correct LBA in the .bin file
    long sector_offset = (long)cdrom->target_lba * CD_USER_DATA_SIZE;
    if (fseek(cdrom->disc_file, sector_offset, SEEK_SET) != 0) {
        LOG_CDROM_WARN("  CDROM ReadN: fseek failed for LBA %u!\n", cdrom->target_lba);
        cdrom->current_state = CD_STATE_ERROR;
        update_status_register(cdrom);
        fifo_push(&cdrom->response_fifo, (cdrom->status & ~STAT_RSLRDY) | 0x10); // Error status
        fifo_push(&cdrom->response_fifo, 0x81); // Error code: Seek error
        for (int i = 0; i < 6; ++i) fifo_push(&cdrom->response_fifo, 0);
        trigger_interrupt(cdrom, 5); // INT5: Error
        return;
    }

    size_t bytes_read = fread(cdrom->data_buffer, 1, CD_USER_DATA_SIZE, cdrom->disc_file);
    if (bytes_read != CD_USER_DATA_SIZE) {
        LOG_CDROM_WARN("  CDROM ReadN: fread failed or incomplete for LBA %u!\n", cdrom->target_lba);
        cdrom->current_state = CD_STATE_ERROR;
        cdrom->last_seek_error = true;  // Mark seek/read error
        update_stat_byte(cdrom);
        update_status_register(cdrom);
        fifo_push(&cdrom->response_fifo, cdrom->stat_byte);  // First byte: stat with error flags
        fifo_push(&cdrom->response_fifo, 0x82); // Error code: Read error
        for (int i = 0; i < 6; ++i) fifo_push(&cdrom->response_fifo, 0);
        trigger_interrupt(cdrom, 5); // INT5: Error
        return;
    }

    cdrom->data_buffer_count = CD_USER_DATA_SIZE;
    cdrom->data_buffer_read_ptr = 0;
    cdrom->status |= STAT_DTEN; // Set Data FIFO not empty flag
    update_stat_byte(cdrom);
    update_status_register(cdrom);
    fifo_push(&cdrom->response_fifo, cdrom->stat_byte);  // Return stat byte
    trigger_interrupt(cdrom, 1); // INT1: Data Ready
    cdrom->current_state = CD_STATE_READING;

    // Increment LBA for continuous reading
    cdrom->target_lba++;
}

static void cmd_read_s(Cdrom* cdrom) {
    LOG_CDROM_INFO("~ CDROM CMD: ReadS (0x1B) - Read with retry\n");
    cdrom->current_state = CD_STATE_CMD_EXEC;
    cdrom->status |= STAT_BUSY;
    update_stat_byte(cdrom);
    update_status_register(cdrom);
    fifo_push(&cdrom->response_fifo, cdrom->stat_byte);  // Return stat byte
    trigger_interrupt(cdrom, 3); // First response
    cdrom_schedule_event(cdrom, 200000, cmd_read_s_complete);
}

// ADDED completion handler for ReadS (with retry logic)
static void cmd_read_s_complete(Cdrom* cdrom) {
    LOG_CDROM_INFO("  CDROM ReadS - Complete\n");
    cdrom->status &= ~STAT_BUSY;

    // --- Actual sector reading implementation with retry ---
    if (!cdrom->disc_present || !cdrom->disc_file) {
        LOG_CDROM_WARN("  CDROM ReadS: No disc present!\n");
        cdrom->current_state = CD_STATE_ERROR;
        // Set error status and trigger INT5
        update_status_register(cdrom);
        fifo_push(&cdrom->response_fifo, (cdrom->status & ~STAT_RSLRDY) | 0x10); // Error status
        fifo_push(&cdrom->response_fifo, 0x80); // Error code: No Disc
        for (int i = 0; i < 6; ++i) fifo_push(&cdrom->response_fifo, 0);
        trigger_interrupt(cdrom, 5); // INT5: Error
        return;
    }

    // Seek to the correct LBA in the .bin file
    long sector_offset = (long)cdrom->target_lba * CD_USER_DATA_SIZE;
    if (fseek(cdrom->disc_file, sector_offset, SEEK_SET) != 0) {
        LOG_CDROM_WARN("  CDROM ReadS: fseek failed for LBA %u!\n", cdrom->target_lba);
        cdrom->current_state = CD_STATE_ERROR;
        update_status_register(cdrom);
        fifo_push(&cdrom->response_fifo, (cdrom->status & ~STAT_RSLRDY) | 0x10); // Error status
        fifo_push(&cdrom->response_fifo, 0x81); // Error code: Seek error
        for (int i = 0; i < 6; ++i) fifo_push(&cdrom->response_fifo, 0);
        trigger_interrupt(cdrom, 5); // INT5: Error
        return;
    }

    size_t bytes_read = fread(cdrom->data_buffer, 1, CD_USER_DATA_SIZE, cdrom->disc_file);
    if (bytes_read != CD_USER_DATA_SIZE) {
        LOG_CDROM_WARN("  CDROM ReadS: fread failed or incomplete for LBA %u, retrying...\n", cdrom->target_lba);
        // ReadS has retry logic - try again
        cdrom_schedule_event(cdrom, 100000, cmd_read_s_complete); // Retry after delay
        return;
    }

    cdrom->data_buffer_count = CD_USER_DATA_SIZE;
    cdrom->data_buffer_read_ptr = 0;
    cdrom->status |= STAT_DTEN; // Set Data FIFO not empty flag
    update_stat_byte(cdrom);
    update_status_register(cdrom);
    fifo_push(&cdrom->response_fifo, cdrom->stat_byte);  // Return stat byte
    trigger_interrupt(cdrom, 1); // INT1: Data Ready
    cdrom->current_state = CD_STATE_READING;

    // Increment LBA for continuous reading
    cdrom->target_lba++;
}

static void cmd_seek_l(Cdrom* cdrom) {
    LOG_CDROM_INFO("~ CDROM CMD: SeekL (0x15) - Forwarding to SetLoc\n");
    cmd_set_loc(cdrom); // SeekL is mechanically the same as SetLoc for our purposes
}

static void cmd_seek_p(Cdrom* cdrom) {
    LOG_CDROM_INFO("~ CDROM CMD: SeekP (0x16) - Physical seek to SetLoc\n");
    cmd_set_loc(cdrom); // SeekP is mechanically the same as SetLoc for our purposes
}

static void cmd_set_mode(Cdrom* cdrom) {
    uint8_t mode = fifo_pop(&cdrom->param_fifo);
    LOG_CDROM_INFO("~ CDROM CMD: SetMode (0x0E) to 0x%02x", mode);
    
    // Parse mode bits according to PSX-SPEX
    cdrom->double_speed = (mode & 0x80) != 0;        // Bit 7: Speed (0=normal, 1=double)
    cdrom->xa_adpcm_enable = (mode & 0x40) != 0;     // Bit 6: XA-ADPCM (0=off, 1=send to SPU)
    cdrom->sector_size_is_2340 = (mode & 0x20) != 0; // Bit 5: Sector Size (0=800h, 1=924h)
    // Bit 4: Ignore bit (not used)
    cdrom->xa_filter_enable = (mode & 0x10) != 0;    // Bit 3: XA-Filter (0=off, 1=filter sectors)
    cdrom->report_enable = (mode & 0x08) != 0;       // Bit 2: Report (0=off, 1=enable audio report IRQs)
    cdrom->auto_pause_enable = (mode & 0x04) != 0;   // Bit 1: AutoPause (0=off, 1=pause at track end)
    cdrom->cdda_enable = (mode & 0x01) != 0;          // Bit 0: CDDA (0=off, 1=allow CD-DA sectors)

    // SetMode is instant - clear busy immediately
    cdrom->status &= ~STAT_BUSY;
    update_stat_byte(cdrom);
    update_status_register(cdrom);
    fifo_push(&cdrom->response_fifo, cdrom->stat_byte);  // Return stat byte
    trigger_interrupt(cdrom, 3); // INT3
}

static void cmd_get_param(Cdrom* cdrom) {
    LOG_CDROM_INFO("~ CDROM CMD: GetParam (0x0F)");
    
    // Reconstruct mode byte from individual flags
    uint8_t mode = 0;
    if (cdrom->double_speed) mode |= 0x80;
    if (cdrom->xa_adpcm_enable) mode |= 0x40;
    if (cdrom->sector_size_is_2340) mode |= 0x20;
    if (cdrom->xa_filter_enable) mode |= 0x10;
    if (cdrom->report_enable) mode |= 0x08;
    if (cdrom->auto_pause_enable) mode |= 0x04;
    if (cdrom->cdda_enable) mode |= 0x01;
    
    // GetParam is instant - clear busy immediately
    cdrom->status &= ~STAT_BUSY;
    update_stat_byte(cdrom);
    update_status_register(cdrom);
    fifo_push(&cdrom->response_fifo, cdrom->stat_byte);  // Return stat byte
    fifo_push(&cdrom->response_fifo, mode);              // Mode byte
    fifo_push(&cdrom->response_fifo, 0x00);              // Reserved (always 0x00)
    fifo_push(&cdrom->response_fifo, cdrom->xa_filter_file);    // File number
    fifo_push(&cdrom->response_fifo, cdrom->xa_filter_channel); // Channel number
    trigger_interrupt(cdrom, 3); // INT3
}

static void cmd_get_loc_l(Cdrom* cdrom) {
    LOG_CDROM_INFO("~ CDROM CMD: GetLocL (0x10) - Get logical position\n");
    
    // Convert current LBA back to MM:SS:FF format
    uint32_t lba = cdrom->target_lba;
    uint8_t m = (lba + 150) / (60 * 75);
    uint8_t s = ((lba + 150) % (60 * 75)) / 75;
    uint8_t f = ((lba + 150) % (60 * 75)) % 75;
    
    // GetLocL is instant - clear busy immediately
    cdrom->status &= ~STAT_BUSY;
    update_stat_byte(cdrom);
    update_status_register(cdrom);
    fifo_push(&cdrom->response_fifo, cdrom->stat_byte);  // Return stat byte
    fifo_push(&cdrom->response_fifo, int_to_bcd(m));     // Minutes (BCD)
    fifo_push(&cdrom->response_fifo, int_to_bcd(s));     // Seconds (BCD)
    fifo_push(&cdrom->response_fifo, int_to_bcd(f));     // Frames (BCD)
    fifo_push(&cdrom->response_fifo, 0x00);              // Track number (not implemented)
    fifo_push(&cdrom->response_fifo, 0x00);              // Index number (not implemented)
    fifo_push(&cdrom->response_fifo, 0x00);              // Reserved
    fifo_push(&cdrom->response_fifo, 0x00);              // Reserved
    trigger_interrupt(cdrom, 3); // INT3
}

static void cmd_get_loc_p(Cdrom* cdrom) {
    LOG_CDROM_INFO("~ CDROM CMD: GetLocP (0x11) - Get physical position\n");
    
    // For now, GetLocP returns the same as GetLocL
    // In a full implementation, this would return actual physical position
    cmd_get_loc_l(cdrom);
}

static void cmd_get_tn(Cdrom* cdrom) {
    LOG_CDROM_INFO("~ CDROM CMD: GetTN (0x13) - Get total number of tracks\n");
    
    // GetTN returns the first and last track numbers
    // For now, return dummy data: first track 1, last track 1
    cdrom->status &= ~STAT_BUSY;
    update_stat_byte(cdrom);
    update_status_register(cdrom);
    fifo_push(&cdrom->response_fifo, cdrom->stat_byte);  // Return stat byte
    fifo_push(&cdrom->response_fifo, int_to_bcd(1));     // First track (BCD)
    fifo_push(&cdrom->response_fifo, int_to_bcd(1));     // Last track (BCD)
    trigger_interrupt(cdrom, 3); // INT3
}

static void cmd_get_td(Cdrom* cdrom) {
    LOG_CDROM_INFO("~ CDROM CMD: GetTD (0x14) - Get track start position\n");
    
    uint8_t track = fifo_pop(&cdrom->param_fifo);
    LOG_CDROM_INFO("  Track: 0x%02x\n", track);
    
    // GetTD returns the start position of the specified track
    // For now, return dummy data: track starts at 00:02:00
    cdrom->status &= ~STAT_BUSY;
    update_stat_byte(cdrom);
    update_status_register(cdrom);
    fifo_push(&cdrom->response_fifo, cdrom->stat_byte);  // Return stat byte
    fifo_push(&cdrom->response_fifo, int_to_bcd(0));     // Minutes (BCD)
    fifo_push(&cdrom->response_fifo, int_to_bcd(2));     // Seconds (BCD)
    fifo_push(&cdrom->response_fifo, int_to_bcd(0));     // Frames (BCD)
    trigger_interrupt(cdrom, 3); // INT3
}

static void cmd_read_toc(Cdrom* cdrom) {
    LOG_CDROM_INFO("~ CDROM CMD: ReadTOC (0x1E) - Read Table of Contents\n");
    
    // ReadTOC takes time to read the TOC
    cdrom->current_state = CD_STATE_CMD_EXEC;
    cdrom->status |= STAT_BUSY;
    update_stat_byte(cdrom);
    update_status_register(cdrom);
    fifo_push(&cdrom->response_fifo, cdrom->stat_byte);  // Return stat byte
    trigger_interrupt(cdrom, 3); // INT3: Command acknowledged
    
    // Schedule completion after TOC read time
    cdrom_schedule_event(cdrom, 1000000, cmd_read_toc_complete); // ~1 second
}

static void cmd_read_toc_complete(Cdrom* cdrom) {
    LOG_CDROM_INFO("  CDROM ReadTOC - Complete\n");
    cdrom->status &= ~STAT_BUSY;
    update_stat_byte(cdrom);
    update_status_register(cdrom);
    fifo_push(&cdrom->response_fifo, cdrom->stat_byte);  // Return stat byte
    trigger_interrupt(cdrom, 2); // INT2: Command complete
    cdrom->current_state = CD_STATE_IDLE;
}

static void cmd_set_filter(Cdrom* cdrom) {
    uint8_t file = fifo_pop(&cdrom->param_fifo);
    uint8_t channel = fifo_pop(&cdrom->param_fifo);
    LOG_CDROM_INFO("~ CDROM CMD: SetFilter (0x0D) file=0x%02x channel=0x%02x", file, channel);
    
    cdrom->xa_filter_file = file;
    cdrom->xa_filter_channel = channel;
    
    // SetFilter is instant - clear busy immediately
    cdrom->status &= ~STAT_BUSY;
    update_stat_byte(cdrom);
    update_status_register(cdrom);
    fifo_push(&cdrom->response_fifo, cdrom->stat_byte);  // Return stat byte
    trigger_interrupt(cdrom, 3); // INT3
}

static void cmd_mute(Cdrom* cdrom) {
    LOG_CDROM_INFO("~ CDROM CMD: Mute (0x0B)");
    
    cdrom->audio_muted = true;
    
    // Mute is instant - clear busy immediately
    cdrom->status &= ~STAT_BUSY;
    update_stat_byte(cdrom);
    update_status_register(cdrom);
    fifo_push(&cdrom->response_fifo, cdrom->stat_byte);  // Return stat byte
    trigger_interrupt(cdrom, 3); // INT3
}

static void cmd_demute(Cdrom* cdrom) {
    LOG_CDROM_INFO("~ CDROM CMD: Demute (0x0C)");
    
    cdrom->audio_muted = false;
    
    // Demute is instant - clear busy immediately
    cdrom->status &= ~STAT_BUSY;
    update_stat_byte(cdrom);
    update_status_register(cdrom);
    fifo_push(&cdrom->response_fifo, cdrom->stat_byte);  // Return stat byte
    trigger_interrupt(cdrom, 3); // INT3
}

static void cmd_stop(Cdrom* cdrom) {
    LOG_CDROM_INFO("~ CDROM CMD: Stop (0x08)\n");
    cdrom->current_state = CD_STATE_IDLE;
    cdrom->status &= ~STAT_BUSY;
    cdrom->motor_on = false;
    update_stat_byte(cdrom);
    update_status_register(cdrom);
    fifo_push(&cdrom->response_fifo, cdrom->stat_byte);  // Return stat byte
    trigger_interrupt(cdrom, 2); // INT2
    // REMOVED: cdrom_maybe_request_irq2(cdrom, 1 << (2 - 1));
}

static void cmd_reset(Cdrom* cdrom) {
    LOG_CDROM_INFO("~ CDROM CMD: Reset (0x1C)");
    
    // Reset is instant - clear busy immediately and return response
    cdrom->status &= ~STAT_BUSY;
    update_stat_byte(cdrom);
    update_status_register(cdrom);
    fifo_push(&cdrom->response_fifo, cdrom->stat_byte);  // Return stat byte
    trigger_interrupt(cdrom, 3); // INT3
    
    // Schedule the actual reset after 1/8 second (400000 cycles)
    cdrom_schedule_event(cdrom, 400000, cmd_reset_complete);
}

static void cmd_reset_complete(Cdrom* cdrom) {
    LOG_CDROM_INFO("  CDROM Reset - Complete (HC05 rebooted)");
    
    // Reset internal state (similar to power-on reset)
    cdrom->current_state = CD_STATE_IDLE;
    cdrom->motor_on = false;
    cdrom->xa_adpcm_playing = false;
    cdrom->stat_byte = 0x00;
    cdrom->shell_was_open = false;
    cdrom->last_id_error = false;
    cdrom->last_seek_error = false;
    cdrom->interrupt_enable = 0x00;
    cdrom->interrupt_flags = 0x00;
    fifo_clear(&cdrom->param_fifo);
    fifo_clear(&cdrom->response_fifo);
    
    // Reset mode to default (not the Init mode)
    cdrom->double_speed = false;
    cdrom->xa_adpcm_enable = false;
    cdrom->sector_size_is_2340 = false;
    cdrom->xa_filter_enable = false;
    cdrom->report_enable = false;
    cdrom->auto_pause_enable = false;
    cdrom->cdda_enable = false;
    cdrom->xa_filter_file = 0;
    cdrom->xa_filter_channel = 0;
    cdrom->audio_muted = false;
    
    // Update status register
    update_status_register(cdrom);
}

// cdrom_step: No changes needed
void cdrom_step(Cdrom* cdrom, uint32_t cycles) {
    static uint32_t sector_timer = 0;
    if (cdrom->current_state == CD_STATE_READING) {
        sector_timer += cycles;
        // 33868800 / 75 = ~451,584 cycles per sector at 1x speed
        while (sector_timer >= 451584) {
            sector_timer -= 451584;
            // Only deliver a new sector if the previous one has been read out
            if (cdrom->data_buffer_read_ptr >= cdrom->data_buffer_count) {
                // Read next sector from disc
                if (!cdrom->disc_present || !cdrom->disc_file) {
                    LOG_CDROM_WARN("  [CDROM] Continuous Read: No disc present!\n");
                    cdrom->current_state = CD_STATE_ERROR;
                    cdrom->last_seek_error = true;
                    update_stat_byte(cdrom);
                    update_status_register(cdrom);
                    fifo_push(&cdrom->response_fifo, cdrom->stat_byte);  // First byte: stat with error flags
                    fifo_push(&cdrom->response_fifo, 0x80); // Error code: No Disc
                    for (int i = 0; i < 6; ++i) fifo_push(&cdrom->response_fifo, 0);
                    trigger_interrupt(cdrom, 5); // INT5: Error
                    return;
                }
                long sector_offset = (long)cdrom->target_lba * CD_USER_DATA_SIZE;
                if (fseek(cdrom->disc_file, sector_offset, SEEK_SET) != 0) {
                    LOG_CDROM_WARN("  [CDROM] Continuous Read: fseek failed for LBA %u!\n", cdrom->target_lba);
                    cdrom->current_state = CD_STATE_ERROR;
                    cdrom->last_seek_error = true;
                    update_stat_byte(cdrom);
                    update_status_register(cdrom);
                    fifo_push(&cdrom->response_fifo, cdrom->stat_byte);  // First byte: stat with error flags
                    fifo_push(&cdrom->response_fifo, 0x81); // Error code: Seek error
                    for (int i = 0; i < 6; ++i) fifo_push(&cdrom->response_fifo, 0);
                    trigger_interrupt(cdrom, 5); // INT5: Error
                    return;
                }
                size_t bytes_read = fread(cdrom->data_buffer, 1, CD_USER_DATA_SIZE, cdrom->disc_file);
                if (bytes_read != CD_USER_DATA_SIZE) {
                    LOG_CDROM_WARN("  [CDROM] Continuous Read: fread failed or incomplete for LBA %u!\n", cdrom->target_lba);
                    cdrom->current_state = CD_STATE_ERROR;
                    cdrom->last_seek_error = true;
                    update_stat_byte(cdrom);
                    update_status_register(cdrom);
                    fifo_push(&cdrom->response_fifo, cdrom->stat_byte);  // First byte: stat with error flags
                    fifo_push(&cdrom->response_fifo, 0x82); // Error code: Read error
                    for (int i = 0; i < 6; ++i) fifo_push(&cdrom->response_fifo, 0);
                    trigger_interrupt(cdrom, 5); // INT5: Error
                    return;
                }
                cdrom->data_buffer_count = CD_USER_DATA_SIZE;
                cdrom->data_buffer_read_ptr = 0;
                cdrom->status |= STAT_DTEN;
                update_stat_byte(cdrom);
                update_status_register(cdrom);
                fifo_push(&cdrom->response_fifo, cdrom->stat_byte);  // Return stat byte
                LOG_CDROM_INFO("  [CDROM] Delivering sector LBA %u, INT1\n", cdrom->target_lba);
                trigger_interrupt(cdrom, 1); // INT1: Data Ready
                cdrom->target_lba++;
            } else {
                // Wait for the CPU to read out the previous sector
                break;
            }
        }
    } else {
        sector_timer = 0; // Reset timer if not reading
    }
}

void cdrom_exec_cmd(Cdrom* cdrom, uint8_t cmd) {
    // 1. Set busy flag immediately
    cdrom->status |= STAT_BUSY;
    LOG_CDROM_INFO("[CDROM] CMD: 0x%02X (Set busy flag)\n", cmd);

    // 2. Populate response FIFO (example for Test command 0x19)
    fifo_clear(&cdrom->response_fifo);
    switch (cmd) {
        case 0x19: // Test
            fifo_push(&cdrom->response_fifo, 0x00); // Example: status OK
            LOG_CDROM_INFO("[CDROM] Test command response: 0x00\n");
            break;
        // Add other commands as needed
        default:
            fifo_push(&cdrom->response_fifo, 0x00); // Default response
            LOG_CDROM_INFO("[CDROM] Default command response: 0x00\n");
            break;
    }

    // 3. Clear busy flag and parameter FIFO after processing
    cdrom->status &= ~STAT_BUSY;
    fifo_clear(&cdrom->param_fifo);
    LOG_CDROM_INFO("[CDROM] CMD: 0x%02X (Clear busy flag, param FIFO)\n", cmd);

    // 4. Request IRQ2 (CDROM) after command completion
    LOG_CDROM_INFO("[CDROM] Requesting IRQ2 (CDROM)\n");
    interconnect_request_irq(cdrom->inter, IRQ_CDROM, "CDROM");
}

void cmd_test(Cdrom *cdrom) {
    uint8_t subcmd = fifo_is_empty(&cdrom->param_fifo) ? 0 : fifo_pop(&cdrom->param_fifo);
    LOG_CDROM_INFO("[CDROM] Test command: subcmd=0x%02x", subcmd);
    fifo_clear(&cdrom->response_fifo);
    // Per PSX-SPEX and DOCS: subcmd 0x20 returns BIOS version (Year, Month, Day, Version), INT3 after all 4 bytes
    if (subcmd == 0x20) {
        LOG_CDROM_INFO("[CDROM] Test 0x20: Returning BIOS version");
        fifo_push(&cdrom->response_fifo, 0x94);  // Year: 1994 (BCD)
        fifo_push(&cdrom->response_fifo, 0x09);  // Month: September (BCD)
        fifo_push(&cdrom->response_fifo, 0x19);  // Day: 19 (BCD)
        fifo_push(&cdrom->response_fifo, 0xC0);  // Version: vC0
        cdrom->status &= ~STAT_BUSY; // Clear BUSY before interrupt
        update_status_register(cdrom);
        trigger_interrupt(cdrom, 3);  // INT3: Response Ready
        return;
    }
    // For other subcommands, return stat byte and INT3
    update_stat_byte(cdrom);
    update_status_register(cdrom);
    fifo_push(&cdrom->response_fifo, cdrom->stat_byte);
    cdrom->status &= ~STAT_BUSY;
    trigger_interrupt(cdrom, 3);
    return;
}