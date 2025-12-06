/*
 * PlayStation 1 CDROM Controller
 * Event-driven implementation based on PSX-SPX documentation and duckstation reference
 */

#include "cdrom.h"
#include "interconnect.h"
#include "event_scheduler.h"
#include "log.h"
#include <string.h>
#include <stdlib.h>

// Forward declarations
static void send_ack(Cdrom *cdrom);
static void send_complete(Cdrom *cdrom);
static void send_error(Cdrom *cdrom, uint8_t error_flags, uint8_t reason);
static void push_response(Cdrom *cdrom, uint8_t value);
static uint8_t pop_param(Cdrom *cdrom);
static uint8_t get_stat_byte(Cdrom *cdrom);

// Event callbacks (called by event scheduler)
static void command_event_callback(void *context, uint32_t cycles_late);
static void drive_event_callback(void *context, uint32_t cycles_late);
static void second_response_callback(void *context, uint32_t cycles_late);

// =============================================================================
// Initialization
// =============================================================================

void cdrom_init(Cdrom *cdrom, struct Interconnect *inter) {
    memset(cdrom, 0, sizeof(Cdrom));
    cdrom->inter = inter;
    cdrom->pending_command = CDC_NONE;
    cdrom->current_command = CDC_NONE;
    cdrom->second_response_cmd = CDC_NONE;
    cdrom->drive_state = DRIVE_IDLE;
    fifo_init(&cdrom->param_fifo);
    fifo_init(&cdrom->response_fifo);
}

void cdrom_reset(Cdrom *cdrom) {
    struct Interconnect *inter = cdrom->inter;
    FILE *disc = cdrom->disc_file;
    uint32_t disc_size = cdrom->disc_size_sectors;
    bool disc_present = cdrom->disc_present;
    
    memset(cdrom, 0, sizeof(Cdrom));
    
    cdrom->inter = inter;
    cdrom->disc_file = disc;
    cdrom->disc_size_sectors = disc_size;
    cdrom->disc_present = disc_present;
    
    cdrom->pending_command = CDC_NONE;
    cdrom->current_command = CDC_NONE;
    cdrom->second_response_cmd = CDC_NONE;
    cdrom->drive_state = DRIVE_IDLE;
    cdrom->motor_on = disc_present;
    
    fifo_init(&cdrom->param_fifo);
    fifo_init(&cdrom->response_fifo);
}

// =============================================================================
// Disc Management
// =============================================================================

bool cdrom_load_disc(Cdrom *cdrom, const char *cue_path) {
    // Parse CUE file to find BIN file
    FILE *cue = fopen(cue_path, "r");
    if (!cue) {
        LOG_CDROM_ERROR("[CDROM] Failed to open CUE file: %s\n", cue_path);
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
                    strncpy(bin_filename, quote1 + 1, len);
                    bin_filename[len] = '\0';
                    break;
                }
            }
        }
    }
    fclose(cue);
    
    if (bin_filename[0] == '\0') {
        LOG_CDROM_ERROR("[CDROM] Could not find BIN file in CUE\n");
        return false;
    }
    
    // Build path to BIN file (same directory as CUE)
    char bin_path[512];
    const char *last_slash = strrchr(cue_path, '/');
    if (last_slash) {
        size_t dir_len = last_slash - cue_path + 1;
        strncpy(bin_path, cue_path, dir_len);
        bin_path[dir_len] = '\0';
        strcat(bin_path, bin_filename);
    } else {
        strcpy(bin_path, bin_filename);
    }
    
    // Open BIN file
    FILE *bin = fopen(bin_path, "rb");
    if (!bin) {
        LOG_CDROM_ERROR("[CDROM] Failed to open BIN file: %s\n", bin_path);
        return false;
    }
    
    // Get file size and calculate sectors
    fseek(bin, 0, SEEK_END);
    long file_size = ftell(bin);
    fseek(bin, 0, SEEK_SET);
    
    cdrom->disc_file = bin;
    cdrom->disc_size_sectors = file_size / CDROM_SECTOR_SIZE;
    cdrom->disc_present = true;
    cdrom->motor_on = true;
    
    // Simple TOC for single-track data disc
    cdrom->first_track = 1;
    cdrom->last_track = 1;
    cdrom->track_lba[1] = 0;  // Track 1 starts at LBA 0
    
    LOG_CDROM_INFO("[CDROM] Loaded disc: %s (%ld bytes, %u sectors)\n", 
             bin_path, file_size, cdrom->disc_size_sectors);
    
    return true;
}

void cdrom_eject_disc(Cdrom *cdrom) {
    if (cdrom->disc_file) {
        fclose(cdrom->disc_file);
        cdrom->disc_file = NULL;
    }
    cdrom->disc_present = false;
    cdrom->motor_on = false;
    cdrom->shell_open = true;
}

// =============================================================================
// Event Scheduling (via interconnect/event_scheduler)
// =============================================================================

static void schedule_command_event(Cdrom *cdrom, uint32_t cycles) {
    if (cdrom->inter) {
        interconnect_schedule_event(cdrom->inter, cycles, 
                                    command_event_callback, cdrom, "CDROM_CMD");
    }
}

static void schedule_drive_event(Cdrom *cdrom, uint32_t cycles) {
    if (cdrom->inter) {
        interconnect_schedule_event(cdrom->inter, cycles,
                                    drive_event_callback, cdrom, "CDROM_DRIVE");
    }
}

static void schedule_second_response_event(Cdrom *cdrom, uint32_t cycles) {
    if (cdrom->inter) {
        interconnect_schedule_event(cdrom->inter, cycles,
                                    second_response_callback, cdrom, "CDROM_INT2");
    }
}

// =============================================================================
// Register Access
// =============================================================================

uint8_t cdrom_read8(Cdrom *cdrom, uint32_t addr) {
    uint32_t offset = addr & 0x3;
    
    switch (offset) {
        case 0: {
            // Status register (0x1F801800)
            uint8_t status = cdrom->index & STAT_INDEX_MASK;
            
            if (fifo_is_empty(&cdrom->param_fifo))
                status |= STAT_PRMEMPT;
            if (!fifo_is_full(&cdrom->param_fifo))
                status |= STAT_PRMWRDY;
            if (!fifo_is_empty(&cdrom->response_fifo))
                status |= STAT_RSLRRDY;
            if (cdrom->data_buffer_valid && cdrom->data_buffer_index < cdrom->data_buffer_size)
                status |= STAT_DRQSTS;
            if (cdrom->pending_command != CDC_NONE)
                status |= STAT_BUSYSTS;
            
            return status;
        }
        
        case 1: // Response FIFO (all indices)
            return fifo_pop(&cdrom->response_fifo);
        
        case 2: // Data FIFO (all indices)
            if (cdrom->data_buffer_valid && cdrom->data_buffer_index < cdrom->data_buffer_size) {
                return cdrom->data_buffer[cdrom->data_buffer_index++];
            }
            return 0;
        
        case 3:
            if (cdrom->index == 0 || cdrom->index == 2) {
                // Interrupt enable register
                return cdrom->interrupt_enable | 0xE0;
            } else {
                // Interrupt flag register
                return cdrom->interrupt_flag | 0xE0;
            }
        
        default:
            return 0;
    }
}

void cdrom_write8(Cdrom *cdrom, uint32_t addr, uint8_t value) {
    uint32_t offset = addr & 0x3;
    
    switch (offset) {
        case 0: // Index register
            cdrom->index = value & 0x3;
            break;
        
        case 1:
            switch (cdrom->index) {
                case 0: {
                    // Command register (0x1F801801.Index0)
                    // Block commands if busy or interrupt pending
                    if (cdrom->pending_command != CDC_NONE) {
                        LOG_CDROM_WARN("[CDROM] Command 0x%02X ignored - BUSY\n", value);
                        return;
                    }
                    if (cdrom->interrupt_flag != 0) {
                        LOG_CDROM_WARN("[CDROM] Command 0x%02X ignored - INT pending (%d)\n", 
                                value, cdrom->interrupt_flag);
                        return;
                    }
                    
                    LOG_CDROM_DEBUG("[CDROM] Cmd 0x%02X", value);
                    
                    // Save parameters for command execution
                    cdrom->pending_command = (CdromCommand)value;
                    cdrom->pending_param_count = cdrom->param_fifo.count;
                    for (int i = 0; i < cdrom->pending_param_count; i++) {
                        cdrom->pending_params[i] = fifo_peek(&cdrom->param_fifo, i);
                    }
                    
                    // Schedule command execution
                    schedule_command_event(cdrom, CDROM_ACK_DELAY);
                    break;
                }
                case 1: // Sound map data out
                    break;
                case 2: // Sound map coding info
                    break;
                case 3: // Right-CD to Right-SPU volume
                    break;
            }
            break;
        
        case 2:
            switch (cdrom->index) {
                case 0: // Parameter FIFO
                    fifo_push(&cdrom->param_fifo, value);
                    break;
                case 1: // Interrupt enable
                    cdrom->interrupt_enable = value & 0x1F;
                    break;
                case 2: // Left-CD to Left-SPU volume
                    break;
                case 3: // Right-CD to Left-SPU volume
                    break;
            }
            break;
        
        case 3:
            switch (cdrom->index) {
                case 0:
                    // Request register
                    if (value & 0x80) {
                        // Want data - start transferring sector to data FIFO
                        if (cdrom->data_buffer_valid) {
                            cdrom->data_buffer_index = 0;
                        }
                    } else {
                        // Clear data buffer
                        cdrom->data_buffer_valid = false;
                        cdrom->data_buffer_index = 0;
                        cdrom->data_buffer_size = 0;
                    }
                    break;
                case 1: {
                    // Interrupt acknowledge
                    uint8_t ack = value & 0x1F;
                    cdrom->interrupt_flag &= ~ack;
                    
                    if (value & 0x40) {
                        // Clear parameter FIFO
                        fifo_clear(&cdrom->param_fifo);
                    }
                    
                    LOG_CDROM_DEBUG("[CDROM] INT ACK: 0x%02X, remaining: %d\n", ack, cdrom->interrupt_flag);
                    
                    // If we have a second response pending and INT is now clear, schedule it
                    if (cdrom->second_response_cmd != CDC_NONE && cdrom->interrupt_flag == 0) {
                        schedule_second_response_event(cdrom, CDROM_READ_DELAY);
                    }
                    
                    // If we're reading and INT cleared, schedule next sector
                    if (cdrom->drive_state == DRIVE_READING && cdrom->interrupt_flag == 0) {
                        schedule_drive_event(cdrom, CDROM_READ_DELAY);
                    }
                    break;
                }
                case 2: // Left-CD to Right-SPU volume
                    break;
                case 3: // Apply volume changes
                    break;
            }
            break;
    }
}

// =============================================================================
// Command Execution
// =============================================================================

static void command_event_callback(void *context, uint32_t cycles_late) {
    Cdrom *cdrom = (Cdrom *)context;
    
    LOG_CDROM_DEBUG("[CDROM] command_event_callback: pending_cmd=0x%02X, int_flag=%d\n",
                    cdrom->pending_command, cdrom->interrupt_flag);
    
    // If interrupt still pending, reschedule
    if (cdrom->interrupt_flag != 0) {
        LOG_CDROM_DEBUG("[CDROM] Command blocked by INT, rescheduling\n");
        schedule_command_event(cdrom, CDROM_ACK_DELAY);
        return;
    }
    
    cdrom_execute_command(cdrom);
}

void cdrom_execute_command(Cdrom *cdrom) {
    CdromCommand cmd = cdrom->pending_command;
    cdrom->pending_command = CDC_NONE;
    cdrom->current_command = cmd;
    
    // Restore parameters
    fifo_clear(&cdrom->param_fifo);
    for (int i = 0; i < cdrom->pending_param_count; i++) {
        fifo_push(&cdrom->param_fifo, cdrom->pending_params[i]);
    }
    
    LOG_CDROM_DEBUG("[CDROM] Exec cmd 0x%02X", cmd);
    fifo_clear(&cdrom->response_fifo);
    
    switch (cmd) {
        case CDC_GETSTAT:
            push_response(cdrom, get_stat_byte(cdrom));
            send_ack(cdrom);
            break;
        
        case CDC_SETLOC: {
            uint8_t mm = pop_param(cdrom);
            uint8_t ss = pop_param(cdrom);
            uint8_t ff = pop_param(cdrom);
            
            // BCD to binary
            uint8_t min = ((mm >> 4) * 10) + (mm & 0x0F);
            uint8_t sec = ((ss >> 4) * 10) + (ss & 0x0F);
            uint8_t frame = ((ff >> 4) * 10) + (ff & 0x0F);
            
            // MSF to LBA (subtract 2 second lead-in = 150 frames)
            cdrom->setloc_lba = ((min * 60 + sec) * 75 + frame) - 150;
            cdrom->setloc_pending = true;
            
            LOG_CDROM_DEBUG("[CDROM] SetLoc: %02d:%02d:%02d -> LBA %u\n", 
                     min, sec, frame, cdrom->setloc_lba);
            
            push_response(cdrom, get_stat_byte(cdrom));
            send_ack(cdrom);
            break;
        }
        
        case CDC_READN:
        case CDC_READS: {
            if (cdrom->setloc_pending) {
                cdrom->current_lba = cdrom->setloc_lba;
                cdrom->setloc_pending = false;
            }
            
            cdrom->drive_state = DRIVE_READING;
            push_response(cdrom, get_stat_byte(cdrom));
            send_ack(cdrom);
            
            // Schedule first sector read
            schedule_drive_event(cdrom, CDROM_READ_DELAY);
            break;
        }
        
        case CDC_STOP:
            cdrom->drive_state = DRIVE_STOPPING;
            push_response(cdrom, get_stat_byte(cdrom));
            send_ack(cdrom);
            cdrom->second_response_cmd = CDC_STOP;
            break;
        
        case CDC_PAUSE:
            if (cdrom->drive_state == DRIVE_READING) {
                cdrom->drive_state = DRIVE_PAUSING;
            }
            push_response(cdrom, get_stat_byte(cdrom));
            send_ack(cdrom);
            cdrom->second_response_cmd = CDC_PAUSE;
            break;
        
        case CDC_INIT:
            push_response(cdrom, get_stat_byte(cdrom));
            send_ack(cdrom);
            cdrom->second_response_cmd = CDC_INIT;
            break;
        
        case CDC_MUTE:
            cdrom->muted = true;
            push_response(cdrom, get_stat_byte(cdrom));
            send_ack(cdrom);
            break;
        
        case CDC_DEMUTE:
            cdrom->muted = false;
            push_response(cdrom, get_stat_byte(cdrom));
            send_ack(cdrom);
            break;
        
        case CDC_SETFILTER:
            cdrom->xa_filter_file = pop_param(cdrom);
            cdrom->xa_filter_channel = pop_param(cdrom);
            push_response(cdrom, get_stat_byte(cdrom));
            send_ack(cdrom);
            break;
        
        case CDC_SETMODE: {
            uint8_t mode = pop_param(cdrom);
            cdrom->mode = mode;
            cdrom->double_speed = (mode & 0x80) != 0;
            cdrom->xa_adpcm_enable = (mode & 0x40) != 0;
            cdrom->whole_sector = (mode & 0x20) != 0;
            cdrom->xa_filter_enable = (mode & 0x08) != 0;
            cdrom->report_enable = (mode & 0x04) != 0;
            cdrom->auto_pause = (mode & 0x02) != 0;
            cdrom->cdda_enable = (mode & 0x01) != 0;
            
            LOG_CDROM_DEBUG("[CDROM] SetMode: 0x%02X (2x=%d, whole=%d)\n",
                     mode, cdrom->double_speed, cdrom->whole_sector);
            
            push_response(cdrom, get_stat_byte(cdrom));
            send_ack(cdrom);
            break;
        }
        
        case CDC_GETLOCP:
            push_response(cdrom, 0x00);  // Track
            push_response(cdrom, 0x01);  // Index
            push_response(cdrom, 0x00);  // Relative MM
            push_response(cdrom, 0x02);  // Relative SS
            push_response(cdrom, 0x00);  // Relative FF
            push_response(cdrom, 0x00);  // Absolute MM
            push_response(cdrom, 0x02);  // Absolute SS
            push_response(cdrom, 0x00);  // Absolute FF
            send_ack(cdrom);
            break;
        
        case CDC_GETTN:
            push_response(cdrom, get_stat_byte(cdrom));
            push_response(cdrom, 0x01);  // First track (BCD)
            push_response(cdrom, 0x01);  // Last track (BCD)
            send_ack(cdrom);
            break;
        
        case CDC_GETTD: {
            uint8_t track = pop_param(cdrom);
            push_response(cdrom, get_stat_byte(cdrom));
            
            if (track == 0) {
                // Track 0 = lead-out (disc length)
                uint32_t lba = cdrom->disc_size_sectors + 150;
                uint8_t min = (lba / 75) / 60;
                uint8_t sec = (lba / 75) % 60;
                push_response(cdrom, ((min / 10) << 4) | (min % 10));
                push_response(cdrom, ((sec / 10) << 4) | (sec % 10));
            } else {
                // Track 1 starts at 00:02:00
                push_response(cdrom, 0x00);
                push_response(cdrom, 0x02);
            }
            send_ack(cdrom);
            break;
        }
        
        case CDC_SEEKL:
        case CDC_SEEKP:
            if (cdrom->setloc_pending) {
                cdrom->target_lba = cdrom->setloc_lba;
                cdrom->setloc_pending = false;
            }
            cdrom->drive_state = DRIVE_SEEKING;
            push_response(cdrom, get_stat_byte(cdrom));
            send_ack(cdrom);
            cdrom->second_response_cmd = cmd;
            break;
        
        case CDC_TEST: {
            uint8_t subfunction = pop_param(cdrom);
            LOG_CDROM_DEBUG("[CDROM] Test(0x%02X)", subfunction);
            
            switch (subfunction) {
                case 0x20:
                    // CDROM BIOS date/version (yy,mm,dd,ver)
                    // SCPH1001 uses: 94/09/19, version C0
                    LOG_CDROM_DEBUG("[CDROM] Test(0x20): Version 94/09/19 vC0");
                    push_response(cdrom, 0x94);  // Year (BCD)
                    push_response(cdrom, 0x09);  // Month (BCD)
                    push_response(cdrom, 0x19);  // Day (BCD)
                    push_response(cdrom, 0xC0);  // Version
                    send_ack(cdrom);
                    break;
                
                case 0x04:
                    // Reset SCEx counters
                    LOG_CDROM_DEBUG("[CDROM] Test(0x04): Reset SCEx");
                    cdrom->motor_on = true;  // Force motor on per duckstation
                    push_response(cdrom, get_stat_byte(cdrom));
                    send_ack(cdrom);
                    break;
                
                case 0x05:
                    // Get SCEx counters - returns stat, total_reads, success_count
                    LOG_CDROM_DEBUG("[CDROM] Test(0x05): SCEx counters");
                    push_response(cdrom, get_stat_byte(cdrom));  // stat byte first!
                    push_response(cdrom, 0x00);  // TOC reads
                    push_response(cdrom, 0x00);  // SCEx strings received
                    send_ack(cdrom);
                    break;
                
                case 0x22:
                    // Get region string - "for U/C" for US region
                    LOG_CDROM_DEBUG("[CDROM] Test(0x22): Region U/C");
                    push_response(cdrom, 'f');
                    push_response(cdrom, 'o');
                    push_response(cdrom, 'r');
                    push_response(cdrom, ' ');
                    push_response(cdrom, 'U');
                    push_response(cdrom, '/');
                    push_response(cdrom, 'C');
                    send_ack(cdrom);
                    break;
                
                default:
                    LOG_CDROM_WARN("[CDROM] Unknown test: 0x%02X\n", subfunction);
                    push_response(cdrom, get_stat_byte(cdrom));
                    send_ack(cdrom);
                    break;
            }
            break;
        }
        
        case CDC_GETID:
            push_response(cdrom, get_stat_byte(cdrom));
            send_ack(cdrom);
            cdrom->second_response_cmd = CDC_GETID;
            break;
        
        case CDC_MOTORON:
            cdrom->motor_on = true;
            push_response(cdrom, get_stat_byte(cdrom));
            send_ack(cdrom);
            cdrom->second_response_cmd = CDC_MOTORON;
            break;
        
        default:
            LOG_CDROM_WARN("[CDROM] Unknown command: 0x%02X\n", cmd);
            send_error(cdrom, ERROR_INVALID_COMMAND, 0x40);
            break;
    }
    
    fifo_clear(&cdrom->param_fifo);
    cdrom->current_command = CDC_NONE;
}

// =============================================================================
// Second Response Handling
// =============================================================================

static void second_response_callback(void *context, uint32_t cycles_late) {
    Cdrom *cdrom = (Cdrom *)context;
    
    if (cdrom->interrupt_flag != 0) {
        // Reschedule if INT still pending
        schedule_second_response_event(cdrom, CDROM_READ_DELAY);
        return;
    }
    
    cdrom_execute_second_response(cdrom);
}

void cdrom_execute_second_response(Cdrom *cdrom) {
    CdromCommand cmd = cdrom->second_response_cmd;
    cdrom->second_response_cmd = CDC_NONE;
    
    fifo_clear(&cdrom->response_fifo);
    
    switch (cmd) {
        case CDC_GETID:
            if (!cdrom->disc_present) {
                // No disc
                push_response(cdrom, 0x08);  // Error + shell open
                push_response(cdrom, 0x40);
                send_error(cdrom, 0x08, 0x40);
            } else {
                // Licensed disc
                push_response(cdrom, get_stat_byte(cdrom));
                push_response(cdrom, 0x00);  // Flags: licensed
                push_response(cdrom, 0x20);  // Type: mode 2
                push_response(cdrom, 0x00);  // Attr
                push_response(cdrom, 'S');   // Region: SCEI
                push_response(cdrom, 'C');
                push_response(cdrom, 'E');
                push_response(cdrom, 'A');   // America
                send_complete(cdrom);
            }
            break;
        
        case CDC_INIT:
            cdrom->motor_on = cdrom->disc_present;
            cdrom->drive_state = DRIVE_IDLE;
            cdrom->mode = 0;
            push_response(cdrom, get_stat_byte(cdrom));
            send_complete(cdrom);
            break;
        
        case CDC_STOP:
            cdrom->motor_on = false;
            cdrom->drive_state = DRIVE_IDLE;
            push_response(cdrom, get_stat_byte(cdrom));
            send_complete(cdrom);
            break;
        
        case CDC_PAUSE:
            cdrom->drive_state = DRIVE_IDLE;
            push_response(cdrom, get_stat_byte(cdrom));
            send_complete(cdrom);
            break;
        
        case CDC_SEEKL:
        case CDC_SEEKP:
            cdrom->current_lba = cdrom->target_lba;
            cdrom->drive_state = DRIVE_IDLE;
            push_response(cdrom, get_stat_byte(cdrom));
            send_complete(cdrom);
            break;
        
        case CDC_MOTORON:
            push_response(cdrom, get_stat_byte(cdrom));
            send_complete(cdrom);
            break;
        
        default:
            break;
    }
}

// =============================================================================
// Drive Event (Sector Reading)
// =============================================================================

static void drive_event_callback(void *context, uint32_t cycles_late) {
    Cdrom *cdrom = (Cdrom *)context;
    
    if (cdrom->interrupt_flag != 0) {
        // Wait for INT to be acknowledged
        return;
    }
    
    cdrom_execute_drive(cdrom);
}

void cdrom_execute_drive(Cdrom *cdrom) {
    if (cdrom->drive_state != DRIVE_READING) {
        return;
    }
    
    // Read sector from disc
    if (!cdrom->disc_file) {
        LOG_CDROM_ERROR("[CDROM] No disc file!\n");
        return;
    }
    
    long offset = (long)cdrom->current_lba * CDROM_SECTOR_SIZE;
    
    if (fseek(cdrom->disc_file, offset, SEEK_SET) != 0) {
        LOG_CDROM_ERROR("[CDROM] Seek failed to LBA %u\n", cdrom->current_lba);
        return;
    }
    
    uint8_t sector[CDROM_SECTOR_SIZE];
    if (fread(sector, 1, CDROM_SECTOR_SIZE, cdrom->disc_file) != CDROM_SECTOR_SIZE) {
        LOG_CDROM_ERROR("[CDROM] Read failed at LBA %u\n", cdrom->current_lba);
        return;
    }
    
    // Extract data portion (skip sync + header for data sectors)
    // Mode 2 Form 1: 12 sync + 4 header + 8 subheader + 2048 data + 4 EDC + 276 ECC
    // Mode 2 Form 2: 12 sync + 4 header + 8 subheader + 2324 data + 4 EDC
    if (cdrom->whole_sector) {
        // Copy whole sector minus sync (2340 bytes from offset 12)
        memcpy(cdrom->data_buffer, sector + 12, 2340);
        cdrom->data_buffer_size = 2340;
    } else {
        // Data only (2048 bytes from offset 24 for Mode 2 Form 1)
        memcpy(cdrom->data_buffer, sector + 24, 2048);
        cdrom->data_buffer_size = 2048;
    }
    
    cdrom->data_buffer_index = 0;
    cdrom->data_buffer_valid = true;
    
    // Send INT1 (data ready)
    fifo_clear(&cdrom->response_fifo);
    push_response(cdrom, get_stat_byte(cdrom));
    cdrom->interrupt_flag = CDROM_INT_DATA_READY;
    
    LOG_CDROM_DEBUG("[CDROM] Read sector %u, INT1\n", cdrom->current_lba);
    
    // Advance to next sector
    cdrom->current_lba++;
    
    // Trigger IRQ
    if (cdrom->inter) {
        interconnect_trigger_cdrom_irq(cdrom->inter);
    }
}

// =============================================================================
// Async Interrupt Delivery
// =============================================================================

void cdrom_deliver_async_interrupt(Cdrom *cdrom) {
    if (cdrom->async_interrupt != 0) {
        if (cdrom->interrupt_flag == 0) {
            fifo_clear(&cdrom->response_fifo);
            for (int i = 0; i < cdrom->async_response_size; i++) {
                push_response(cdrom, cdrom->async_response[i]);
            }
            cdrom->interrupt_flag = cdrom->async_interrupt;
            cdrom->async_interrupt = 0;
            cdrom->async_response_size = 0;
            
            if (cdrom->inter) {
                interconnect_trigger_cdrom_irq(cdrom->inter);
            }
        }
    }
}

// =============================================================================
// Status Queries
// =============================================================================

bool cdrom_has_pending_command(Cdrom *cdrom) {
    return cdrom->pending_command != CDC_NONE;
}

bool cdrom_has_pending_interrupt(Cdrom *cdrom) {
    return (cdrom->interrupt_flag & cdrom->interrupt_enable) != 0;
}

// =============================================================================
// Helper Functions
// =============================================================================

static void push_response(Cdrom *cdrom, uint8_t value) {
    fifo_push(&cdrom->response_fifo, value);
}

static uint8_t pop_param(Cdrom *cdrom) {
    return fifo_pop(&cdrom->param_fifo);
}

static uint8_t get_stat_byte(Cdrom *cdrom) {
    uint8_t stat = 0;
    
    if (cdrom->motor_on)
        stat |= STAT_BYTE_MOTOR_ON;
    if (cdrom->shell_open || !cdrom->disc_present)
        stat |= STAT_BYTE_SHELL_OPEN;
    if (cdrom->drive_state == DRIVE_READING)
        stat |= STAT_BYTE_READING;
    if (cdrom->drive_state == DRIVE_SEEKING)
        stat |= STAT_BYTE_SEEKING;
    if (cdrom->drive_state == DRIVE_PLAYING)
        stat |= STAT_BYTE_PLAYING;
    
    return stat;
}

static void send_ack(Cdrom *cdrom) {
    cdrom->interrupt_flag = CDROM_INT_ACK;
    LOG_CDROM_DEBUG("[CDROM] INT3 (ACK)");
    
    if (cdrom->inter) {
        interconnect_trigger_cdrom_irq(cdrom->inter);
    } else {
        LOG_CDROM_ERROR("[CDROM] send_ack: inter is NULL!\n");
    }
}

static void send_complete(Cdrom *cdrom) {
    cdrom->interrupt_flag = CDROM_INT_COMPLETE;
    LOG_CDROM_DEBUG("[CDROM] INT2 (Complete)");
    
    if (cdrom->inter) {
        interconnect_trigger_cdrom_irq(cdrom->inter);
    }
}

static void send_error(Cdrom *cdrom, uint8_t error_flags, uint8_t reason) {
    fifo_clear(&cdrom->response_fifo);
    push_response(cdrom, error_flags);
    push_response(cdrom, reason);
    cdrom->interrupt_flag = CDROM_INT_ERROR;
    LOG_CDROM_DEBUG("[CDROM] INT5 (Error)\n");
    
    if (cdrom->inter) {
        interconnect_trigger_cdrom_irq(cdrom->inter);
    }
}
