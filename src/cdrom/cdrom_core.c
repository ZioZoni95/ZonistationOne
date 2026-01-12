/**
 * @file cdrom_core.c
 * @brief CDROM Controller Implementation
 * 
 * Thread-safe CDROM controller for PlayStation 1 emulator.
 * Based on DuckStation implementation and PSX-SPX documentation.
 * 
 * All public functions are O(1) complexity unless otherwise noted.
 * FIFO operations use circular buffers for O(1) push/pop.
 * Command dispatch uses function pointers for O(1) execution.
 */

#include "cdrom/cdrom_core.h"
#include "cdrom/cdrom_types.h"
#include "log.h"
#include "interconnect.h"
#include <string.h>
#include <stdlib.h>

// ============================================================================
// Forward Declarations
// ============================================================================

static void cdrom_fifo_init(CdromFifo* fifo);
void cdrom_send_ack(CdromState* cdrom, struct Interconnect* inter);
void cdrom_send_error(CdromState* cdrom, struct Interconnect* inter, uint8_t reason);
void cdrom_send_response_internal(CdromState* cdrom, struct Interconnect* inter, 
                                        CdromInterrupt int_type, uint8_t* data, uint8_t count);
static uint32_t cdrom_get_ack_delay_cycles(CdromCommand cmd);

// Command handlers - now in cdrom_commands.c
extern void cmd_sync(CdromState* cdrom, struct Interconnect* inter);
extern void cmd_getstat(CdromState* cdrom, struct Interconnect* inter);
extern void cmd_setloc(CdromState* cdrom, struct Interconnect* inter);
extern void cmd_play(CdromState* cdrom, struct Interconnect* inter);
extern void cmd_forward(CdromState* cdrom, struct Interconnect* inter);
extern void cmd_backward(CdromState* cdrom, struct Interconnect* inter);
extern void cmd_readn(CdromState* cdrom, struct Interconnect* inter);
extern void cmd_motoron(CdromState* cdrom, struct Interconnect* inter);
extern void cmd_stop(CdromState* cdrom, struct Interconnect* inter);
extern void cmd_pause(CdromState* cdrom, struct Interconnect* inter);
extern void cmd_init(CdromState* cdrom, struct Interconnect* inter);
extern void cmd_mute(CdromState* cdrom, struct Interconnect* inter);
extern void cmd_demute(CdromState* cdrom, struct Interconnect* inter);
extern void cmd_setfilter(CdromState* cdrom, struct Interconnect* inter);
extern void cmd_setmode(CdromState* cdrom, struct Interconnect* inter);
extern void cmd_getmode(CdromState* cdrom, struct Interconnect* inter);
extern void cmd_getlocl(CdromState* cdrom, struct Interconnect* inter);
extern void cmd_getlocp(CdromState* cdrom, struct Interconnect* inter);
extern void cmd_readt(CdromState* cdrom, struct Interconnect* inter);
extern void cmd_gettn(CdromState* cdrom, struct Interconnect* inter);
extern void cmd_gettd(CdromState* cdrom, struct Interconnect* inter);
extern void cmd_seekl(CdromState* cdrom, struct Interconnect* inter);
extern void cmd_seekp(CdromState* cdrom, struct Interconnect* inter);
extern void cmd_setclock(CdromState* cdrom, struct Interconnect* inter);
extern void cmd_getclock(CdromState* cdrom, struct Interconnect* inter);
extern void cmd_test(CdromState* cdrom, struct Interconnect* inter);
extern void cmd_getid(CdromState* cdrom, struct Interconnect* inter);
extern void cmd_reads(CdromState* cdrom, struct Interconnect* inter);
extern void cmd_reset(CdromState* cdrom, struct Interconnect* inter);
extern void cmd_getq(CdromState* cdrom, struct Interconnect* inter);
extern void cmd_readtoc(CdromState* cdrom, struct Interconnect* inter);
extern void cmd_videocd(CdromState* cdrom, struct Interconnect* inter);
extern void cdrom_execute_second_response(CdromState* cdrom, struct Interconnect* inter);

// Command dispatch table - O(1) lookup
typedef void (*CdromCommandHandler)(CdromState*, struct Interconnect*);

static const CdromCommandHandler command_table[32] = {
    cmd_sync,       // 0x00: Sync (returns error - invalid command)
    cmd_getstat,    // 0x01: GetStat
    cmd_setloc,     // 0x02: SetLoc
    cmd_play,       // 0x03: Play
    cmd_forward,    // 0x04: Forward
    cmd_backward,   // 0x05: Backward
    cmd_readn,      // 0x06: ReadN
    cmd_motoron,    // 0x07: MotorOn/Standby
    cmd_stop,       // 0x08: Stop
    cmd_pause,      // 0x09: Pause
    cmd_init,       // 0x0A: Init
    cmd_mute,       // 0x0B: Mute
    cmd_demute,     // 0x0C: Demute
    cmd_setfilter,  // 0x0D: SetFilter
    cmd_setmode,    // 0x0E: SetMode
    cmd_getmode,    // 0x0F: GetMode/Getparam
    cmd_getlocl,    // 0x10: GetLocL
    cmd_getlocp,    // 0x11: GetLocP
    cmd_readt,      // 0x12: ReadT (alternate ReadTOC)
    cmd_gettn,      // 0x13: GetTN
    cmd_gettd,      // 0x14: GetTD
    cmd_seekl,      // 0x15: SeekL
    cmd_seekp,      // 0x16: SeekP
    cmd_setclock,   // 0x17: SetClock (undocumented)
    cmd_getclock,   // 0x18: GetClock (undocumented)
    cmd_test,       // 0x19: Test
    cmd_getid,      // 0x1A: GetID
    cmd_reads,      // 0x1B: ReadS
    cmd_reset,      // 0x1C: Reset
    cmd_getq,       // 0x1D: GetQ (vC1+)
    cmd_readtoc,    // 0x1E: ReadTOC (vC1+)
    cmd_videocd     // 0x1F: VideoCD (SCPH-5903 only)
};

// ============================================================================
// Lifecycle Management
// ============================================================================

void cdrom_init(CdromState* cdrom) {
    memset(cdrom, 0, sizeof(CdromState));
    
    // Initialize mutex (recursive for nested locking)
    mutex_init(&cdrom->lock);
    
    // Initialize FIFOs
    cdrom_fifo_init(&cdrom->param_fifo);
    cdrom_fifo_init(&cdrom->response_fifo);
    cdrom_fifo_init(&cdrom->async_response_fifo);
    
    // Set default state
    cdrom->interrupt_enable = 0x1F;  // All interrupts enabled
    cdrom->drive_state = DRIVE_IDLE;
    cdrom->current_command = CMD_NONE;
    cdrom->pending_command = CMD_NONE;
    cdrom->second_response_cmd = CMD_NONE;
    
    // Default status: Shell open (no disc)
    cdrom->secondary_status = STAT_SHELL_OPEN;
    
    // Default audio volume (normal stereo)
    cdrom->cd_audio_volume_l_to_l = 0x80;
    cdrom->cd_audio_volume_r_to_r = 0x80;
    cdrom->cd_audio_volume_l_to_r = 0x00;
    cdrom->cd_audio_volume_r_to_l = 0x00;
    
    cdrom->log_enabled = true;
    
    LOG_CDROM_INFO("[CDROM] Controller initialized (thread-safe)");
}

void cdrom_shutdown(CdromState* cdrom) {
    mutex_lock(&cdrom->lock);
    
    // Close disc if loaded
    if (cdrom->disc_file) {
        fclose(cdrom->disc_file);
        cdrom->disc_file = NULL;
        LOG_CDROM_INFO("[CDROM] Disc closed: %s", cdrom->disc_path);
    }
    
    // Print statistics
    if (cdrom->log_enabled) {
        cdrom_print_stats(cdrom);
    }
    
    mutex_unlock(&cdrom->lock);
    mutex_destroy(&cdrom->lock);
    
    LOG_CDROM_INFO("[CDROM] Controller shut down");
}

void cdrom_reset(CdromState* cdrom) {
    mutex_lock(&cdrom->lock);
    
    // Save disc information
    FILE* disc = cdrom->disc_file;
    uint32_t disc_size = cdrom->disc_size_sectors;
    bool disc_present = cdrom->disc_present;
    char disc_path_copy[256];
    strncpy(disc_path_copy, cdrom->disc_path, sizeof(disc_path_copy));
    
    // Clear all state except mutex and stats
    Mutex lock_copy = cdrom->lock;
    uint64_t stats_commands = cdrom->command_count;
    uint64_t stats_sectors = cdrom->sector_reads;
    uint64_t stats_interrupts = cdrom->interrupt_count;
    uint64_t stats_errors = cdrom->error_count;
    bool log_enabled = cdrom->log_enabled;
    
    memset(cdrom, 0, sizeof(CdromState));
    
    // Restore preserved data
    cdrom->lock = lock_copy;
    cdrom->disc_file = disc;
    cdrom->disc_size_sectors = disc_size;
    cdrom->disc_present = disc_present;
    strncpy(cdrom->disc_path, disc_path_copy, sizeof(cdrom->disc_path));
    cdrom->command_count = stats_commands;
    cdrom->sector_reads = stats_sectors;
    cdrom->interrupt_count = stats_interrupts;
    cdrom->error_count = stats_errors;
    cdrom->log_enabled = log_enabled;
    
    // Re-initialize FIFOs
    cdrom_fifo_init(&cdrom->param_fifo);
    cdrom_fifo_init(&cdrom->response_fifo);
    cdrom_fifo_init(&cdrom->async_response_fifo);
    
    // Reset state
    cdrom->interrupt_enable = 0x1F;
    cdrom->drive_state = DRIVE_IDLE;
    cdrom->current_command = CMD_NONE;
    cdrom->pending_command = CMD_NONE;
    cdrom->second_response_cmd = CMD_NONE;
    cdrom->motor_on = disc_present;  // Motor on if disc present
    
    // Default audio volume
    cdrom->cd_audio_volume_l_to_l = 0x80;
    cdrom->cd_audio_volume_r_to_r = 0x80;
    
    mutex_unlock(&cdrom->lock);
    
    LOG_CDROM_INFO("[CDROM] Controller reset");
}

// ============================================================================
// Disc Management
// ============================================================================

bool cdrom_load_disc(CdromState* cdrom, const char* cue_path) {
    mutex_lock(&cdrom->lock);
    
    // Parse CUE file to find BIN file
    FILE* cue = fopen(cue_path, "r");
    if (!cue) {
        LOG_CDROM_ERROR("[CDROM] Failed to open CUE file: %s", cue_path);
        mutex_unlock(&cdrom->lock);
        return false;
    }
    
    char line[256];
    char bin_filename[256] = {0};
    
    while (fgets(line, sizeof(line), cue)) {
        if (strstr(line, "FILE")) {
            char *quote1 = strchr(line, '"');
            if (quote1) {
                char *quote2 = strchr(quote1 + 1, '"');
                if (quote2) {
                    size_t len = quote2 - quote1 - 1;
                    if (len < sizeof(bin_filename)) {
                        strncpy(bin_filename, quote1 + 1, len);
                        bin_filename[len] = '\0';
                        break;
                    }
                }
            }
        }
    }
    fclose(cue);
    
    if (bin_filename[0] == '\0') {
        LOG_CDROM_ERROR("[CDROM] Could not find BIN file in CUE");
        mutex_unlock(&cdrom->lock);
        return false;
    }
    
    // Build path to BIN file (same directory as CUE)
    char bin_path[512];
    const char* last_slash = strrchr(cue_path, '/');
    if (!last_slash) last_slash = strrchr(cue_path, '\\');
    
    if (last_slash) {
        size_t dir_len = last_slash - cue_path + 1;
        strncpy(bin_path, cue_path, dir_len);
        bin_path[dir_len] = '\0';
        strncat(bin_path, bin_filename, sizeof(bin_path) - dir_len - 1);
    } else {
        strncpy(bin_path, bin_filename, sizeof(bin_path));
    }
    
    // Open BIN file
    FILE* bin = fopen(bin_path, "rb");
    if (!bin) {
        LOG_CDROM_ERROR("[CDROM] Failed to open BIN file: %s", bin_path);
        mutex_unlock(&cdrom->lock);
        return false;
    }
    
    // Get file size to calculate sector count
    fseek(bin, 0, SEEK_END);
    long file_size = ftell(bin);
    fseek(bin, 0, SEEK_SET);
    
    uint32_t sector_count = file_size / CDROM_SECTOR_SIZE_RAW;
    
    // Close old disc if present
    if (cdrom->disc_file) {
        fclose(cdrom->disc_file);
    }
    
    // Set disc info
    cdrom->disc_file = bin;
    cdrom->disc_size_sectors = sector_count;
    cdrom->disc_present = true;
    cdrom->motor_on = true;
    strncpy(cdrom->disc_path, bin_path, sizeof(cdrom->disc_path) - 1);
    
    // Update status
    cdrom->secondary_status |= STAT_MOTOR_ON;
    cdrom->secondary_status &= ~STAT_SHELL_OPEN;
    
    mutex_unlock(&cdrom->lock);
    
    LOG_CDROM_INFO("[CDROM] Disc loaded: %s (%u sectors)", bin_path, sector_count);
    return true;
}

void cdrom_eject_disc(CdromState* cdrom) {
    mutex_lock(&cdrom->lock);
    
    if (cdrom->disc_file) {
        fclose(cdrom->disc_file);
        cdrom->disc_file = NULL;
        LOG_CDROM_INFO("[CDROM] Disc ejected: %s", cdrom->disc_path);
    }
    
    cdrom->disc_present = false;
    cdrom->motor_on = false;
    cdrom->disc_size_sectors = 0;
    cdrom->disc_path[0] = '\0';
    
    // Update status
    cdrom->secondary_status &= ~STAT_MOTOR_ON;
    cdrom->secondary_status |= STAT_SHELL_OPEN;
    
    // Stop any ongoing operations
    cdrom->drive_state = DRIVE_IDLE;
    cdrom->secondary_status &= ~(STAT_READING | STAT_SEEKING | STAT_PLAYING_CDDA);
    
    mutex_unlock(&cdrom->lock);
}

bool cdrom_has_disc(const CdromState* cdrom) {
    mutex_lock((Mutex*)&cdrom->lock);  // Cast away const for mutex
    bool has_disc = cdrom->disc_present;
    mutex_unlock((Mutex*)&cdrom->lock);
    return has_disc;
}

// ============================================================================
// Hardware Register Access
// ============================================================================

uint8_t cdrom_read_register(CdromState* cdrom, uint32_t offset) {
    mutex_lock(&cdrom->lock);
    
    uint8_t bank = cdrom->status_register & 0x03;
    uint8_t value = 0;
    
    switch(offset) {
        case 0: // HSTS (status register) - all banks
            value = cdrom->status_register;
            // Build status bits
            if (cdrom->mode_register & MODE_XA_ENABLE) value |= 0x04; // ADPBUSY
            if (cdrom_fifo_is_empty(&cdrom->param_fifo)) value |= 0x08; // PRMEMPT
            if (!cdrom_fifo_is_full(&cdrom->param_fifo)) value |= 0x10; // PRMWRDY
            if (!cdrom_fifo_is_empty(&cdrom->response_fifo)) value |= 0x20; // RSLRRDY
            if (cdrom->request_register & 0x80) value |= 0x40; // DRQSTS (BFRD set)
            if (cdrom->current_command != CMD_NONE) value |= 0x80; // BUSYSTS
            break;
            
        case 1: // RESULT (response FIFO) - all banks
            if (!cdrom_fifo_is_empty(&cdrom->response_fifo)) {
                cdrom_pop_response(cdrom, &value);
            }
            break;
            
        case 2: // RDDATA (sector data) - all banks
            if (cdrom->sector_buffer.position < cdrom->sector_buffer.size) {
                value = cdrom->sector_buffer.data[cdrom->sector_buffer.position++];
            }
            break;
            
        case 3: // HINTSTS/HINTMSK (bank-dependent)
            if (bank & 1) {
                // Banks 1, 3: HINTSTS (interrupt status)
                value = cdrom->interrupt_flag | 0xE0; // Bits 5-7 always 1
            } else {
                // Banks 0, 2: HINTMSK (interrupt mask)
                value = cdrom->interrupt_enable | 0xE0; // Bits 5-7 always 1
            }
            break;
    }
    
    mutex_unlock(&cdrom->lock);
    return value;
}

void cdrom_write_register(CdromState* cdrom, struct Interconnect* inter, uint32_t offset, uint8_t value) {
    mutex_lock(&cdrom->lock);
    
    uint8_t bank = cdrom->status_register & 0x03;
    
    switch(offset) {
        case 0: // ADDRESS (bank select) - all banks
            cdrom->status_register = (cdrom->status_register & ~0x03) | (value & 0x03);
            break;
            
        case 1: // Bank-dependent
            switch(bank) {
                case 0: // COMMAND
                    if (cdrom->current_command == CMD_NONE) {
                        uint8_t cmd = value & 0x1F;
                        if (cdrom->log_enabled) {
                            LOG_CDROM_DEBUG("[CDROM] Command received: 0x%02x", cmd);
                        }
                        // Execute command immediately with interconnect context
                        mutex_unlock(&cdrom->lock);  // Unlock before execute
                        cdrom_execute_command(cdrom, inter, cmd);
                        return;  // Already unlocked
                    }
                    break;
                case 1: // WRDATA (write to sector buffer - not typically used)
                    break;
                case 2: // CI (unknown/unused)
                    break;
                case 3: // ATV2 (R->R volume)
                    cdrom->cd_audio_volume_r_to_r = value;
                    break;
            }
            break;
            
        case 2: // Bank-dependent
            switch(bank) {
                case 0: // PARAMETER
                    if (!cdrom_fifo_is_full(&cdrom->param_fifo)) {
                        cdrom_push_param(cdrom, value);
                    }
                    break;
                case 1: // HINTMSK (interrupt enable)
                    cdrom->interrupt_enable = value & 0x1F;
                    break;
                case 2: // ATV0 (L->L volume)
                    cdrom->cd_audio_volume_l_to_l = value;
                    break;
                case 3: // ATV3 (R->L volume)
                    cdrom->cd_audio_volume_r_to_l = value;
                    break;
            }
            break;
            
        case 3: // Bank-dependent
            switch(bank) {
                case 0: // HCHPCTL (request register)
                    cdrom->request_register = value;
                    if (value & 0x80) { // BFRD
                        // Prepare sector buffer for reading
                        cdrom->sector_buffer.position = 0;
                    }
                    break;
                case 1: // HCLRCTL (control register)
                    if (value & 0x07) {
                        // Acknowledge interrupt (bits 0-2)
                        cdrom_acknowledge_interrupt(cdrom, value & 0x07);
                    }
                    if (value & 0x40) {
                        // Clear parameter FIFO
                        cdrom_clear_param_fifo(cdrom);
                    }
                    break;
                case 2: // ATV1 (L->R volume)
                    cdrom->cd_audio_volume_l_to_r = value;
                    break;
                case 3: // ADPCTL (apply volume changes)
                    // Volume changes take effect here
                    break;
            }
            break;
    }
    
    mutex_unlock(&cdrom->lock);
}

// ============================================================================
// DMA Interface
// ============================================================================

void cdrom_dma_read(CdromState* cdrom, uint32_t* words, uint32_t word_count) {
    mutex_lock(&cdrom->lock);
    
    // Read 32-bit words (4 bytes each) from sector buffer
    // Complexity: O(n) where n = word_count (typically 512 for 2KB sector)
    for (uint32_t i = 0; i < word_count; i++) {
        uint32_t word = 0;
        
        // Read 4 bytes in little-endian order
        for (int b = 0; b < 4; b++) {
            if (cdrom->sector_buffer.position < cdrom->sector_buffer.size) {
                word |= (uint32_t)cdrom->sector_buffer.data[cdrom->sector_buffer.position++] << (b * 8);
            }
        }
        
        words[i] = word;
    }
    
    mutex_unlock(&cdrom->lock);
}

// ============================================================================
// Event Callbacks
// ============================================================================

void cdrom_command_event(CdromState* cdrom, uint32_t cycles_late) {
    (void)cycles_late; // Unused for now
    
    mutex_lock(&cdrom->lock);
    
    if (cdrom->pending_command != CMD_NONE) {
        cdrom->current_command = cdrom->pending_command;
        cdrom->pending_command = CMD_NONE;
        cdrom->command_count++;
        
        // This will be implemented to call execute_command with interconnect
        if (cdrom->log_enabled) {
            LOG_CDROM_DEBUG("[CDROM] Executing command: 0x%02x", cdrom->current_command);
        }
    }
    
    mutex_unlock(&cdrom->lock);
}

void cdrom_drive_event(CdromState* cdrom, uint32_t cycles_late) {
    (void)cycles_late;
    
    mutex_lock(&cdrom->lock);
    
    // Drive state machine - advances reading/seeking/playing
    switch(cdrom->drive_state) {
        case DRIVE_SEEKING_LOGICAL:
        case DRIVE_SEEKING_PHYSICAL:
            // Complete seek operation
            cdrom->current_lba = cdrom->seek_end_lba;
            cdrom->drive_state = DRIVE_IDLE;
            cdrom->secondary_status &= ~STAT_SEEKING;
            
            if (cdrom->read_after_seek) {
                cdrom->drive_state = DRIVE_READING;
                cdrom->secondary_status |= STAT_READING;
            } else if (cdrom->play_after_seek) {
                cdrom->drive_state = DRIVE_PLAYING;
                cdrom->secondary_status |= STAT_PLAYING_CDDA;
            }
            break;
            
        case DRIVE_READING:
            // Read next sector
            if (cdrom_read_sector(cdrom, NULL)) {
                cdrom->current_lba++;
                cdrom->sector_reads++;
                // Schedule INT1 (data ready)
            }
            break;
            
        case DRIVE_PLAYING:
            // Read audio sector
            if (cdrom_read_sector(cdrom, NULL)) {
                cdrom->current_lba++;
                cdrom->sector_reads++;
                // Decode CD-DA audio
            }
            break;
            
        default:
            break;
    }
    
    mutex_unlock(&cdrom->lock);
}

void cdrom_interrupt_event(CdromState* cdrom, struct Interconnect* inter, uint32_t cycles_late) {
    (void)cycles_late;
    
    mutex_lock(&cdrom->lock);
    
    if (cdrom->async_interrupt != INT_NONE) {
        cdrom_trigger_interrupt(cdrom, inter, (CdromInterrupt)cdrom->async_interrupt);
        cdrom->async_interrupt = INT_NONE;
    }
    
    mutex_unlock(&cdrom->lock);
}

// Continued in next part due to size...
/**
 * @file cdrom_commands.c
 * @brief CDROM Command Implementations (Part 2)
 * 
 * Command handlers and helper functions for CDROM controller.
 * This file continues from cdrom_core.c
 */

// This content should be appended to cdrom_core.c
// Or included at the end of cdrom_core.c

// ============================================================================
// FIFO Operations (O(1) complexity)
// ============================================================================

static void cdrom_fifo_init(CdromFifo* fifo) {
    fifo->head = 0;
    fifo->tail = 0;
    fifo->count = 0;
}

bool cdrom_push_param(CdromState* cdrom, uint8_t value) {
    if (cdrom_fifo_is_full(&cdrom->param_fifo)) {
        return false;
    }
    
    cdrom->param_fifo.data[cdrom->param_fifo.tail] = value;
    cdrom->param_fifo.tail = (cdrom->param_fifo.tail + 1) % CDROM_FIFO_SIZE;
    cdrom->param_fifo.count++;
    return true;
}

bool cdrom_pop_param(CdromState* cdrom, uint8_t* value_out) {
    if (cdrom_fifo_is_empty(&cdrom->param_fifo)) {
        return false;
    }
    
    *value_out = cdrom->param_fifo.data[cdrom->param_fifo.head];
    cdrom->param_fifo.head = (cdrom->param_fifo.head + 1) % CDROM_FIFO_SIZE;
    cdrom->param_fifo.count--;
    return true;
}

bool cdrom_push_response(CdromState* cdrom, uint8_t value) {
    if (cdrom_fifo_is_full(&cdrom->response_fifo)) {
        return false;
    }
    
    cdrom->response_fifo.data[cdrom->response_fifo.tail] = value;
    cdrom->response_fifo.tail = (cdrom->response_fifo.tail + 1) % CDROM_FIFO_SIZE;
    cdrom->response_fifo.count++;
    return true;
}

bool cdrom_pop_response(CdromState* cdrom, uint8_t* value_out) {
    if (cdrom_fifo_is_empty(&cdrom->response_fifo)) {
        return false;
    }
    
    *value_out = cdrom->response_fifo.data[cdrom->response_fifo.head];
    cdrom->response_fifo.head = (cdrom->response_fifo.head + 1) % CDROM_FIFO_SIZE;
    cdrom->response_fifo.count--;
    return true;
}

void cdrom_clear_param_fifo(CdromState* cdrom) {
    cdrom_fifo_init(&cdrom->param_fifo);
}

void cdrom_clear_response_fifo(CdromState* cdrom) {
    cdrom_fifo_init(&cdrom->response_fifo);
}

// ============================================================================
// Interrupt Management
// ============================================================================

void cdrom_trigger_interrupt(CdromState* cdrom, struct Interconnect* inter, CdromInterrupt interrupt_type) {
    cdrom->interrupt_flag = (uint8_t)interrupt_type;
    cdrom->interrupt_count++;
    
    // Request IRQ_CDROM (IRQ2) via interconnect
    interconnect_request_irq(inter, IRQ_CDROM, "CDROM");
    
    if (cdrom->log_enabled) {
        LOG_CDROM_DEBUG("[CDROM] INT%d triggered", interrupt_type);
    }
}

bool cdrom_has_pending_interrupt(const CdromState* cdrom) {
    return (cdrom->interrupt_flag & 0x07) != 0;
}

void cdrom_acknowledge_interrupt(CdromState* cdrom, uint8_t ack_mask) {
    cdrom->interrupt_flag &= ~(ack_mask & 0x07);
    
    // Clear response FIFO after ACK
    if (ack_mask & 0x07) {
        cdrom_clear_response_fifo(cdrom);
    }
}

// ============================================================================
// Command Execution
// ============================================================================

void cdrom_execute_command(CdromState* cdrom, struct Interconnect* inter, uint8_t command) {
    mutex_lock(&cdrom->lock);
    
    cdrom->current_command = command;
    
    if (command >= 32 || command_table[command] == NULL) {
        // Invalid command - send error response (INT5)
        cdrom_send_error(cdrom, inter, ERROR_REASON_INVALID_COMMAND);
        cdrom->error_count++;
        mutex_unlock(&cdrom->lock);
        return;
    }
    
    // Execute command handler (O(1) function pointer dispatch)
    command_table[command](cdrom, inter);
    
    cdrom->current_command = CMD_NONE;
    mutex_unlock(&cdrom->lock);
}

// ============================================================================
// Sector Reading
// ============================================================================

bool cdrom_read_sector(CdromState* cdrom, struct Interconnect* inter) {
    if (!cdrom->disc_present || !cdrom->disc_file) {
        return false;
    }
    
    // Calculate file offset (O(1) arithmetic)
    long offset = (long)cdrom->current_lba * CDROM_SECTOR_SIZE_RAW;
    
    // Seek to sector position (O(1) file operation)
    if (fseek(cdrom->disc_file, offset, SEEK_SET) != 0) {
        LOG_CDROM_ERROR("[CDROM] Seek failed: LBA=%u", cdrom->current_lba);
        return false;
    }
    
    // Read raw sector (O(1) - fixed size read)
    size_t bytes_read = fread(cdrom->sector_buffer.data, 1, CDROM_SECTOR_SIZE_RAW, cdrom->disc_file);
    
    if (bytes_read != CDROM_SECTOR_SIZE_RAW) {
        LOG_CDROM_ERROR("[CDROM] Read failed: LBA=%u, got %zu bytes", cdrom->current_lba, bytes_read);
        return false;
    }
    
    // Set buffer size based on mode
    if (cdrom->mode_register & MODE_READ_RAW) {
        // Raw mode: 2340 bytes (excluding 12-byte sync pattern)
        cdrom->sector_buffer.size = CDROM_SECTOR_SIZE_RAW - CDROM_SECTOR_SYNC_SIZE;
    } else {
        // Data mode: 2048 bytes
        cdrom->sector_buffer.size = CDROM_SECTOR_SIZE_DATA;
    }
    
    cdrom->sector_buffer.position = 0;
    
    // Trigger INT1 interrupt to signal data ready (only if interconnect provided)
    if (inter) {
        uint8_t status = cdrom_get_status_byte(cdrom);
        cdrom_send_response_internal(cdrom, inter, INT_DATA_READY, &status, 1);
        
        LOG_CDROM_DEBUG("[CDROM] Sector read complete: LBA=%u, size=%u bytes", 
                       cdrom->current_lba, cdrom->sector_buffer.size);
    }
    
    return true;
}

uint8_t cdrom_get_status_byte(const CdromState* cdrom) {
    return cdrom->secondary_status;
}

// ============================================================================
// Response Helpers
// ============================================================================

void cdrom_send_response_internal(CdromState* cdrom, struct Interconnect* inter, 
                                        CdromInterrupt int_type, uint8_t* data, uint8_t count) {
    // Clear old response
    cdrom_clear_response_fifo(cdrom);
    
    // Push response bytes
    for (uint8_t i = 0; i < count; i++) {
        cdrom_push_response(cdrom, data[i]);
    }
    
    // Trigger interrupt
    cdrom_trigger_interrupt(cdrom, inter, int_type);
}

void cdrom_send_ack(CdromState* cdrom, struct Interconnect* inter) {
    uint8_t status = cdrom_get_status_byte(cdrom);
    cdrom_send_response_internal(cdrom, inter, INT_ACK, &status, 1);
}

void cdrom_send_error(CdromState* cdrom, struct Interconnect* inter, uint8_t reason) {
    uint8_t response[2];
    response[0] = cdrom_get_status_byte(cdrom) | STAT_ERROR;
    response[1] = reason;
    cdrom_send_response_internal(cdrom, inter, INT_ERROR, response, 2);
}

// Helper to queue a second response (INT2/INT5) for commands that need it
// Forward declare the callback function
static void cdrom_second_response_callback(void* context, uint32_t cycles_late);

// Context structure for second response callbacks
typedef struct {
    CdromState* cdrom;
    struct Interconnect* inter;
} CdromEventContext;

// Allocate context on heap (will be freed after callback)
static CdromEventContext* cdrom_context_pool[8] = {NULL};  // Pool of 8 contexts
static int cdrom_context_pool_next = 0;

void cdrom_queue_second_response(CdromState* cdrom, struct Interconnect* inter, CdromCommand cmd, uint32_t delay_cycles) {
    cdrom->second_response_cmd = cmd;
    
    // Allocate context from pool or heap
    CdromEventContext* ctx = cdrom_context_pool[cdrom_context_pool_next];
    if (!ctx) {
        ctx = malloc(sizeof(CdromEventContext));
        cdrom_context_pool[cdrom_context_pool_next] = ctx;
    }
    cdrom_context_pool_next = (cdrom_context_pool_next + 1) % 8;
    
    ctx->cdrom = cdrom;
    ctx->inter = inter;
    
    // Schedule event using interconnect_schedule_event
    interconnect_schedule_event(inter, delay_cycles, cdrom_second_response_callback, ctx, "CDROM_2nd_Response");
    
    LOG_CDROM_DEBUG("[CDROM] Queued second response for command 0x%02X (delay=%u cycles)", 
                   cmd, delay_cycles);
}

// Callback for second response event
static void cdrom_second_response_callback(void* context, uint32_t cycles_late) {
    CdromEventContext* ctx = (CdromEventContext*)context;
    CdromState* cdrom = ctx->cdrom;
    struct Interconnect* inter = ctx->inter;
    (void)cycles_late; // Unused for now
    
    LOG_CDROM_DEBUG("[CDROM] Second response callback fired for command 0x%02X", cdrom->second_response_cmd);
    
    // Forward declare cdrom_execute_second_response from cdrom_commands.c
    extern void cdrom_execute_second_response(CdromState* cdrom, struct Interconnect* inter);
    
    // Execute the pending second response
    cdrom_execute_second_response(cdrom, inter);
    
    // Note: Context is recycled through pool, not freed
}

// ============================================================================
// Command Handlers (PSX-SPX Documentation)
// ============================================================================

// Audio Output
// ============================================================================

bool cdrom_get_audio_frame(CdromState* cdrom, int16_t* left_out, int16_t* right_out) {
    mutex_lock(&cdrom->lock);
    
    // TODO: Implement audio FIFO for CD-DA and XA-ADPCM
    // For now, return silence
    *left_out = 0;
    *right_out = 0;
    
    mutex_unlock(&cdrom->lock);
    return false;
}

// ============================================================================
// Debug and Statistics
// ============================================================================

void cdrom_print_stats(const CdromState* cdrom) {
    LOG_CDROM_INFO("[CDROM] === Statistics ===");
    LOG_CDROM_INFO("[CDROM]   Commands: %llu", (unsigned long long)cdrom->command_count);
    LOG_CDROM_INFO("[CDROM]   Sectors:  %llu", (unsigned long long)cdrom->sector_reads);
    LOG_CDROM_INFO("[CDROM]   Interrupts: %llu", (unsigned long long)cdrom->interrupt_count);
    LOG_CDROM_INFO("[CDROM]   Errors:   %llu", (unsigned long long)cdrom->error_count);
}

void cdrom_set_logging(CdromState* cdrom, bool enabled) {
    mutex_lock(&cdrom->lock);
    cdrom->log_enabled = enabled;
    mutex_unlock(&cdrom->lock);
}

// ============================================================================
// Timing Helpers (for event scheduler integration)
// ============================================================================

static uint32_t cdrom_get_ack_delay_cycles(CdromCommand cmd) {
    // Command acknowledgment delays (approximate, based on DuckStation)
    (void)cmd; // Most commands have similar delay
    return 15000; // ~15k cycles (~0.45ms at 33.8MHz)
}
