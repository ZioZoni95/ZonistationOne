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
    LOG_CDROM_IMPORTANT("[CDROM] Response register read. FIFO count: %d, HSTS: 0x%02x", cdrom->result_fifo.count, cdrom->hsts_register);
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

// --- PSX-SPX CDROM Register & Status Constants ---
#define CDROM_REG0  0  // HSTS (read) / ADDRESS (write)
#define CDROM_REG1  1  // RESULT/COMMAND (bank-switched)
#define CDROM_REG2  2  // RDDATA/PARAMETER (bank-switched)  
#define CDROM_REG3  3  // HINTSTS,HINTMSK/HCHPCTL,HCLRCTL (bank-switched)

// HSTS register bits per PSX-SPX
#define HSTS_RA_MASK    0x03  // Bits 0-1: Register bank
#define HSTS_ADPBUSY    0x04  // Bit 2: ADPCM busy
#define HSTS_PRMEMPT    0x08  // Bit 3: Parameter empty
#define HSTS_PRMWRDY    0x10  // Bit 4: Parameter write ready  
#define HSTS_RSLRRDY    0x20  // Bit 5: Result read ready
#define HSTS_DRQSTS     0x40  // Bit 6: Data request
#define HSTS_BUSYSTS    0x80  // Bit 7: Busy status

// Interrupt types per PSX-SPX (HINTSTS bits 0-2)
#define INT_NOINTR      0  // No interrupt
#define INT_DATAREADY   1  // Data ready
#define INT_COMPLETE    2  // Command complete  
#define INT_ACKNOWLEDGE 3  // Command acknowledge
#define INT_DATAEND     4  // Data end
#define INT_DISKERROR   5  // Disk error

// --- Internal Helper Functions ---

// Set interrupt type in HINTSTS register per PSX-SPX
static void set_interrupt_type(Cdrom* cdrom, uint8_t int_type) {
    if (int_type > INT_DISKERROR) {
        LOG_CDROM_ERROR("Invalid interrupt type: %d", int_type);
        return;
    }
    
    // Clear previous interrupt type (bits 0-2) and set new type
    cdrom->hintsts_register = (cdrom->hintsts_register & 0xF8) | (int_type & 0x07);
    
    LOG_CDROM_DEBUG("[CDROM] Set interrupt type: %d (HINTSTS=0x%02x)", 
                    int_type, cdrom->hintsts_register);
    
    // Request IRQ only if interrupt type is not INT_NOINTR
    if (int_type != INT_NOINTR) {
        interconnect_request_irq(cdrom->inter, IRQ_CDROM, "CDROM");
    }
}

// Replace trigger_interrupt to use PSX-SPX interrupt types
static void trigger_interrupt(Cdrom* cdrom, uint8_t int_type) {
    set_interrupt_type(cdrom, int_type);
}

static void cdrom_schedule_event(Cdrom* cdrom, uint32_t cycles, void (*handler)(Cdrom*)) {
    LOG_CDROM_INFO("[CDROM] Scheduling event: cycles=%u, handler=%p\n", cycles, (void*)handler);
    cdrom->pending_completion_handler = handler;
    eventq_schedule(cdrom->inter, EVQ_CDROM, cycles);
}

static void update_hsts_register(Cdrom* cdrom) {
    uint8_t old_hsts = cdrom->hsts_register;
    
    // Start with current register bank in bits 0-1
    cdrom->hsts_register = cdrom->register_bank & HSTS_RA_MASK;
    
    // Bit 2: ADPBUSY (ADPCM busy) - always 0 for now
    // cdrom->hsts_register |= HSTS_ADPBUSY;  // Set when XA-ADPCM playing
    
    // Bit 3: PRMEMPT (Parameter empty) 
    if (fifo_is_empty(&cdrom->param_fifo)) {
        cdrom->hsts_register |= HSTS_PRMEMPT;
    }
    
    // Bit 4: PRMWRDY (Parameter write ready)
    if (!fifo_is_full(&cdrom->param_fifo)) {
        cdrom->hsts_register |= HSTS_PRMWRDY;
    }
    
    // Bit 5: RSLRRDY (Result read ready)
    if (!fifo_is_empty(&cdrom->result_fifo)) {
        cdrom->hsts_register |= HSTS_RSLRRDY;
    }
    
    // Bit 6: DRQSTS (Data request) - set when data buffer has unread data
    if (cdrom->data_buffer_read_ptr < cdrom->data_buffer_count) {
        cdrom->hsts_register |= HSTS_DRQSTS;
    }
    
    // Bit 7: BUSYSTS (Busy status) - set when processing command
    if (cdrom->current_state == CD_STATE_CMD_EXEC) {
        cdrom->hsts_register |= HSTS_BUSYSTS;
    }
    
    // Log status changes for debugging
    log_cdrom_status_change(old_hsts, cdrom->hsts_register);
}

// --- Command Handlers (This is where the main logic is filled in) ---

static void cmd_get_stat(Cdrom* cdrom) {
    LOG_CDROM_INFO("~ CDROM CMD: GetStat (0x01)\n");    
    fifo_clear(&cdrom->result_fifo);
    update_hsts_register(cdrom);
    fifo_push(&cdrom->result_fifo, cdrom->hsts_register);
    trigger_interrupt(cdrom, INT_ACKNOWLEDGE); // INT3: Command acknowledged
}

// Init command per PSX-SPX: Sets mode=20h, activates motor, Standby, abort all commands
static void cmd_init(Cdrom* cdrom) {
    LOG_CDROM_INFO("~ CDROM CMD: Init (0x0A) - Step 1\n");
    cdrom->current_state = CD_STATE_CMD_EXEC;
    
    // First response is immediate (acknowledges command)
    update_hsts_register(cdrom);
    fifo_clear(&cdrom->result_fifo);
    fifo_push(&cdrom->result_fifo, cdrom->hsts_register);
    trigger_interrupt(cdrom, INT_ACKNOWLEDGE); // INT3: Command acknowledged

    // Schedule the completion response  
    cdrom_schedule_event(cdrom, 300000, cmd_init_complete);
}

static void cmd_init_complete(Cdrom* cdrom) {
    LOG_CDROM_INFO("  CDROM Init - Step 2 (Completion)\n");
    
    // Reset internal state per PSX-SPX
    cdrom->hintmsk_register = 0;
    fifo_clear(&cdrom->param_fifo);
    cdrom->double_speed = false;
    cdrom->sector_size_is_2340 = false;
    cdrom->current_state = CD_STATE_IDLE;
    
    // Second response (signals init is finished)
    update_hsts_register(cdrom);
    fifo_push(&cdrom->result_fifo, cdrom->hsts_register);
    trigger_interrupt(cdrom, INT_COMPLETE); // INT2: Command complete
}

// <<< MODIFIED: Implemented two-stage GetID >>>
static void cmd_get_id(Cdrom* cdrom) {
    LOG_CDROM_INFO("~ CDROM CMD: GetID (0x1A) - Step 1\n");
    cdrom->current_state = CD_STATE_CMD_EXEC;
    cdrom->hsts_register |= HSTS_BUSYSTS;

    // First response is immediate (acknowledges command)
    update_hsts_register(cdrom);
    fifo_clear(&cdrom->result_fifo);
    fifo_push(&cdrom->result_fifo, cdrom->hsts_register);
    trigger_interrupt(cdrom, INT_ACKNOWLEDGE);
    
    // Schedule the result
    cdrom_schedule_event(cdrom, 100000, cmd_get_id_complete);
}

static void cmd_get_id_complete(Cdrom* cdrom) {
    LOG_CDROM_INFO("  CDROM GetID - Step 2 (Completion)");
    cdrom->hsts_register &= ~HSTS_BUSYSTS;

    if (!cdrom->disc_present) {
        LOG_CDROM_IMPORTANT("[BOOT] BIOS is checking for disc: NO DISC PRESENT");
        // No Disc Error response
        uint8_t error_status = (cdrom->hsts_register & ~HSTS_RSLRRDY) | 0x10; // Nocash says STAT=10h for No Disc Error
        fifo_push(&cdrom->result_fifo, error_status);
        fifo_push(&cdrom->result_fifo, 0x80); // Error Code: No Disc
        for(int i = 0; i < 6; ++i) fifo_push(&cdrom->result_fifo, 0);
        LOG_CDROM_IMPORTANT("[BOOT] CDROM INT5 (error) triggered: No disc");
        trigger_interrupt(cdrom, INT_DISKERROR); // INT5: Error
    } else {
        LOG_CDROM_IMPORTANT("[BOOT] BIOS is checking for disc: DISC PRESENT");
        // Standard Licensed Disc response (SCEA)
        update_hsts_register(cdrom);
        fifo_push(&cdrom->result_fifo, cdrom->hsts_register);
        fifo_push(&cdrom->result_fifo, 0x02); // Status: Licensed
        fifo_push(&cdrom->result_fifo, 0x00); // Disc Type: CD-ROM
        fifo_push(&cdrom->result_fifo, 0x00);
        fifo_push(&cdrom->result_fifo, 'S');
        fifo_push(&cdrom->result_fifo, 'C');
        fifo_push(&cdrom->result_fifo, 'E');
        fifo_push(&cdrom->result_fifo, 'A');
        trigger_interrupt(cdrom, INT_COMPLETE); // INT2: Command Complete
    }
    cdrom->current_state = CD_STATE_IDLE;
    // REMOVED: cdrom_maybe_request_irq2(cdrom, 1 << (2 - 1));
}

// Stubs for other commands - no changes needed yet
static void cmd_pause(Cdrom* cdrom) {
    LOG_CDROM_INFO("~ CDROM CMD: Pause (0x09)\n");
    cdrom->current_state = CD_STATE_CMD_EXEC;
    cdrom->hsts_register |= HSTS_BUSYSTS;
    update_hsts_register(cdrom);
    fifo_push(&cdrom->result_fifo, cdrom->hsts_register);
    trigger_interrupt(cdrom, INT_ACKNOWLEDGE);
    cdrom_schedule_event(cdrom, 150000, cmd_pause_complete);
}

static void cmd_pause_complete(Cdrom* cdrom) {
    LOG_CDROM_INFO("  CDROM Pause - Complete\n");
    cdrom->hsts_register &= ~HSTS_BUSYSTS;
    cdrom->current_state = CD_STATE_IDLE;
    update_hsts_register(cdrom);
    fifo_push(&cdrom->result_fifo, cdrom->hsts_register);
    trigger_interrupt(cdrom, INT_COMPLETE);
    // REMOVED: cdrom_maybe_request_irq2(cdrom, 1 << (2 - 1));
}

// Main command dispatcher per PSX-SPX - only block if BUSYSTS set
static void cdrom_handle_command(Cdrom* cdrom, uint8_t command) {
    LOG_CDROM_DEBUG("[CDROM] Command received: 0x%02x", command);
    
    // Log parameters if any
    if (!fifo_is_empty(&cdrom->param_fifo)) {
        char param_str[64] = {0};
        for (int i = 0; i < cdrom->param_fifo.count; ++i) {
            int len = strlen(param_str);
            snprintf(param_str + len, sizeof(param_str) - len, "0x%02x ", 
                    cdrom->param_fifo.data[(cdrom->param_fifo.read_ptr + i) % FIFO_SIZE]);
        }
        LOG_CDROM_DEBUG("[CDROM] Command parameters: %s", param_str);
    }
    
    // Log first command received during boot
    static int first_cdrom_cmd_logged = 0;
    if (!first_cdrom_cmd_logged) {
        LOG_CDROM_INFO("[BOOT] First CDROM command received: 0x%02x", command);
        first_cdrom_cmd_logged = 1;
    }
    
    // Check if busy per PSX-SPX - commands are ignored if BUSYSTS is set
    if (cdrom->hsts_register & HSTS_BUSYSTS) {
        LOG_CDROM_WARN("CDROM: Command 0x%02x IGNORED (BUSYSTS set)", command);
        return;
    }
    
    cdrom->pending_command = command;
    cdrom->current_state = CD_STATE_CMD_EXEC; // Sets BUSYSTS via update_hsts_register
    update_hsts_register(cdrom);
    
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
            cdrom->current_state = CD_STATE_IDLE;
            update_hsts_register(cdrom);
            break;
    }
}

// --- Core Public Functions ---

void cdrom_init(Cdrom* cdrom, struct Interconnect* inter) {
    LOG_CDROM_INFO("Initializing CD-ROM with PSX-SPX compliance...\n");
    memset(cdrom, 0, sizeof(Cdrom));
    cdrom->inter = inter;
    
    // Initialize registers per PSX-SPX
    cdrom->register_bank = 0;
    cdrom->hsts_register = HSTS_PRMEMPT | HSTS_PRMWRDY; // Parameter FIFO empty and ready
    cdrom->hintsts_register = INT_NOINTR; // No interrupt pending
    cdrom->hintmsk_register = 0; // All interrupts disabled initially
    
    // Initialize state
    cdrom->disc_present = false;
    cdrom->disc_file = NULL;
    cdrom->current_state = CD_STATE_IDLE;
    
    // Initialize FIFOs
    fifo_init(&cdrom->param_fifo);
    fifo_init(&cdrom->result_fifo);
    
    LOG_CDROM_DEBUG("CDROM initialized - HSTS: 0x%02x, HINTSTS: 0x%02x", 
                    cdrom->hsts_register, cdrom->hintsts_register);
}

static void cmd_set_loc(Cdrom* cdrom) {
    LOG_CDROM_INFO("~ CDROM CMD: SetLoc (0x02)\n");
    if (cdrom->param_fifo.count < 3) {
        LOG_CDROM_WARN("  ERROR: SetLoc requires 3 parameters.\n");
        cdrom->current_state = CD_STATE_IDLE;
        update_hsts_register(cdrom);
        return;
    }
    uint8_t m = bcd_to_int(fifo_pop(&cdrom->param_fifo));
    uint8_t s = bcd_to_int(fifo_pop(&cdrom->param_fifo));
    uint8_t f = bcd_to_int(fifo_pop(&cdrom->param_fifo));
    cdrom->target_lba = (m * 60 * 75) + (s * 75) + f - 150;
    LOG_CDROM_INFO("  Set LBA to %u (M:%u S:%u F:%u)\n", cdrom->target_lba, m, s, f);
    cdrom_schedule_event(cdrom, 10000, cmd_set_loc_complete);
}

static void cmd_set_loc_complete(Cdrom* cdrom) {
    LOG_CDROM_INFO("~ CDROM CMD set_loc_complete)\n");
    cdrom->current_state = CD_STATE_IDLE;
    fifo_clear(&cdrom->param_fifo);
    update_hsts_register(cdrom);
    fifo_push(&cdrom->result_fifo, cdrom->hsts_register);
    trigger_interrupt(cdrom, INT_ACKNOWLEDGE); // INT3
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

// cdrom_read_register: PSX-SPX compliant bank-switched register reading
uint8_t cdrom_read_register(Cdrom* cdrom, uint32_t addr) {
    uint8_t offset = addr & 3;
    uint8_t bank = cdrom->register_bank & 3;
    
    switch (offset) {
        case CDROM_REG0: // HSTS (read all banks)
            update_hsts_register(cdrom);
            return cdrom->hsts_register;
            
        case CDROM_REG1: // RESULT (all banks)
            log_cdrom_response_read(cdrom);
            if (!fifo_is_empty(&cdrom->result_fifo)) {
                uint8_t value = fifo_pop(&cdrom->result_fifo);
                update_hsts_register(cdrom);
                return value;
            }
            return 0;
            
        case CDROM_REG2: // RDDATA (all banks)  
            if (cdrom->data_buffer_read_ptr < cdrom->data_buffer_count) {
                uint8_t value = cdrom->data_buffer[cdrom->data_buffer_read_ptr++];
                update_hsts_register(cdrom);
                return value;
            }
            return 0;
            
        case CDROM_REG3: // Bank-switched: HINTMSK (banks 0,2) / HINTSTS (banks 1,3)
            if (bank == 1 || bank == 3) {
                // HINTSTS: bits 0-2=interrupt type, bits 3-4=flags, bits 5-7=reserved (always 1)
                return cdrom->hintsts_register | 0xE0;
            } else {
                // HINTMSK: bits 0-2=interrupt enable, bits 5-7=reserved (always 1)  
                return cdrom->hintmsk_register | 0xE0;
            }
            
        default:
            return 0;
    }
}

// cdrom_write_register: PSX-SPX compliant bank-switched register writing  
void cdrom_write_register(Cdrom* cdrom, uint32_t addr, uint8_t value) {
    uint8_t offset = addr & 3;
    uint8_t bank = cdrom->register_bank & 3;
    
    switch (offset) {
        case CDROM_REG0: // ADDRESS (write all banks) - sets register bank
            cdrom->register_bank = value & 3;
            update_hsts_register(cdrom);
            return;
            
        case CDROM_REG1: // Bank-switched: COMMAND (bank 0) / WRDATA (bank 1)
            if (bank == 0) {
                // COMMAND - only accept if not busy per PSX-SPX
                if (!(cdrom->hsts_register & HSTS_BUSYSTS)) {
                    cdrom_handle_command(cdrom, value);
                }
            } else if (bank == 1) {
                // WRDATA - for sound map XA-ADPCM upload (not implemented)
                LOG_CDROM_WARN("WRDATA write not implemented: 0x%02x", value);
            }
            return;
            
        case CDROM_REG2: // Bank-switched: PARAMETER (bank 0) / CI (bank 2) 
            if (bank == 0) {
                // PARAMETER - add to parameter FIFO
                fifo_push(&cdrom->param_fifo, value);
                update_hsts_register(cdrom);
            } else if (bank == 2) {
                // CI - XA-ADPCM configuration (not implemented)
                LOG_CDROM_WARN("CI write not implemented: 0x%02x", value);
            }
            return;
            
        case CDROM_REG3: // Bank-switched register 3
            if (bank == 0) {
                // HCHPCTL - host chip control
                if (value & 0x80) { // BFRD - request sector buffer read
                    LOG_CDROM_DEBUG("BFRD: Buffer read request");
                }
                if (value & 0x40) { // BFWR - request sector buffer write  
                    LOG_CDROM_DEBUG("BFWR: Buffer write request");
                }
            } else if (bank == 1) {
                // HCLRCTL - interrupt acknowledge and control
                if (value & 0x07) { // CLRINT - acknowledge interrupt
                    cdrom->hintsts_register &= ~0x07; // Clear interrupt type
                    set_interrupt_type(cdrom, INT_NOINTR);
                }
                if (value & 0x40) { // CLRPRM - clear parameter FIFO
                    fifo_clear(&cdrom->param_fifo);
                    update_hsts_register(cdrom);
                }
                if (value & 0x80) { // CHPRST - reset decoder chip  
                    LOG_CDROM_INFO("CDROM decoder reset requested");
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
    cdrom->hsts_register |= HSTS_BUSYSTS;
    update_hsts_register(cdrom);
    fifo_push(&cdrom->result_fifo, cdrom->hsts_register);
    trigger_interrupt(cdrom, INT_ACKNOWLEDGE); // INT3: First response
    cdrom_schedule_event(cdrom, 200000, cmd_read_n_complete);
}

// ADDED completion handler
static void cmd_read_n_complete(Cdrom* cdrom) {
    LOG_CDROM_INFO("  CDROM ReadN - Complete\n");
    cdrom->hsts_register &= ~HSTS_BUSYSTS;

    // --- Actual sector reading implementation ---
    if (!cdrom->disc_present || !cdrom->disc_file) {
        LOG_CDROM_WARN("  CDROM ReadN: No disc present!\n");
        cdrom->current_state = CD_STATE_ERROR;
        // Set error status and trigger INT5
        update_hsts_register(cdrom);
        fifo_push(&cdrom->result_fifo, (cdrom->hsts_register & ~HSTS_RSLRRDY) | 0x10); // Error status
        fifo_push(&cdrom->result_fifo, 0x80); // Error code: No Disc
        for (int i = 0; i < 6; ++i) fifo_push(&cdrom->result_fifo, 0);
        trigger_interrupt(cdrom, INT_DISKERROR); // INT5: Error
        return;
    }

    // Seek to the correct LBA in the .bin file
    long sector_offset = (long)cdrom->target_lba * CD_USER_DATA_SIZE;
    if (fseek(cdrom->disc_file, sector_offset, SEEK_SET) != 0) {
        LOG_CDROM_WARN("  CDROM ReadN: fseek failed for LBA %u!\n", cdrom->target_lba);
        cdrom->current_state = CD_STATE_ERROR;
        update_hsts_register(cdrom);
        fifo_push(&cdrom->result_fifo, (cdrom->hsts_register & ~HSTS_RSLRRDY) | 0x10); // Error status
        fifo_push(&cdrom->result_fifo, 0x81); // Error code: Seek error
        for (int i = 0; i < 6; ++i) fifo_push(&cdrom->result_fifo, 0);
        trigger_interrupt(cdrom, INT_DISKERROR); // INT5: Error
        return;
    }

    size_t bytes_read = fread(cdrom->data_buffer, 1, CD_USER_DATA_SIZE, cdrom->disc_file);
    if (bytes_read != CD_USER_DATA_SIZE) {
        LOG_CDROM_WARN("  CDROM ReadN: fread failed or incomplete for LBA %u!\n", cdrom->target_lba);
        cdrom->current_state = CD_STATE_ERROR;
        update_hsts_register(cdrom);
        fifo_push(&cdrom->result_fifo, (cdrom->hsts_register & ~HSTS_RSLRRDY) | 0x10); // Error status
        fifo_push(&cdrom->result_fifo, 0x82); // Error code: Read error
        for (int i = 0; i < 6; ++i) fifo_push(&cdrom->result_fifo, 0);
        trigger_interrupt(cdrom, INT_DISKERROR); // INT5: Error
        return;
    }

    cdrom->data_buffer_count = CD_USER_DATA_SIZE;
    cdrom->data_buffer_read_ptr = 0;
    cdrom->hsts_register |= HSTS_DRQSTS; // Set Data FIFO not empty flag
    update_hsts_register(cdrom);
    fifo_push(&cdrom->result_fifo, cdrom->hsts_register);
    trigger_interrupt(cdrom, INT_DATAREADY); // INT1: Data Ready
    cdrom->current_state = CD_STATE_READING;

    // Increment LBA for continuous reading
    cdrom->target_lba++;
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

    update_hsts_register(cdrom);
    fifo_push(&cdrom->result_fifo, cdrom->hsts_register);
    trigger_interrupt(cdrom, INT_ACKNOWLEDGE); // INT3
    // REMOVED: cdrom_maybe_request_irq2(cdrom, 1 << (3 - 1));
}

static void cmd_stop(Cdrom* cdrom) {
    LOG_CDROM_INFO("~ CDROM CMD: Stop (0x08)\n");
    cdrom->current_state = CD_STATE_IDLE;
    cdrom->hsts_register &= ~(HSTS_BUSYSTS | HSTS_DRQSTS);
    update_hsts_register(cdrom);
    fifo_push(&cdrom->result_fifo, cdrom->hsts_register);
    trigger_interrupt(cdrom, INT_COMPLETE); // INT2
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
                    update_hsts_register(cdrom);
                    fifo_push(&cdrom->result_fifo, (cdrom->hsts_register & ~HSTS_RSLRRDY) | 0x10); // Error status
                    fifo_push(&cdrom->result_fifo, 0x80); // Error code: No Disc
                    for (int i = 0; i < 6; ++i) fifo_push(&cdrom->result_fifo, 0);
                    trigger_interrupt(cdrom, INT_DISKERROR); // INT5: Error
                    return;
                }
                long sector_offset = (long)cdrom->target_lba * CD_USER_DATA_SIZE;
                if (fseek(cdrom->disc_file, sector_offset, SEEK_SET) != 0) {
                    LOG_CDROM_WARN("  [CDROM] Continuous Read: fseek failed for LBA %u!\n", cdrom->target_lba);
                    cdrom->current_state = CD_STATE_ERROR;
                    update_hsts_register(cdrom);
                    fifo_push(&cdrom->result_fifo, (cdrom->hsts_register & ~HSTS_RSLRRDY) | 0x10); // Error status
                    fifo_push(&cdrom->result_fifo, 0x81); // Error code: Seek error
                    for (int i = 0; i < 6; ++i) fifo_push(&cdrom->result_fifo, 0);
                    trigger_interrupt(cdrom, INT_DISKERROR); // INT5: Error
                    return;
                }
                size_t bytes_read = fread(cdrom->data_buffer, 1, CD_USER_DATA_SIZE, cdrom->disc_file);
                if (bytes_read != CD_USER_DATA_SIZE) {
                    LOG_CDROM_WARN("  [CDROM] Continuous Read: fread failed or incomplete for LBA %u!\n", cdrom->target_lba);
                    cdrom->current_state = CD_STATE_ERROR;
                    update_hsts_register(cdrom);
                    fifo_push(&cdrom->result_fifo, (cdrom->hsts_register & ~HSTS_RSLRRDY) | 0x10); // Error status
                    fifo_push(&cdrom->result_fifo, 0x82); // Error code: Read error
                    for (int i = 0; i < 6; ++i) fifo_push(&cdrom->result_fifo, 0);
                    trigger_interrupt(cdrom, INT_DISKERROR); // INT5: Error
                    return;
                }
                cdrom->data_buffer_count = CD_USER_DATA_SIZE;
                cdrom->data_buffer_read_ptr = 0;
                cdrom->hsts_register |= HSTS_DRQSTS;
                update_hsts_register(cdrom);
                fifo_push(&cdrom->result_fifo, cdrom->hsts_register);
                LOG_CDROM_INFO("  [CDROM] Delivering sector LBA %u, INT1\n", cdrom->target_lba);
                trigger_interrupt(cdrom, INT_DATAREADY); // INT1: Data Ready
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
    cdrom->hsts_register |= HSTS_BUSYSTS;
    LOG_CDROM_INFO("[CDROM] CMD: 0x%02X (Set busy flag)\n", cmd);

    // 2. Populate response FIFO (example for Test command 0x19)
    fifo_clear(&cdrom->result_fifo);
    switch (cmd) {
        case 0x19: // Test
            fifo_push(&cdrom->result_fifo, 0x00); // Example: status OK
            LOG_CDROM_INFO("[CDROM] Test command response: 0x00\n");
            break;
        // Add other commands as needed
        default:
            fifo_push(&cdrom->result_fifo, 0x00); // Default response
            LOG_CDROM_INFO("[CDROM] Default command response: 0x00\n");
            break;
    }

    // 3. Clear busy flag and parameter FIFO after processing
    cdrom->hsts_register &= ~HSTS_BUSYSTS;
    fifo_clear(&cdrom->param_fifo);
    LOG_CDROM_INFO("[CDROM] CMD: 0x%02X (Clear busy flag, param FIFO)\n", cmd);

    // 4. Request IRQ2 (CDROM) after command completion
    LOG_CDROM_INFO("[CDROM] Requesting IRQ2 (CDROM)\n");
    interconnect_request_irq(cdrom->inter, IRQ_CDROM, "CDROM");
}

void cmd_test(Cdrom *cdrom) {
    uint8_t subcmd = fifo_is_empty(&cdrom->param_fifo) ? 0 : fifo_pop(&cdrom->param_fifo);
    
    LOG_CDROM_IMPORTANT("[BOOT] CDROM Test cmd 0x19, subcmd 0x%02x", subcmd);
    fifo_clear(&cdrom->result_fifo);
    
    switch (subcmd) {
        case 0x20: // Get CDROM Controller Version/Date per PSX-SPX
            LOG_CDROM_IMPORTANT("[BOOT] BIOS Test subcmd 0x20 - Controller version request");
            
            // PSX-SPX format: 19h,20h --> INT3(yy,mm,dd,ver)
            // Return SCPH-1001 compatible version (94h,09h,19h,C0h)
            fifo_push(&cdrom->result_fifo, 0x94); // Year: 1994 (BCD)
            fifo_push(&cdrom->result_fifo, 0x09); // Month: September (BCD)
            fifo_push(&cdrom->result_fifo, 0x19); // Day: 19 (BCD)
            fifo_push(&cdrom->result_fifo, 0xC0); // Version: vC0 (BCD)
            break;
            
        case 0x21: // Get Drive Switches per PSX-SPX
            fifo_push(&cdrom->result_fifo, 0x00); // POS0=0, DOOR=0 (closed)
            break;
            
        default:
            LOG_CDROM_WARN("Unknown test subcmd 0x%02x, returning generic response", subcmd);
            // Generic response for unknown subcommands
            fifo_push(&cdrom->result_fifo, 0x00);
            fifo_push(&cdrom->result_fifo, 0x00);  
            fifo_push(&cdrom->result_fifo, 0x00);
            fifo_push(&cdrom->result_fifo, 0x00);
            break;
    }
    
    // Update status and trigger interrupt
    update_hsts_register(cdrom);
    trigger_interrupt(cdrom, INT_ACKNOWLEDGE); // INT3: Command acknowledged
    LOG_CDROM_IMPORTANT("[BOOT] CDROM Test command completed, INT3 triggered");
}