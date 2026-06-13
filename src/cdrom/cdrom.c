/*
 * CDROM Controller core: init, register I/O, interrupt delivery, event callbacks.
 * Command handlers live in cdrom_commands.c.
 * Disc/async in cdrom_disc.c. Audio in cdrom_audio.c.
 */

#include "cdrom.h"
#include "interconnect.h"
#include "log.h"
#include <string.h>
#include <stdlib.h>

/* =========================================================================
 * Helper functions (called from cdrom_commands.c via extern declarations)
 * ========================================================================= */

uint8_t cdrom_get_stat_byte(Cdrom *cdrom) {
    uint8_t s = 0;
    if (cdrom->motor_on)                           s |= STAT_BYTE_MOTOR_ON;
    if (cdrom->shell_open)                         s |= STAT_BYTE_SHELL_OPEN;
    if (cdrom->drive_state == DRIVE_READING)       s |= STAT_BYTE_READING;
    if (cdrom->drive_state == DRIVE_SEEKING)       s |= STAT_BYTE_SEEKING;
    if (cdrom->drive_state == DRIVE_PLAYING)       s |= STAT_BYTE_PLAYING;
    return s;
}

void cdrom_push_response(Cdrom *cdrom, uint8_t v) {
    fifo_push(&cdrom->response_fifo, v);
}

uint8_t cdrom_pop_param(Cdrom *cdrom) {
    return fifo_pop(&cdrom->param_fifo);
}

void cdrom_send_ack(Cdrom *cdrom) {
    cdrom->interrupt_flag = CDROM_INT_ACK;
    LOG_CDROM_DEBUG("[CDROM] INT3 ACK");
    if (cdrom->inter) interconnect_trigger_cdrom_irq(cdrom->inter);
}

void cdrom_send_complete(Cdrom *cdrom) {
    cdrom->interrupt_flag = CDROM_INT_COMPLETE;
    LOG_CDROM_DEBUG("[CDROM] INT2 Complete");
    if (cdrom->inter) interconnect_trigger_cdrom_irq(cdrom->inter);
}

void cdrom_send_error(Cdrom *cdrom, uint8_t err, uint8_t reason) {
    fifo_clear(&cdrom->response_fifo);
    cdrom_push_response(cdrom, err);
    cdrom_push_response(cdrom, reason);
    cdrom->interrupt_flag = CDROM_INT_ERROR;
    LOG_CDROM_DEBUG("[CDROM] INT5 Error err=0x%02X reason=0x%02X", err, reason);
    if (cdrom->inter) interconnect_trigger_cdrom_irq(cdrom->inter);
}

/* =========================================================================
 * Event Scheduling
 * ========================================================================= */

static void command_event_callback(void *ctx, uint32_t cycles_late);
static void drive_event_callback(void *ctx, uint32_t cycles_late);
static void second_response_callback(void *ctx, uint32_t cycles_late);

void cdrom_schedule_command_event(Cdrom *cdrom, uint32_t cycles) {
    if (cdrom->cmd_event_pending) return;
    cdrom->cmd_event_pending = true;
    if (cdrom->inter)
        interconnect_schedule_event(cdrom->inter, cycles,
                                    command_event_callback, cdrom, "CDROM_CMD");
}

void cdrom_schedule_drive_event(Cdrom *cdrom, uint32_t cycles) {
    if (cdrom->inter)
        interconnect_schedule_event(cdrom->inter, cycles,
                                    drive_event_callback, cdrom, "CDROM_DRIVE");
}

void cdrom_schedule_second_response_event(Cdrom *cdrom, uint32_t cycles) {
    if (cdrom->second_event_pending) return;
    cdrom->second_event_pending = true;
    if (cdrom->inter)
        interconnect_schedule_event(cdrom->inter, cycles,
                                    second_response_callback, cdrom, "CDROM_INT2");
}

static void command_event_callback(void *ctx, uint32_t cycles_late) {
    (void)cycles_late;
    Cdrom *cdrom = (Cdrom *)ctx;
    cdrom->cmd_event_pending = false;
    if (cdrom->interrupt_flag != 0) {
        cdrom_schedule_command_event(cdrom, CDROM_MIN_INT_DELAY);
        return;
    }
    if (cdrom->pending_command == CDC_NONE) return;
    cdrom_execute_command(cdrom);
}

static void drive_event_callback(void *ctx, uint32_t cycles_late) {
    (void)cycles_late;
    Cdrom *cdrom = (Cdrom *)ctx;
    if (cdrom->interrupt_flag != 0) {
        /* Drive event blocked by pending INT — retry after a short delay.
           For CDDA there is no INT1, so don't retry there. */
        if (cdrom->drive_state != DRIVE_PLAYING)
            return;  /* INT ACK handler reschedules for reading */
        /* For CDDA, try again shortly */
        cdrom_schedule_drive_event(cdrom, CDROM_MIN_INT_DELAY);
        return;
    }
    cdrom_execute_drive(cdrom);
}

static void second_response_callback(void *ctx, uint32_t cycles_late) {
    (void)cycles_late;
    Cdrom *cdrom = (Cdrom *)ctx;
    cdrom->second_event_pending = false;
    if (cdrom->interrupt_flag != 0) {
        cdrom_schedule_second_response_event(cdrom, CDROM_MIN_INT_DELAY);
        return;
    }
    cdrom_execute_second_response(cdrom);
}

/* =========================================================================
 * Init / Reset
 * ========================================================================= */

void cdrom_init(Cdrom *cdrom, struct Interconnect *inter) {
    memset(cdrom, 0, sizeof(*cdrom));
    cdrom->inter            = inter;
    cdrom->pending_command  = CDC_NONE;
    cdrom->current_command  = CDC_NONE;
    cdrom->second_response_cmd = CDC_NONE;
    cdrom->drive_state      = DRIVE_IDLE;
    cdrom->vol_ll = cdrom->vol_rr = 0x80;
    fifo_init(&cdrom->param_fifo);
    fifo_init(&cdrom->response_fifo);
    cdrom_audio_init(&cdrom->audio_fifo, &cdrom->xa_adpcm_state);
}

void cdrom_reset(Cdrom *cdrom) {
    bool disc_present = cdrom->disc_present;

    /* Reset only command/drive state — disc, async reader, audio unchanged */
    cdrom->index               = 0;
    cdrom->interrupt_enable    = 0;
    cdrom->interrupt_flag      = 0;
    cdrom->pending_command     = CDC_NONE;
    cdrom->current_command     = CDC_NONE;
    cdrom->second_response_cmd = CDC_NONE;
    cdrom->pending_param_count = 0;
    cdrom->second_response_size = 0;
    cdrom->drive_state         = DRIVE_IDLE;
    cdrom->disc_present        = disc_present;
    cdrom->motor_on            = disc_present;
    cdrom->shell_open          = false;
    cdrom->read_after_seek     = false;
    cdrom->play_after_seek     = false;
    cdrom->current_lba         = 0;
    cdrom->target_lba          = 0;
    cdrom->setloc_lba          = 0;
    cdrom->setloc_pending      = false;
    cdrom->current_subq_lba    = 0;
    cdrom->cdda_speed          = 1;
    cdrom->mode                = 0;
    cdrom->double_speed        = false;
    cdrom->xa_adpcm_enable     = false;
    cdrom->whole_sector        = false;
    cdrom->xa_filter_enable    = false;
    cdrom->xa_filter_file      = 0;
    cdrom->xa_filter_channel   = 0;
    cdrom->report_enable       = false;
    cdrom->auto_pause          = false;
    cdrom->cdda_enable         = false;
    cdrom->muted               = false;
    cdrom->data_buffer_armed   = false;
    cdrom->current_read_buffer  = 0;
    cdrom->current_write_buffer = 0;
    cdrom->vol_ll = cdrom->vol_rr = 0x80;
    cdrom->vol_lr = cdrom->vol_rl = 0;
    cdrom->vol_ll_t = cdrom->vol_rr_t = 0x80;
    cdrom->vol_lr_t = cdrom->vol_rl_t = 0;
    memset(cdrom->sector_buffers, 0, sizeof(cdrom->sector_buffers));
    memset(&cdrom->last_subq, 0, sizeof(cdrom->last_subq));

    fifo_init(&cdrom->param_fifo);
    fifo_init(&cdrom->response_fifo);
    cdrom_audio_init(&cdrom->audio_fifo, &cdrom->xa_adpcm_state);
}

/* =========================================================================
 * Disc Management
 * ========================================================================= */

bool cdrom_load_disc(Cdrom *cdrom, const char *cue_path) {
    if (cdrom->disc_present) cdrom_disc_unload(&cdrom->disc);

    if (!cdrom_disc_load(&cdrom->disc, cue_path)) return false;

    cdrom->disc_present = true;
    cdrom->shell_open   = false;
    cdrom->motor_on     = true;

    /* Seed SubQ to track 1 start */
    cdrom->last_subq = cdrom_disc_get_subq(&cdrom->disc, 0);

    /* Start async reader */
    cdrom_async_reader_init(&cdrom->async_reader, &cdrom->disc);

    LOG_CDROM_INFO("[CDROM] Disc loaded: %u tracks, %u sectors",
                   cdrom->disc.last_track, cdrom->disc.total_sectors);
    return true;
}

void cdrom_eject_disc(Cdrom *cdrom) {
    if (cdrom->async_reader.thread) cdrom_async_reader_shutdown(&cdrom->async_reader);
    cdrom_disc_unload(&cdrom->disc);
    cdrom->disc_present = false;
    cdrom->motor_on     = false;
    cdrom->shell_open   = true;
}

/* =========================================================================
 * Register Access
 * ========================================================================= */

uint8_t cdrom_read8(Cdrom *cdrom, uint32_t addr) {
    uint32_t offset = addr & 0x3;
    switch (offset) {

    case 0: {
        /* Status register */
        SectorBuffer *sb = &cdrom->sector_buffers[cdrom->current_read_buffer];
        uint8_t st = cdrom->index & STAT_INDEX_MASK;
        if (fifo_is_empty(&cdrom->param_fifo))    st |= STAT_PRMEMPT;
        if (!fifo_is_full(&cdrom->param_fifo))    st |= STAT_PRMWRDY;
        if (!fifo_is_empty(&cdrom->response_fifo)) st |= STAT_RSLRRDY;
        if (cdrom->data_buffer_armed && sb->valid && sb->position < sb->data_size)
            st |= STAT_DRQSTS;
        if (cdrom->pending_command != CDC_NONE)   st |= STAT_BUSYSTS;
        return st;
    }

    case 1:
        return fifo_pop(&cdrom->response_fifo);

    case 2: {
        SectorBuffer *sb = &cdrom->sector_buffers[cdrom->current_read_buffer];
        if (cdrom->data_buffer_armed && sb->valid && sb->position < sb->data_size)
            return sb->raw[sb->data_start + sb->position++];
        return 0;
    }

    case 3:
        if (cdrom->index == 0 || cdrom->index == 2)
            return cdrom->interrupt_enable | 0xE0;
        else
            return cdrom->interrupt_flag | 0xE0;

    default:
        return 0;
    }
}

void cdrom_write8(Cdrom *cdrom, uint32_t addr, uint8_t value) {
    uint32_t offset = addr & 0x3;
    switch (offset) {

    case 0:
        cdrom->index = value & 0x3;
        break;

    case 1:
        switch (cdrom->index) {
        case 0: {
            /* Command register */
            if (cdrom->pending_command != CDC_NONE) {
                LOG_CDROM_WARN("[CDROM] Cmd 0x%02X dropped (busy)", value);
                return;
            }
            cdrom->pending_command       = (CdromCommand)value;
            cdrom->pending_param_count   = cdrom->param_fifo.count;
            for (int i = 0; i < cdrom->pending_param_count; i++)
                cdrom->pending_params[i] = fifo_peek(&cdrom->param_fifo, (uint8_t)i);
            cdrom_schedule_command_event(cdrom, CDROM_ACK_DELAY);
            break;
        }
        case 1: break; /* sound map data */
        case 2: break; /* sound map coding */
        case 3: break; /* R→R SPU volume */
        }
        break;

    case 2:
        switch (cdrom->index) {
        case 0: fifo_push(&cdrom->param_fifo, value); break;
        case 1: cdrom->interrupt_enable = value & 0x1F; break;
        case 2: cdrom->vol_ll_t = value; break; /* L←CDL temp */
        case 3: cdrom->vol_rl_t = value; break; /* R←CDL temp */
        }
        break;

    case 3:
        switch (cdrom->index) {
        case 0:
            /* Request register */
            if (value & 0x80) {
                /* Arm data buffer: reset read pointer */
                cdrom->data_buffer_armed = true;
                cdrom->sector_buffers[cdrom->current_read_buffer].position = 0;
            } else {
                /* Disarm / clear buffer */
                cdrom->data_buffer_armed = false;
                cdrom->sector_buffers[cdrom->current_read_buffer].valid = false;
            }
            break;

        case 1: {
            /* Interrupt acknowledge */
            uint8_t ack = value & 0x1F;
            cdrom->interrupt_flag &= ~ack;
            if (value & 0x40) fifo_clear(&cdrom->param_fifo);

            LOG_CDROM_DEBUG("[CDROM] INT ACK 0x%02X remaining=%d", ack, cdrom->interrupt_flag);

            if (cdrom->interrupt_flag == 0) {
                /* Second response pending */
                if (cdrom->second_response_cmd != CDC_NONE)
                    cdrom_schedule_second_response_event(cdrom, CDROM_MIN_INT_DELAY);

                /* Reading: schedule next sector delivery */
                if (cdrom->drive_state == DRIVE_READING)
                    cdrom_schedule_drive_event(cdrom,
                        cdrom->double_speed ? CDROM_READ_DELAY_2X : CDROM_READ_DELAY_1X);

                /* Unblock a command that arrived while INT was pending */
                if (cdrom->pending_command != CDC_NONE)
                    cdrom_schedule_command_event(cdrom, CDROM_MIN_INT_DELAY);
            }
            break;
        }
        case 2: cdrom->vol_lr_t = value; break; /* L←CDR temp */
        case 3:
            cdrom->vol_rr_t = value; /* R←CDR temp */
            if (value & 0x20) {       /* bit5 = commit all temp → working */
                cdrom->vol_ll = cdrom->vol_ll_t;
                cdrom->vol_lr = cdrom->vol_lr_t;
                cdrom->vol_rl = cdrom->vol_rl_t;
                cdrom->vol_rr = cdrom->vol_rr_t;
                LOG_CDROM_DEBUG("[CDROM] Volume matrix committed: LL=%02x LR=%02x RL=%02x RR=%02x",
                                cdrom->vol_ll, cdrom->vol_lr, cdrom->vol_rl, cdrom->vol_rr);
            }
            break;
        }
        break;
    }
}

/* =========================================================================
 * Status Queries
 * ========================================================================= */

bool cdrom_has_pending_command(Cdrom *cdrom) {
    return cdrom->pending_command != CDC_NONE;
}

bool cdrom_has_pending_interrupt(Cdrom *cdrom) {
    return (cdrom->interrupt_flag & cdrom->interrupt_enable) != 0;
}

/* =========================================================================
 * Audio Frame (called by SPU/SDL)
 * ========================================================================= */

void cdrom_get_audio_frame(Cdrom *cdrom, int16_t *left, int16_t *right) {
    cdrom_audio_get_frame(&cdrom->audio_fifo, left, right);
}

/* =========================================================================
 * DMA Read (channel 3: CDROM → RAM)
 * ========================================================================= */

uint32_t cdrom_dma_read_word(Cdrom *cdrom) {
    if (!cdrom->data_buffer_armed) return 0;

    SectorBuffer *sb = &cdrom->sector_buffers[cdrom->current_read_buffer];
    if (!sb->valid || sb->position + 4 > sb->data_size) return 0;

    uint32_t off = sb->data_start + sb->position;
    uint32_t word = (uint32_t)sb->raw[off]
                  | ((uint32_t)sb->raw[off + 1] << 8)
                  | ((uint32_t)sb->raw[off + 2] << 16)
                  | ((uint32_t)sb->raw[off + 3] << 24);
    sb->position += 4;

    if (sb->position >= sb->data_size) {
        cdrom->data_buffer_armed = false;
        sb->valid = false;
        sb->position = 0;
        LOG_CDROM_DEBUG("[CDROM] DMA sector buffer exhausted lba=%u", sb->lba);
    }
    return word;
}
