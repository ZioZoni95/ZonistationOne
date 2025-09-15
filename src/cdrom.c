/*
 * Portions of this file are inspired by PCSX ReARMed (https://github.com/notaz/pcsx_rearmed)
 * Copyright (c) PCSX ReARMed authors. Used under the GNU GPL v2.
 * Logic and structure are adapted for clarity and uniqueness.
 */
// --- cdrom.c ---
#include "cdrom.h"
#include "interconnect.h" // For interrupt definitions/requests IRQ_CDROM
#include <stdio.h>
#include <string.h> // For memset
#include <stdlib.h> // For exit if needed
#include "log.h"

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
    if ((old_status & 0x40) != (new_status & 0x40)) {
        LOG_CDROM_IMPORTANT("[CDROM] Status BUSY changed: %d -> %d", (old_status & 0x40) != 0, (new_status & 0x40) != 0);
    }
    if ((old_status & 0x10) != (new_status & 0x10)) {
        LOG_CDROM_IMPORTANT("[CDROM] Status RSLRDY changed: %d -> %d", (old_status & 0x10) != 0, (new_status & 0x10) != 0);
    }
}

// --- Sector Structure Constants ---
#define CD_RAW_SECTOR_SIZE 2352
#define CD_USER_DATA_SIZE 2048
#define CD_MODE2_FORM1_HEADER_SIZE 24
#define CD_MODE_RAWISH_SIZE 2340
#define CD_MODE_RAWISH_OFFSET 12

// --- CDROM Commands (from your header) ---
#define CDC_GETSTAT     0x01
#define CDC_SETLOC      0x02
#define CDC_READN       0x06
#define CDC_STOP        0x08
#define CDC_PAUSE       0x09
#define CDC_INIT        0x0A
#define CDC_SETMODE     0x0E
#define CDC_SEEKL       0x15
#define CDC_TEST        0x19
#define CDC_GETID       0x1A

// --- Forward Declarations for Command Handlers ---
static void cdrom_handle_command(Cdrom* cdrom, uint8_t command);
static void cmd_get_stat(Cdrom* cdrom);
static void cmd_init(Cdrom* cdrom);
static void cmd_get_id(Cdrom* cdrom);
static void cmd_set_loc(Cdrom* cdrom);
static void cmd_read_n(Cdrom* cdrom);
static void cmd_pause(Cdrom* cdrom);
static void cmd_seek_l(Cdrom* cdrom);
static void cmd_set_mode(Cdrom* cdrom);
static void cmd_stop(Cdrom* cdrom);

// --- NEW: Forward declarations for completion handlers ---
static void cmd_init_complete(Cdrom* cdrom);
static void cmd_get_id_complete(Cdrom* cdrom);
static void cmd_pause_complete(Cdrom* cdrom);
static void cmd_read_n_complete(Cdrom* cdrom);
static void cmd_set_loc_complete(Cdrom* cdrom);

// --- Internal Helper Function Declarations ---
static void fifo_init(Fifo8* fifo);
static bool fifo_push(Fifo8* fifo, uint8_t value);
static uint8_t fifo_pop(Fifo8* fifo);
static void fifo_clear(Fifo8* fifo);
static bool fifo_is_empty(Fifo8* fifo);
static bool fifo_is_full(Fifo8* fifo);
static uint8_t bcd_to_int(uint8_t bcd);
static void update_status_register(Cdrom* cdrom);
static void trigger_interrupt(Cdrom* cdrom, uint8_t int_code);
static void cdrom_schedule_event(Cdrom* cdrom, uint32_t cycles, void (*handler)(Cdrom*));
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
static void fifo_clear(Fifo8* fifo) { fifo->count = 0; fifo->read_ptr = 0; }
static bool fifo_is_empty(Fifo8* fifo) { return fifo->count == 0; }
static bool fifo_is_full(Fifo8* fifo) { return fifo->count >= FIFO_SIZE; }
static uint8_t bcd_to_int(uint8_t bcd) { return ((bcd >> 4) * 10) + (bcd & 0x0F); }

// --- Status Register Bits (from nocash specs) ---
#define STAT_PRMEMPT    (1 << 2) // Parameter FIFO empty
#define STAT_PRMWRDY    (1 << 3) // Parameter FIFO not full
#define STAT_RSLRDY     (1 << 4) // Response FIFO Not Empty
#define STAT_DTEN       (1 << 5) // Data FIFO Not Empty
#define STAT_BUSY       (1 << 6)
#define STAT_MOTORON    (1 << 7) // Motor is on

// --- Internal Helper Functions ---

// Improved: Set/latch interrupt flag and request IRQ2 if enabled (robust late enable logic)
static void cdrom_set_irq_flag(Cdrom* cdrom, uint8_t irq_bit) {
    uint8_t prev_flags = cdrom->interrupt_flags;
    if ((cdrom->interrupt_flags & irq_bit) == 0) {
        cdrom->interrupt_flags |= irq_bit;
        LOG_CDROM_DEBUG("[CDROM] set_irq_flag: Set 0x%02x, flags now 0x%02x (was 0x%02x)", irq_bit, cdrom->interrupt_flags, prev_flags);
        LOG_CDROM_DEBUG("[CDROM] (set_irq_flag) enable=0x%02x, flags=0x%02x", cdrom->interrupt_enable, cdrom->interrupt_flags);
        interconnect_request_irq(cdrom->inter, IRQ_CDROM, "CDROM");
    }
}

// Replace trigger_interrupt to use cdrom_set_irq_flag
static void trigger_interrupt(Cdrom* cdrom, uint8_t int_code) {
    if (int_code > 0 && int_code < 8) {
        uint8_t flag_bit = 1 << (int_code - 1);
        cdrom_set_irq_flag(cdrom, flag_bit);
    }
}

static void cdrom_schedule_event(Cdrom* cdrom, uint32_t cycles, void (*handler)(Cdrom*)) {
    LOG_CDROM_INFO("[CDROM] Scheduling event: cycles=%u, handler=%p\n", cycles, (void*)handler);
    cdrom->pending_completion_handler = handler;
    eventq_schedule(cdrom->inter, EVQ_CDROM, cycles);
}

static void update_status_register(Cdrom* cdrom) {
    uint8_t old_status = cdrom->status;
    uint8_t preserved_bits = cdrom->status & (STAT_BUSY | STAT_MOTORON);
    cdrom->status = (cdrom->index & 0x03) | preserved_bits;

    if (fifo_is_empty(&cdrom->param_fifo)) cdrom->status |= STAT_PRMEMPT;
    if (!fifo_is_full(&cdrom->param_fifo)) cdrom->status |= STAT_PRMWRDY;
    if (!fifo_is_empty(&cdrom->response_fifo)) cdrom->status |= STAT_RSLRDY;
    if (cdrom->data_buffer_count > cdrom->data_buffer_read_ptr) cdrom->status |= STAT_DTEN;
    log_cdrom_status_change(old_status, cdrom->status);
}

// --- Command Handlers (This is where the main logic is filled in) ---

static void cmd_get_stat(Cdrom* cdrom) {
    LOG_CDROM_INFO("~ CDROM CMD: GetStat (0x01)\n");    
    fifo_clear(&cdrom->response_fifo);
    update_status_register(cdrom);
    fifo_push(&cdrom->response_fifo, cdrom->status);
    trigger_interrupt(cdrom, 3); // INT3: Response Ready
}

// <<< MODIFIED: Implemented two-stage Init >>>
static void cmd_init(Cdrom* cdrom) {
    LOG_CDROM_INFO("~ CDROM CMD: Init (0x0A) - Step 1\n");
    cdrom->current_state = CD_STATE_CMD_EXEC;
    cdrom->status |= STAT_BUSY | STAT_MOTORON;

    // First response is immediate (acknowledges command)
    update_status_register(cdrom);
    fifo_clear(&cdrom->response_fifo);
    fifo_push(&cdrom->response_fifo, cdrom->status);
    trigger_interrupt(cdrom, 3);

    // Schedule the second part (actual init and completion response)
    cdrom_schedule_event(cdrom, 300000, cmd_init_complete);
}

static void cmd_init_complete(Cdrom* cdrom) {
    LOG_CDROM_INFO("  CDROM Init - Step 2 (Completion)\n");
    cdrom->status &= ~STAT_BUSY;

    // Reset internal state
    cdrom->interrupt_enable = 0;
    cdrom->interrupt_flags = 0;
    fifo_clear(&cdrom->param_fifo);
    cdrom->double_speed = false;
    cdrom->sector_size_is_2340 = false;
    cdrom->current_state = CD_STATE_IDLE;
    
    // Second response (signals command is finished)
    update_status_register(cdrom);
    fifo_push(&cdrom->response_fifo, cdrom->status);
    trigger_interrupt(cdrom, 2); // INT2: Command Complete
    // REMOVED: cdrom_maybe_request_irq2(cdrom, 1 << (2 - 1));
}

// <<< MODIFIED: Implemented two-stage GetID >>>
static void cmd_get_id(Cdrom* cdrom) {
    LOG_CDROM_INFO("~ CDROM CMD: GetID (0x1A) - Step 1\n");
    cdrom->current_state = CD_STATE_CMD_EXEC;
    cdrom->status |= STAT_BUSY;

    // First response is immediate (acknowledges command)
    update_status_register(cdrom);
    fifo_clear(&cdrom->response_fifo);
    fifo_push(&cdrom->response_fifo, cdrom->status);
    trigger_interrupt(cdrom, 3);
    
    // Schedule the result
    cdrom_schedule_event(cdrom, 100000, cmd_get_id_complete);
}

static void cmd_get_id_complete(Cdrom* cdrom) {
    LOG_CDROM_INFO("  CDROM GetID - Step 2 (Completion)");
    cdrom->status &= ~STAT_BUSY;

    if (!cdrom->disc_present) {
        LOG_CDROM_IMPORTANT("[BOOT] BIOS is checking for disc: NO DISC PRESENT");
        // No Disc Error response
        uint8_t error_status = (cdrom->status & ~STAT_RSLRDY) | 0x10; // Nocash says STAT=10h for No Disc Error
        fifo_push(&cdrom->response_fifo, error_status);
        fifo_push(&cdrom->response_fifo, 0x80); // Error Code: No Disc
        for(int i = 0; i < 6; ++i) fifo_push(&cdrom->response_fifo, 0);
        LOG_CDROM_IMPORTANT("[BOOT] CDROM INT5 (error) triggered: No disc");
        trigger_interrupt(cdrom, 5); // INT5: Error
    } else {
        LOG_CDROM_IMPORTANT("[BOOT] BIOS is checking for disc: DISC PRESENT");
        // Standard Licensed Disc response (SCEA)
        update_status_register(cdrom);
        fifo_push(&cdrom->response_fifo, cdrom->status);
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
    // REMOVED: cdrom_maybe_request_irq2(cdrom, 1 << (2 - 1));
}

// Stubs for other commands - no changes needed yet
static void cmd_pause(Cdrom* cdrom) {
    LOG_CDROM_INFO("~ CDROM CMD: Pause (0x09)\n");
    cdrom->current_state = CD_STATE_CMD_EXEC;
    cdrom->status |= STAT_BUSY;
    update_status_register(cdrom);
    fifo_push(&cdrom->response_fifo, cdrom->status);
    trigger_interrupt(cdrom, 3);
    cdrom_schedule_event(cdrom, 150000, cmd_pause_complete);
}

static void cmd_pause_complete(Cdrom* cdrom) {
    LOG_CDROM_INFO("  CDROM Pause - Complete\n");
    cdrom->status &= ~STAT_BUSY;
    cdrom->current_state = CD_STATE_IDLE;
    update_status_register(cdrom);
    fifo_push(&cdrom->response_fifo, cdrom->status);
    trigger_interrupt(cdrom, 2);
    // REMOVED: cdrom_maybe_request_irq2(cdrom, 1 << (2 - 1));
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
        case CDC_GETSTAT: cmd_get_stat(cdrom); break;
        case CDC_SETLOC:  cmd_set_loc(cdrom); break;
        case CDC_READN:   cmd_read_n(cdrom); break;
        case CDC_PAUSE:   cmd_pause(cdrom); break;
        case CDC_INIT:    cmd_init(cdrom); break;
        case CDC_SETMODE: cmd_set_mode(cdrom); break;
        case CDC_STOP:    cmd_stop(cdrom); break;
        case CDC_SEEKL:   cmd_seek_l(cdrom); break;
        case CDC_TEST:    cmd_test(cdrom); break;
        case CDC_GETID:   cmd_get_id(cdrom); break;
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
    update_status_register(cdrom);
    fifo_push(&cdrom->response_fifo, cdrom->status);
    trigger_interrupt(cdrom, 3); // INT3
    cdrom->current_state = CD_STATE_IDLE;
    // REMOVED: cdrom_maybe_request_irq2(cdrom, 1 << (3 - 1));
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
                cdrom->interrupt_enable = value & 0x1F;
                cdrom->interrupt_flags &= ~(value & 0x1F);
                if (value & 0x40) {
                    cdrom->interrupt_flags = 0;
                }
                // Late enable logic
                if (cdrom->interrupt_enable & cdrom->interrupt_flags) {
                    interconnect_request_irq(cdrom->inter, IRQ_CDROM, "CDROM late enable");
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
    update_status_register(cdrom);
    fifo_push(&cdrom->response_fifo, cdrom->status);
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
        update_status_register(cdrom);
        fifo_push(&cdrom->response_fifo, (cdrom->status & ~STAT_RSLRDY) | 0x10); // Error status
        fifo_push(&cdrom->response_fifo, 0x82); // Error code: Read error
        for (int i = 0; i < 6; ++i) fifo_push(&cdrom->response_fifo, 0);
        trigger_interrupt(cdrom, 5); // INT5: Error
        return;
    }

    cdrom->data_buffer_count = CD_USER_DATA_SIZE;
    cdrom->data_buffer_read_ptr = 0;
    cdrom->status |= STAT_DTEN; // Set Data FIFO not empty flag
    update_status_register(cdrom);
    fifo_push(&cdrom->response_fifo, cdrom->status);
    trigger_interrupt(cdrom, 1); // INT1: Data Ready
    cdrom->current_state = CD_STATE_READING;

    // Increment LBA for continuous reading
    cdrom->target_lba++;
    // REMOVED: cdrom_maybe_request_irq2(cdrom, 1 << (1 - 1));
}

static void cmd_seek_l(Cdrom* cdrom) {
    LOG_CDROM_INFO("~ CDROM CMD: SeekL (0x15) - Forwarding to SetLoc\n");
    cmd_set_loc(cdrom); // SeekL is mechanically the same as SetLoc for our purposes
}

static void cmd_set_mode(Cdrom* cdrom) {
    uint8_t mode = fifo_pop(&cdrom->param_fifo);
    LOG_CDROM_INFO("~ CDROM CMD: SetMode (0x0E) to 0x%02x\n", mode);
    cdrom->double_speed = (mode & 0x80) != 0;
    cdrom->is_cd_da = (mode & 0x40) != 0;
    cdrom->sector_size_is_2340 = (mode & 0x20) != 0;

    update_status_register(cdrom);
    fifo_push(&cdrom->response_fifo, cdrom->status);
    trigger_interrupt(cdrom, 3); // INT3
    // REMOVED: cdrom_maybe_request_irq2(cdrom, 1 << (3 - 1));
}

static void cmd_stop(Cdrom* cdrom) {
    LOG_CDROM_INFO("~ CDROM CMD: Stop (0x08)\n");
    cdrom->current_state = CD_STATE_IDLE;
    cdrom->status &= ~(STAT_BUSY | STAT_MOTORON);
    update_status_register(cdrom);
    fifo_push(&cdrom->response_fifo, cdrom->status);
    trigger_interrupt(cdrom, 2); // INT2
    // REMOVED: cdrom_maybe_request_irq2(cdrom, 1 << (2 - 1));
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
                    update_status_register(cdrom);
                    fifo_push(&cdrom->response_fifo, (cdrom->status & ~STAT_RSLRDY) | 0x10); // Error status
                    fifo_push(&cdrom->response_fifo, 0x80); // Error code: No Disc
                    for (int i = 0; i < 6; ++i) fifo_push(&cdrom->response_fifo, 0);
                    trigger_interrupt(cdrom, 5); // INT5: Error
                    return;
                }
                long sector_offset = (long)cdrom->target_lba * CD_USER_DATA_SIZE;
                if (fseek(cdrom->disc_file, sector_offset, SEEK_SET) != 0) {
                    LOG_CDROM_WARN("  [CDROM] Continuous Read: fseek failed for LBA %u!\n", cdrom->target_lba);
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
                    LOG_CDROM_WARN("  [CDROM] Continuous Read: fread failed or incomplete for LBA %u!\n", cdrom->target_lba);
                    cdrom->current_state = CD_STATE_ERROR;
                    update_status_register(cdrom);
                    fifo_push(&cdrom->response_fifo, (cdrom->status & ~STAT_RSLRDY) | 0x10); // Error status
                    fifo_push(&cdrom->response_fifo, 0x82); // Error code: Read error
                    for (int i = 0; i < 6; ++i) fifo_push(&cdrom->response_fifo, 0);
                    trigger_interrupt(cdrom, 5); // INT5: Error
                    return;
                }
                cdrom->data_buffer_count = CD_USER_DATA_SIZE;
                cdrom->data_buffer_read_ptr = 0;
                cdrom->status |= STAT_DTEN;
                update_status_register(cdrom);
                fifo_push(&cdrom->response_fifo, cdrom->status);
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
    printf("CDROM: cmd_test called, subcmd=0x%02x\n", subcmd);
    fifo_clear(&cdrom->response_fifo);
    
    // Handle specific test subcommands
    switch (subcmd) {
        case 0x20: // Get CDROM Controller Version/Date
            LOG_CDROM_IMPORTANT("[BOOT] BIOS Test subcmd 0x20 - Controller version request");
            fifo_push(&cdrom->response_fifo, 0x1c); // Version info (SCPH-1001 compatible)
            fifo_push(&cdrom->response_fifo, 0x94);
            fifo_push(&cdrom->response_fifo, 0x00);
            fifo_push(&cdrom->response_fifo, 0x00);
            break;
        default:
            // Generic handshake response for unknown subcmds
            printf("CDROM: Handshake response for subcmd=0x%02x\n", subcmd);
            fifo_push(&cdrom->response_fifo, 0x1c);
            fifo_push(&cdrom->response_fifo, 0x94);
            fifo_push(&cdrom->response_fifo, 0x00);
            fifo_push(&cdrom->response_fifo, 0x00);
            break;
    }
    
    // Always trigger interrupt to notify BIOS that command completed
    update_status_register(cdrom);
    cdrom->status |= 0x20;  // Response ready
    trigger_interrupt(cdrom, 3);  // INT3 for successful command completion
    LOG_CDROM_IMPORTANT("[BOOT] CDROM Test command completed, INT3 triggered");
}