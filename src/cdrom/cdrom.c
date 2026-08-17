/* SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 ZioZoni95
 *
 * Part of ZoniStation One, a PlayStation 1 emulator.
 * See LICENSE for the full licence text and THIRD-PARTY.md for the
 * components of this project that have other authors.
 */
/*
 * CDROM Controller core: init, register I/O, interrupt delivery, event callbacks.
 * Command handlers live in cdrom_commands.c.
 * Disc/async in cdrom_disc.c. Audio in cdrom_audio.c.
 */

#include "cdrom.h"
#include "interconnect.h"
#include "event_scheduler.h"
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
 *
 * Scheduled on the shared event_scheduler.c queue (EVQ_CDROM_COMMAND/
 * DRIVE/SECOND_RESPONSE), not a private timer array. Tick functions below
 * are called by event_scheduler.c's handler table when those events fire.
 * ========================================================================= */

/* Cycles left until `deadline`, floored so a due-or-late event still goes
 * through the event queue rather than being dispatched inline. */
static uint32_t cdrom_delay_left(const Cdrom *cdrom, uint32_t deadline) {
    if (!cdrom->inter) return CDROM_MIN_INT_DELAY;
    int32_t left = (int32_t)(deadline - cdrom->inter->cpu_cycle_counter);
    return (left > (int32_t)CDROM_MIN_INT_DELAY) ? (uint32_t)left : CDROM_MIN_INT_DELAY;
}

void cdrom_schedule_command_event(Cdrom *cdrom, uint32_t cycles) {
    if (cdrom->cmd_event_pending) return;
    cdrom->cmd_event_pending = true;
    if (cdrom->inter) {
        cdrom->cmd_deadline = cdrom->inter->cpu_cycle_counter + cycles;
        eventq_schedule(cdrom->inter, EVQ_CDROM_COMMAND, cycles);
    }
}

/* Re-arm an event that was due but could not be delivered because an earlier
 * interrupt was still unacknowledged. Keeps the original deadline: what is
 * owed is the time that is left, not a fresh minimum. */
static void cdrom_rearm_command_event(Cdrom *cdrom) {
    cdrom->cmd_event_pending = true;
    if (cdrom->inter)
        eventq_schedule(cdrom->inter, EVQ_CDROM_COMMAND,
                        cdrom_delay_left(cdrom, cdrom->cmd_deadline));
}

static void cdrom_rearm_second_response(Cdrom *cdrom) {
    cdrom->second_event_pending = true;
    if (cdrom->inter)
        eventq_schedule(cdrom->inter, EVQ_CDROM_SECOND_RESPONSE,
                        cdrom_delay_left(cdrom, cdrom->second_deadline));
}

void cdrom_schedule_drive_event(Cdrom *cdrom, uint32_t cycles) {
    if (cdrom->inter) {
        cdrom->drive_deadline = cdrom->inter->cpu_cycle_counter + cycles;
        eventq_schedule(cdrom->inter, EVQ_CDROM_DRIVE, cycles);
    }
}

/* Re-arm the drive on what is left of its own deadline — see drive_deadline. */
static void cdrom_rearm_drive_event(Cdrom *cdrom) {
    if (cdrom->inter)
        eventq_schedule(cdrom->inter, EVQ_CDROM_DRIVE,
                        cdrom_delay_left(cdrom, cdrom->drive_deadline));
}

void cdrom_schedule_second_response_event(Cdrom *cdrom, uint32_t cycles) {
    /* Re-schedule rather than refuse. Dropping the new deadline made a second
     * command's INT2 fire at the first command's time, or not at all: measured
     * on a boot, twelve Init commands produced three second responses and a
     * burst of ten produced one. */
    cdrom->second_event_pending = true;
    if (cdrom->inter) {
        cdrom->second_deadline = cdrom->inter->cpu_cycle_counter + cycles;
        eventq_schedule(cdrom->inter, EVQ_CDROM_SECOND_RESPONSE, cycles);
    }
}

void cdrom_command_event_tick(struct Interconnect *inter) {
    Cdrom *cdrom = &inter->cdrom;
    cdrom->cmd_event_pending = false;
    if (cdrom->interrupt_flag != 0) {
        cdrom_rearm_command_event(cdrom);
        return;
    }
    if (cdrom->pending_command == CDC_NONE) return;
    cdrom_execute_command(cdrom);
}

void cdrom_drive_event_tick(struct Interconnect *inter) {
    Cdrom *cdrom = &inter->cdrom;
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

void cdrom_second_response_event_tick(struct Interconnect *inter) {
    Cdrom *cdrom = &inter->cdrom;
    cdrom->second_event_pending = false;
    if (cdrom->interrupt_flag != 0) {
        cdrom_rearm_second_response(cdrom);
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
    cdrom->cmd_deadline        = 0;
    cdrom->second_deadline     = 0;
    cdrom->last_header_valid   = false;
    cdrom->drive_state         = DRIVE_IDLE;
    cdrom->disc_present        = disc_present;
    cdrom->motor_on            = disc_present;
    cdrom->shell_open          = false;
    cdrom->read_after_seek     = false;
    cdrom->play_after_seek     = false;
    cdrom->seek_phase          = false;
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
    cdrom->xa_mute             = false;
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
    cdrom->disc_region  = cdrom_disc_detect_region(&cdrom->disc);
    LOG_CDROM_INFO("[CDROM] Disc region detected: '%c'", cdrom->disc_region ? cdrom->disc_region : '?');

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
            /* How long until the acknowledge, by command and drive state
             * (cdromdrive.md:1877-1894): Init and ReadTOC do a slow
             * initialisation before answering, and a stopped drive answers
             * sooner than a spinning one because the mainloop is doing less. */
            uint32_t ack = CDROM_ACK_DELAY;
            if (value == CDC_INIT || value == CDC_READTOC) ack = CDROM_ACK_DELAY_INIT;
            else if (!cdrom->motor_on)                     ack = CDROM_ACK_DELAY_STOPPED;
            cdrom_schedule_command_event(cdrom, ack);
            break;
        }
        case 1: break; /* WRDATA — sound map data (not implemented) */
        case 2: break; /* CI — sound map coding info (not implemented) */
        case 3: cdrom->vol_rr_t = value; break;  /* ATV2: R→R (cdromdrive.md:229) */
        }
        break;

    case 2:
        switch (cdrom->index) {
        case 0: fifo_push(&cdrom->param_fifo, value); break;
        case 1: cdrom->interrupt_enable = value & 0x1F; break;
        case 2: cdrom->vol_ll_t = value; break; /* ATV0: L→L (cdromdrive.md:227) */
        case 3: cdrom->vol_lr_t = value; break; /* ATV3: R→L, i.e. L←CDR (:230) */
        }
        break;

    case 3:
        switch (cdrom->index) {
        case 0:
            /* Request register: disarm (BFRD=0) resets the read pointer,
             * arm (BFRD=1) does NOT. This lets software issue several separate
             * reads of the SAME sector (e.g. a short header peek followed by a
             * bulk data transfer) without losing progress across re-arms —
             * armed-only transitions must leave ->position exactly where it
             * was. A fresh sector load (cdrom_execute_drive) already resets
             * ->position=0 on its own, so this only handles the explicit
             * software-driven "restart from the top" case. Previously this
             * was backwards (reset on arm, not on disarm), which silently
             * discarded any header-then-data split-transfer progress the
             * moment software re-armed for the second transfer. */
            if (value & 0x80) {
                cdrom->data_buffer_armed = true;
            } else {
                /* Disarm (BFRD=0): stop offering data via RDDATA/DMA and
                 * reset the read pointer, but do NOT discard the underlying
                 * sector data (->valid) — real hardware doesn't re-fetch
                 * from disc just because software un-sets BFRD; only a
                 * fresh ReadN/sector load or full consumption
                 * (cdrom_dma_read_word) should invalidate the buffer. */
                cdrom->data_buffer_armed = false;
                cdrom->sector_buffers[cdrom->current_read_buffer].position = 0;
            }
            break;

        case 1: {
            /* Interrupt acknowledge */
            uint8_t ack = value & 0x1F;
            cdrom->interrupt_flag &= ~ack;
            if (value & 0x40) fifo_clear(&cdrom->param_fifo);

            LOG_CDROM_DEBUG("[CDROM] INT ACK 0x%02X remaining=%d", ack, cdrom->interrupt_flag);

            if (cdrom->interrupt_flag == 0) {
                /* Second response pending. Re-arm on what is left of its own
                 * deadline: the guest acknowledging an INT3 does not make the
                 * head arrive sooner. */
                if (cdrom->second_response_cmd != CDC_NONE)
                    cdrom_rearm_second_response(cdrom);

                /* Reading: the next sector is due on disc time, which the
                 * delivery of the last one already set. */
                if (cdrom->drive_state == DRIVE_READING)
                    cdrom_rearm_drive_event(cdrom);

                /* Unblock a command that arrived while INT was pending */
                if (cdrom->pending_command != CDC_NONE)
                    cdrom_rearm_command_event(cdrom);
            }
            break;
        }
        case 2: cdrom->vol_rl_t = value; break; /* ATV1: L→R, i.e. R←CDL (:228) */
        case 3:
            /* ADPCTL, not a volume register (cdromdrive.md:249-255): bit0
             * ADPMUTE, bit5 CHNGATV applies the staged ATV0-3 values. This port
             * used to be stored into the R→R volume, so every CHNGATV write
             * (value 20h) also set that gain to 20h — a quarter volume. */
            cdrom->xa_mute = (value & 0x01) != 0;
            if (value & 0x20) {       /* CHNGATV: commit staged → working */
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

static inline int16_t cdrom_sat16(int32_t v) {
    return (int16_t)(v < -32768 ? -32768 : (v > 32767 ? 32767 : v));
}

void cdrom_get_audio_frame(Cdrom *cdrom, int16_t *left, int16_t *right) {
    int16_t l = 0, r = 0;
    cdrom_audio_get_frame(&cdrom->audio_fifo, &l, &r);

    /* Muting forces the output volume to zero — the controller keeps processing
     * audio sectors internally (cdromdrive.md:1018-1022). It used to be applied
     * by not pushing samples at all, which starved the FIFO instead of feeding
     * it silence and left the resampler's history frozen across the mute. */
    if (cdrom->muted || cdrom->xa_mute) { *left = 0; *right = 0; return; }

    /* ATV0-ATV3 volume matrix (cdromdrive.md:227-247): 80h is normal, FFh is
     * double, and the hardware saturates properly up to double volume — which
     * is what clamping the 16-bit sum gives. Nothing read these four registers
     * before, so Spyro's mono option and Resident Evil 2's CD fades (:243-247)
     * had no effect whatsoever. */
    int32_t out_l = ((int32_t)l * (int32_t)cdrom->vol_ll +
                     (int32_t)r * (int32_t)cdrom->vol_lr) >> 7;
    int32_t out_r = ((int32_t)l * (int32_t)cdrom->vol_rl +
                     (int32_t)r * (int32_t)cdrom->vol_rr) >> 7;
    *left  = cdrom_sat16(out_l);
    *right = cdrom_sat16(out_r);
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
