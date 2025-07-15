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

// Logging: Only use LOG_ERROR/LOG_FATAL for unrecoverable errors, LOG_INFO for disc load success.

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
    return true;
}
static uint8_t fifo_pop(Fifo8* fifo) {
    if (fifo->count == 0) return 0;
    uint8_t value = fifo->data[fifo->read_ptr];
    fifo->read_ptr = (fifo->read_ptr + 1) % FIFO_SIZE;
    fifo->count--;
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
    cdrom->interrupt_flags |= irq_bit;
    LOG_CDROM_INFO("[CDROM] set_irq_flag: Set 0x%02x, flags now 0x%02x (was 0x%02x)\n", irq_bit, cdrom->interrupt_flags, prev_flags);
    if (cdrom->interrupt_enable & irq_bit) {
        LOG_CDROM_INFO("[CDROM] IRQ2 REQUESTED (set_irq_flag): enable=0x%02x, flags=0x%02x\n", cdrom->interrupt_enable, cdrom->interrupt_flags);
        interconnect_request_irq(cdrom->inter, IRQ_CDROM, "CDROM");
    } else {
        LOG_CDROM_DEBUG("[CDROM] IRQ2 NOT requested (enable=0x%02x, flags=0x%02x)\n", cdrom->interrupt_enable, cdrom->interrupt_flags);
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
    uint8_t preserved_bits = cdrom->status & (STAT_BUSY | STAT_MOTORON);
    cdrom->status = (cdrom->index & 0x03) | preserved_bits;

    if (fifo_is_empty(&cdrom->param_fifo)) cdrom->status |= STAT_PRMEMPT;
    if (!fifo_is_full(&cdrom->param_fifo)) cdrom->status |= STAT_PRMWRDY;
    if (!fifo_is_empty(&cdrom->response_fifo)) cdrom->status |= STAT_RSLRDY;
    if (cdrom->data_buffer_count > cdrom->data_buffer_read_ptr) cdrom->status |= STAT_DTEN;
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
    interconnect_request_irq(cdrom->inter, IRQ_CDROM, "CDROM");
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
    LOG_CDROM_INFO("  CDROM GetID - Step 2 (Completion)\n");
    cdrom->status &= ~STAT_BUSY;

    if (!cdrom->disc_present) {
        // No Disc Error response
        uint8_t error_status = (cdrom->status & ~STAT_RSLRDY) | 0x10; // Nocash says STAT=10h for No Disc Error
        fifo_push(&cdrom->response_fifo, error_status);
        fifo_push(&cdrom->response_fifo, 0x80); // Error Code: No Disc
        for(int i = 0; i < 6; ++i) fifo_push(&cdrom->response_fifo, 0);
        trigger_interrupt(cdrom, 5); // INT5: Error
    } else {
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
    interconnect_request_irq(cdrom->inter, IRQ_CDROM, "CDROM");
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
    interconnect_request_irq(cdrom->inter, IRQ_CDROM, "CDROM");
}

// --- Main command dispatcher: Only block if busy, not if interrupt pending ---
static void cdrom_handle_command(Cdrom* cdrom, uint8_t command) {
    if (cdrom->status & STAT_BUSY) {
        LOG_CDROM_WARN("CDROM: Command 0x%02x IGNORED (busy)\n", command);
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
            LOG_CDROM_ERROR("CDROM Error: Unhandled command 0x%02x\n", command);
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
    interconnect_request_irq(cdrom->inter, IRQ_CDROM, "CDROM");
}


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
    uint8_t reg_index = cdrom->index;

    if (offset == CDREG_INDEX) {
        update_status_register(cdrom);
        return cdrom->status;
    }

    switch (offset) {
        case CDREG_INDEX:
            update_status_register(cdrom);
            return cdrom->status;
        case CDREG_RESPONSE:
            return (reg_index == 1) ? fifo_pop(&cdrom->response_fifo) : 0;
        case CDREG_DATA:
            return (reg_index == 2 && cdrom->data_buffer_read_ptr < cdrom->data_buffer_count) ? cdrom->data_buffer[cdrom->data_buffer_read_ptr++] : 0;
        case CDREG_IRQ_EN_FLAG:
            return (reg_index == 1) ? (cdrom->interrupt_enable & 0x1F) | ((cdrom->interrupt_flags & 0x7) << 5) : 0;
    }
    return 0;
}

// cdrom_write_register: Real fix for register/index handling and command acceptance
void cdrom_write_register(Cdrom* cdrom, uint32_t addr, uint8_t value) {
    uint8_t offset = addr & 3;
    uint8_t reg_index = cdrom->index & 0x3; // Always mask to 2 bits
    LOG_CDROM_TRACE("CDROM Write: Index=%d, Offset=0x%x, Value=0x%02x\n", reg_index, offset, value);

    switch (offset) {
        case CDREG_INDEX:
            LOG_CDROM_TRACE("  -> Set Index to %d\n", value & 3);
            cdrom->index = value & 3; // Only change index here
            return;
        case CDREG_COMMAND:
            if (reg_index == 0) {
                if (!(cdrom->status & STAT_BUSY)) {
                    LOG_CDROM_INFO("  -> Command Write: 0x%02x (ACCEPTED)\n", value);
                    cdrom_handle_command(cdrom, value);
                } else {
                    LOG_CDROM_WARN("  -> Command Write: 0x%02x IGNORED (busy)\n", value);
                }
            } else {
                LOG_CDROM_TRACE("  -> Ignored Command Write (Index != 0)\n");
            }
            break;
        case CDREG_PARAMETER:
            if (reg_index == 0) {
                LOG_CDROM_TRACE("  -> Parameter FIFO Write: 0x%02x\n", value);
                fifo_push(&cdrom->param_fifo, value);
            } else {
                LOG_CDROM_TRACE("  -> Ignored Parameter Write (Index != 0)\n");
            }
            break;
        case CDREG_REQUEST:
            if (reg_index == 0) {
                LOG_CDROM_TRACE("  -> Request Write: 0x%02x\n", value);
                if (value & 0x80) {
                    LOG_CDROM_TRACE("    -> Clear Parameter FIFO\n");
                    fifo_clear(&cdrom->param_fifo);
                }
            } else if (reg_index == 1) {
                LOG_CDROM_TRACE("  -> IRQ Enable/Flags Write: 0x%02x\n", value);
                uint8_t prev_enable = cdrom->interrupt_enable;
                uint8_t prev_flags = cdrom->interrupt_flags;
                cdrom->interrupt_enable = value & 0x1F;
                // Clear only the flags for which the corresponding bit is set in value
                cdrom->interrupt_flags &= ~(value & 0x1F);
                if (value & 0x40) {
                    LOG_CDROM_TRACE("    -> Clear All Interrupt Flags\n");
                    cdrom->interrupt_flags = 0;
                }
                LOG_CDROM_INFO("[CDROM] IRQ Enable/Flags Write: enable=0x%02x (was 0x%02x), flags=0x%02x (was 0x%02x)\n", cdrom->interrupt_enable, prev_enable, cdrom->interrupt_flags, prev_flags);
                // After updating, if any enabled interrupt is pending, request IRQ2 (late enable logic)
                uint8_t pending = cdrom->interrupt_enable & cdrom->interrupt_flags;
                if (pending) {
                    LOG_CDROM_INFO("[CDROM] IRQ2 REQUESTED (enable/flags write): enable=0x%02x, flags=0x%02x\n", cdrom->interrupt_enable, cdrom->interrupt_flags);
                    interconnect_request_irq(cdrom->inter, IRQ_CDROM, "CDROM");
                }
            } else {
                LOG_CDROM_TRACE("  -> Ignored Request Write (Index != 0/1)\n");
            }
            break;
        default:
            LOG_CDROM_TRACE("  -> Unknown Write\n");
            break;
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
    interconnect_request_irq(cdrom->inter, IRQ_CDROM, "CDROM");
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
    interconnect_request_irq(cdrom->inter, IRQ_CDROM, "CDROM");
}

static void cmd_stop(Cdrom* cdrom) {
    LOG_CDROM_INFO("~ CDROM CMD: Stop (0x08)\n");
    cdrom->current_state = CD_STATE_IDLE;
    cdrom->status &= ~(STAT_BUSY | STAT_MOTORON);
    update_status_register(cdrom);
    fifo_push(&cdrom->response_fifo, cdrom->status);
    trigger_interrupt(cdrom, 2); // INT2
    interconnect_request_irq(cdrom->inter, IRQ_CDROM, "CDROM");
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

void cmd_test(Cdrom* cdrom) {
    uint8_t sub_command = fifo_pop(&cdrom->param_fifo);
    LOG_CDROM_INFO("~ CDROM CMD: Test (0x19), Sub: 0x%02x\n", sub_command);
    fifo_clear(&cdrom->response_fifo);
    // --- nocash/PSX-Spex: Test(0x20) must return status, year, month, day, version ---
    // 1. Push status (with RSLRDY set) and trigger INT3 (response ready)
    update_status_register(cdrom);
    fifo_push(&cdrom->response_fifo, cdrom->status | STAT_RSLRDY); // status byte
    update_status_register(cdrom);
    trigger_interrupt(cdrom, 3); // INT3: Response ready
    // 2. Push result bytes (year, month, day, version) or placeholder
    if (sub_command == 0x20) {
        fifo_push(&cdrom->response_fifo, 0x94); // Year
        fifo_push(&cdrom->response_fifo, 0x12); // Month
        fifo_push(&cdrom->response_fifo, 0x20); // Day
        fifo_push(&cdrom->response_fifo, 0xC2); // Version (from SCPH1001)
        LOG_CDROM_DEBUG("[CDROM] Test(0x20): Queued BIOS date/version response\n");
    } else {
        fifo_push(&cdrom->response_fifo, 0x00); // Placeholder for unknown subcommands
    }
    update_status_register(cdrom);
    // 3. Trigger INT2 (command complete) after result is available
    trigger_interrupt(cdrom, 2); // INT2: Command complete
    LOG_CDROM_INFO("[CDROM] Test(0x%02x): Response FIFO now has %d bytes\n", sub_command, cdrom->response_fifo.count);
    // --- End nocash/PSX-Spex compliance ---
    cdrom->current_state = CD_STATE_IDLE;
}