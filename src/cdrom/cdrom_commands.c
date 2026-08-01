/* SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 ZioZoni95
 *
 * Part of ZoniStation One, a PlayStation 1 emulator.
 * See LICENSE for the full licence text and THIRD-PARTY.md for the
 * components of this project that have other authors.
 */
/*
 * CDROM command handlers — first and second responses, drive loop.
 * Called from cdrom.c event callbacks.
 */

#include "cdrom.h"
#include "interconnect.h"
#include "log.h"
#include "lua_debug.h"
#include <string.h>
#include <stdlib.h>

/* =========================================================================
 * Name tables
 * ========================================================================= */

static const char* cdrom_cmd_name(uint8_t cmd) {
    static const char* const names[32] = {
        "Sync","Getstat","Setloc","Play","Forward","Backward","ReadN","MotorOn",
        "Stop","Pause","Init","Mute","Demute","Setfilter","Setmode","Getparam",
        "GetlocL","GetlocP","ReadT","GetTN","GetTD","SeekL","SeekP","SetClock",
        "GetClock","Test","GetID","ReadS","Reset","GetQ","ReadTOC","VideoCD"
    };
    return (cmd < 32) ? names[cmd] : (cmd == 0xFF ? "None" : "Unknown");
}

static const char* cdrom_drive_state_name(int state) {
    switch (state) {
        case 0: return "IDLE";
        case 1: return "SPINUP";
        case 2: return "SEEKING";
        case 3: return "READING";
        case 4: return "PLAYING";
        case 5: return "PAUSING";
        case 6: return "STOPPING";
        default: return "UNKNOWN";
    }
}

/* =========================================================================
 * Internal helpers (shared with cdrom.c via extern)
 * ========================================================================= */

void cdrom_send_ack(Cdrom *cdrom);
void cdrom_send_complete(Cdrom *cdrom);
void cdrom_send_error(Cdrom *cdrom, uint8_t err, uint8_t reason);
void cdrom_push_response(Cdrom *cdrom, uint8_t v);
uint8_t cdrom_pop_param(Cdrom *cdrom);
uint8_t cdrom_get_stat_byte(Cdrom *cdrom);
void cdrom_schedule_command_event(Cdrom *cdrom, uint32_t cycles);
void cdrom_schedule_drive_event(Cdrom *cdrom, uint32_t cycles);
void cdrom_schedule_second_response_event(Cdrom *cdrom, uint32_t cycles);

/* =========================================================================
 * Begin helpers
 * ========================================================================= */

static void begin_reading(Cdrom *cdrom) {
    bool location_changed = false;
    if (cdrom->setloc_pending) {
        location_changed      = (cdrom->setloc_lba != cdrom->current_lba);
        cdrom->current_lba    = cdrom->setloc_lba;
        cdrom->setloc_pending = false;
    }
    LOG_CDROM_DEBUG("[CDROM] Drive state: %s -> READING (LBA %u)",
                    cdrom_drive_state_name(cdrom->drive_state), cdrom->current_lba);
    cdrom->drive_state = DRIVE_READING;
    cdrom_async_reader_queue(&cdrom->async_reader, cdrom->current_lba);
    /* Location changed: head needs extra seek time before first sector */
    uint32_t base = cdrom->double_speed ? CDROM_READ_DELAY_2X : CDROM_READ_DELAY_1X;
    uint32_t delay = location_changed ? CDROM_SEEK_CHANGE_DELAY : base;
    cdrom_schedule_drive_event(cdrom, delay);
}

static void begin_playing(Cdrom *cdrom, uint8_t track_param) {
    bool location_changed = false;
    if (track_param > 0 && cdrom->disc.last_track > 0) {
        uint8_t t = track_param;
        if (t > cdrom->disc.last_track) t = cdrom->disc.last_track;
        location_changed      = (cdrom->disc.tracks[t].start_lba != cdrom->current_lba);
        cdrom->current_lba    = cdrom->disc.tracks[t].start_lba;
        cdrom->setloc_pending = false;
    } else if (cdrom->setloc_pending) {
        location_changed      = (cdrom->setloc_lba != cdrom->current_lba);
        cdrom->current_lba    = cdrom->setloc_lba;
        cdrom->setloc_pending = false;
    }
    LOG_CDROM_DEBUG("[CDROM] Drive state: %s -> PLAYING (LBA %u)",
                    cdrom_drive_state_name(cdrom->drive_state), cdrom->current_lba);
    cdrom->drive_state = DRIVE_PLAYING;
    cdrom->cdda_speed  = 1;
    cdrom_async_reader_queue(&cdrom->async_reader, cdrom->current_lba);
    uint32_t base = cdrom->double_speed ? CDROM_READ_DELAY_2X : CDROM_READ_DELAY_1X;
    uint32_t delay = location_changed ? CDROM_SEEK_CHANGE_DELAY : base;
    cdrom_schedule_drive_event(cdrom, delay);
}

static void begin_seeking(Cdrom *cdrom, bool read_after, bool play_after) {
    cdrom->read_after_seek = read_after;
    cdrom->play_after_seek = play_after;
    if (cdrom->setloc_pending) {
        cdrom->target_lba     = cdrom->setloc_lba;
        cdrom->setloc_pending = false;
    }
    LOG_CDROM_DEBUG("[CDROM] Drive state: %s -> SEEKING (LBA %u -> %u)",
                    cdrom_drive_state_name(cdrom->drive_state), cdrom->current_lba, cdrom->target_lba);
    cdrom->drive_state = DRIVE_SEEKING;
    /* Use fast delay when head is already at target (pcsx-redux: 0x800 cycles) */
    uint32_t delay = (cdrom->target_lba == cdrom->current_lba)
        ? CDROM_SEEK_FAST_DELAY
        : cdrom_disc_get_seek_ticks(cdrom->current_lba, cdrom->target_lba);
    delay += cdrom->pending_speed_change;   /* see Setmode */
    cdrom->pending_speed_change = 0;
    cdrom_schedule_second_response_event(cdrom, delay);
}

/* =========================================================================
 * First Response (cdrom_execute_command)
 * ========================================================================= */

void cdrom_execute_command(Cdrom *cdrom) {
    CdromCommand cmd = cdrom->pending_command;
    cdrom->pending_command = CDC_NONE;
    cdrom->current_command = cmd;

    /* restore parameters */
    fifo_clear(&cdrom->param_fifo);
    for (int i = 0; i < cdrom->pending_param_count; i++)
        fifo_push(&cdrom->param_fifo, cdrom->pending_params[i]);

    LOG_CDROM_DEBUG("[CDROM] Execute command %s (0x%02X)", cdrom_cmd_name((uint8_t)cmd), (uint8_t)cmd);
    fifo_clear(&cdrom->response_fifo);

    /* Commands that need a disc: return NOT_READY immediately if none present */
    if (!cdrom->disc_present) {
        switch (cmd) {
            case CDC_SETLOC:
            case CDC_PLAY:
            case CDC_READN: case CDC_READS:
            case CDC_SEEKL: case CDC_SEEKP:
                cdrom_send_error(cdrom, cdrom_get_stat_byte(cdrom) | STAT_BYTE_ERROR, 0x80);
                return;
            default: break;
        }
    }

    switch (cmd) {

    /* --- 0x00 Sync --- */
    case CDC_SYNC:
        cdrom_push_response(cdrom, cdrom_get_stat_byte(cdrom));
        cdrom_send_ack(cdrom);
        break;

    /* --- 0x01 Getstat --- */
    case CDC_GETSTAT:
        cdrom_push_response(cdrom, cdrom_get_stat_byte(cdrom));
        cdrom_send_ack(cdrom);
        break;

    /* --- 0x02 Setloc --- */
    case CDC_SETLOC: {
        uint8_t mm = cdrom_pop_param(cdrom);
        uint8_t ss = cdrom_pop_param(cdrom);
        uint8_t ff = cdrom_pop_param(cdrom);
        uint8_t m  = cdrom_from_bcd(mm);
        uint8_t s  = cdrom_from_bcd(ss);
        uint8_t f  = cdrom_from_bcd(ff);
        cdrom->setloc_lba    = (uint32_t)((m*60 + s)*75 + f) - 150;
        cdrom->setloc_pending = true;
        LOG_CDROM_DEBUG("[CDROM] Setloc %02u:%02u:%02u -> LBA %u", m, s, f, cdrom->setloc_lba);
        cdrom_push_response(cdrom, cdrom_get_stat_byte(cdrom));
        cdrom_send_ack(cdrom);
        break;
    }

    /* --- 0x03 Play --- */
    case CDC_PLAY: {
        uint8_t track_param = cdrom_pop_param(cdrom);
        cdrom_push_response(cdrom, cdrom_get_stat_byte(cdrom));
        cdrom_send_ack(cdrom);
        begin_playing(cdrom, track_param);
        break;
    }

    /* --- 0x04 Forward --- */
    case CDC_FORWARD:
        if (cdrom->drive_state == DRIVE_PLAYING) {
            cdrom->cdda_speed = (cdrom->cdda_speed < 4) ? cdrom->cdda_speed * 2 : 4;
        }
        cdrom_push_response(cdrom, cdrom_get_stat_byte(cdrom));
        cdrom_send_ack(cdrom);
        break;

    /* --- 0x05 Backward --- */
    case CDC_BACKWARD:
        if (cdrom->drive_state == DRIVE_PLAYING) {
            if (cdrom->current_lba > 4) cdrom->current_lba -= 4;
            else cdrom->current_lba = 0;
            cdrom_async_reader_queue(&cdrom->async_reader, cdrom->current_lba);
        }
        cdrom_push_response(cdrom, cdrom_get_stat_byte(cdrom));
        cdrom_send_ack(cdrom);
        break;

    /* --- 0x06 ReadN / 0x1B ReadS --- */
    case CDC_READN:
    case CDC_READS:
        cdrom_push_response(cdrom, cdrom_get_stat_byte(cdrom));
        cdrom_send_ack(cdrom);
        begin_reading(cdrom);
        break;

    /* --- 0x07 MotorOn --- */
    case CDC_MOTORON:
        cdrom->motor_on = true;
        cdrom_push_response(cdrom, cdrom_get_stat_byte(cdrom));
        cdrom_send_ack(cdrom);
        cdrom->second_response_cmd = CDC_MOTORON;
        cdrom_schedule_second_response_event(cdrom, CDROM_SPINUP_DELAY);
        break;

    /* --- 0x08 Stop --- */
    case CDC_STOP: {
        uint32_t stop_delay = (cdrom->motor_on && cdrom->drive_state != DRIVE_IDLE)
            ? CDROM_STOP_SPIN_DELAY : CDROM_STOP_IDLE_DELAY;
        cdrom->drive_state = DRIVE_STOPPING;
        cdrom_push_response(cdrom, cdrom_get_stat_byte(cdrom));
        cdrom_send_ack(cdrom);
        cdrom->second_response_cmd = CDC_STOP;
        cdrom_schedule_second_response_event(cdrom, stop_delay);
        break;
    }

    /* --- 0x09 Pause --- */
    case CDC_PAUSE: {
        /* pcsx-redux hardware-tested: 7ms if already idle, else 1s/2s by speed */
        uint32_t pause_delay;
        if (cdrom->drive_state == DRIVE_IDLE) {
            pause_delay = CDROM_PAUSE_IDLE_DELAY;
        } else {
            pause_delay = cdrom->double_speed ? CDROM_PAUSE_2X_DELAY : CDROM_PAUSE_1X_DELAY;
            if (cdrom->drive_state == DRIVE_READING || cdrom->drive_state == DRIVE_PLAYING)
                cdrom->drive_state = DRIVE_PAUSING;
        }
        cdrom_push_response(cdrom, cdrom_get_stat_byte(cdrom));
        cdrom_send_ack(cdrom);
        cdrom->second_response_cmd = CDC_PAUSE;
        cdrom_schedule_second_response_event(cdrom, pause_delay);
        break;
    }

    /* --- 0x0A Init --- */
    case CDC_INIT:
        cdrom_push_response(cdrom, cdrom_get_stat_byte(cdrom));
        cdrom_send_ack(cdrom);
        cdrom->second_response_cmd = CDC_INIT;
        cdrom_schedule_second_response_event(cdrom, CDROM_INIT_DELAY);
        break;

    /* --- 0x0B Mute --- */
    case CDC_MUTE:
        cdrom->muted = true;
        cdrom_push_response(cdrom, cdrom_get_stat_byte(cdrom));
        cdrom_send_ack(cdrom);
        break;

    /* --- 0x0C Demute --- */
    case CDC_DEMUTE:
        cdrom->muted = false;
        cdrom_push_response(cdrom, cdrom_get_stat_byte(cdrom));
        cdrom_send_ack(cdrom);
        break;

    /* --- 0x0D Setfilter --- */
    case CDC_SETFILTER:
        cdrom->xa_filter_file    = cdrom_pop_param(cdrom);
        cdrom->xa_filter_channel = cdrom_pop_param(cdrom);
        cdrom_push_response(cdrom, cdrom_get_stat_byte(cdrom));
        cdrom_send_ack(cdrom);
        break;

    /* --- 0x0E Setmode --- */
    case CDC_SETMODE: {
        uint8_t m = cdrom_pop_param(cdrom);
        bool new_double_speed = (m & 0x80) != 0;
        /* Changing the read speed spins the drive up or down; hardware needs
         * about 0.6 s to go 1x->2x and 0.7 s the other way (DOCS/cdromdrive.md:
         * 1896-1908). Setmode itself only answers INT3, so the cost is carried
         * and charged to the next seek. */
        if (new_double_speed != cdrom->double_speed)
            cdrom->pending_speed_change = new_double_speed
                ? CDROM_SPEED_UP_DELAY : CDROM_SPEED_DOWN_DELAY;
        cdrom->mode           = m;
        cdrom->double_speed   = new_double_speed;
        cdrom->xa_adpcm_enable= (m & 0x40) != 0;
        cdrom->whole_sector   = (m & 0x20) != 0;
        cdrom->xa_filter_enable= (m & 0x08) != 0;
        cdrom->report_enable  = (m & 0x04) != 0;
        cdrom->auto_pause     = (m & 0x02) != 0;
        cdrom->cdda_enable    = (m & 0x01) != 0;
        LOG_CDROM_DEBUG("[CDROM] Setmode 0x%02X (2x=%d whole=%d xa=%d) @ 0x%08x",
                        m, cdrom->double_speed, cdrom->whole_sector, cdrom->xa_adpcm_enable);
        cdrom_push_response(cdrom, cdrom_get_stat_byte(cdrom));
        cdrom_send_ack(cdrom);
        break;
    }

    /* --- 0x0F Getparam --- */
    case CDC_GETPARAM:
        cdrom_push_response(cdrom, cdrom_get_stat_byte(cdrom));
        cdrom_push_response(cdrom, cdrom->mode);
        cdrom_push_response(cdrom, 0x00);  /* reserved */
        cdrom_push_response(cdrom, cdrom->xa_filter_file);
        cdrom_push_response(cdrom, cdrom->xa_filter_channel);
        cdrom_send_ack(cdrom);
        break;

    /* --- 0x10 GetlocL --- */
    case CDC_GETLOCL: {
        /* Return header+subheader from last read sector buffer */
        SectorBuffer *sb = &cdrom->sector_buffers[cdrom->current_read_buffer];
        if (sb->valid && sb->lba < cdrom->disc.total_sectors) {
            /* bytes 12-19: header (MM:SS:FF:mode) + subheader (file,ch,submode,coding) */
            for (int i = 0; i < 8; i++)
                cdrom_push_response(cdrom, sb->raw[12 + i]);
        } else {
            /* not reading or no valid sector */
            cdrom_send_error(cdrom, cdrom_get_stat_byte(cdrom) | STAT_BYTE_ERROR, 0x80);
            break;
        }
        cdrom_send_ack(cdrom);
        break;
    }

    /* --- 0x11 GetlocP --- */
    case CDC_GETLOCP: {
        SubQ *q = &cdrom->last_subq;
        cdrom_push_response(cdrom, q->track_bcd);
        cdrom_push_response(cdrom, q->index_bcd);
        cdrom_push_response(cdrom, q->rel_mm_bcd);
        cdrom_push_response(cdrom, q->rel_ss_bcd);
        cdrom_push_response(cdrom, q->rel_ff_bcd);
        cdrom_push_response(cdrom, q->abs_mm_bcd);
        cdrom_push_response(cdrom, q->abs_ss_bcd);
        cdrom_push_response(cdrom, q->abs_ff_bcd);
        cdrom_send_ack(cdrom);
        break;
    }

    /* --- 0x12 ReadT (session info) --- */
    case CDC_READT:
        (void)cdrom_pop_param(cdrom);  /* session number, ignored */
        cdrom_push_response(cdrom, cdrom_get_stat_byte(cdrom));
        cdrom_send_ack(cdrom);
        /* second response: session 1 starts at 00:02:00 */
        cdrom->second_response_data[0] = cdrom_get_stat_byte(cdrom);
        cdrom->second_response_data[1] = 0x00;  /* complete flag */
        cdrom->second_response_data[2] = 0x02;  /* start MM */
        cdrom->second_response_data[3] = 0x00;
        cdrom->second_response_size = 4;
        cdrom->second_response_cmd  = CDC_READT;
        cdrom_schedule_second_response_event(cdrom, CDROM_ACK_DELAY);
        break;

    /* --- 0x13 GetTN --- */
    case CDC_GETTN:
        cdrom_push_response(cdrom, cdrom_get_stat_byte(cdrom));
        cdrom_push_response(cdrom, cdrom_to_bcd(cdrom->disc.first_track ? cdrom->disc.first_track : 1));
        cdrom_push_response(cdrom, cdrom_to_bcd(cdrom->disc.last_track  ? cdrom->disc.last_track  : 1));
        cdrom_send_ack(cdrom);
        break;

    /* --- 0x14 GetTD --- */
    case CDC_GETTD: {
        uint8_t tnum = cdrom_pop_param(cdrom);
        cdrom_push_response(cdrom, cdrom_get_stat_byte(cdrom));
        uint32_t lba = 0;
        if (tnum == 0) {
            lba = cdrom->disc.total_sectors; /* lead-out */
        } else if (tnum >= cdrom->disc.first_track && tnum <= cdrom->disc.last_track) {
            lba = cdrom->disc.tracks[tnum].start_lba;
        }
        uint8_t mm, ss, ff;
        cdrom_lba_to_msf(lba, &mm, &ss, &ff);
        cdrom_push_response(cdrom, cdrom_to_bcd(mm));
        cdrom_push_response(cdrom, cdrom_to_bcd(ss));
        cdrom_send_ack(cdrom);
        break;
    }

    /* --- 0x15 SeekL / 0x16 SeekP --- */
    case CDC_SEEKL:
    case CDC_SEEKP:
        cdrom_push_response(cdrom, cdrom_get_stat_byte(cdrom));
        cdrom_send_ack(cdrom);
        cdrom->second_response_cmd = cmd;
        begin_seeking(cdrom, false, false);
        break;

    /* --- 0x17 SetClock (not emulated) --- */
    case CDC_SETCLOCK:
        cdrom_push_response(cdrom, cdrom_get_stat_byte(cdrom));
        cdrom_send_ack(cdrom);
        break;

    /* --- 0x18 GetClock (not emulated) --- */
    case CDC_GETCLOCK:
        for (int i = 0; i < 8; i++) cdrom_push_response(cdrom, 0x00);
        cdrom_send_ack(cdrom);
        break;

    /* --- 0x19 Test --- */
    case CDC_TEST: {
        uint8_t sub = cdrom_pop_param(cdrom);
        switch (sub) {
        case 0x20:  /* BIOS version: 94/09/19, vC0 */
            cdrom_push_response(cdrom, 0x94);
            cdrom_push_response(cdrom, 0x09);
            cdrom_push_response(cdrom, 0x19);
            cdrom_push_response(cdrom, 0xC0);
            cdrom_send_ack(cdrom);
            break;
        case 0x04:  /* reset SCEx, force motor on */
            cdrom->motor_on = true;
            cdrom_push_response(cdrom, cdrom_get_stat_byte(cdrom));
            cdrom_send_ack(cdrom);
            break;
        case 0x05:  /* SCEx counters */
            cdrom_push_response(cdrom, cdrom_get_stat_byte(cdrom));
            cdrom_push_response(cdrom, 0x00);
            cdrom_push_response(cdrom, 0x00);
            cdrom_send_ack(cdrom);
            break;
        case 0x22:  /* region string */
            cdrom_push_response(cdrom, 'f');
            cdrom_push_response(cdrom, 'o');
            cdrom_push_response(cdrom, 'r');
            cdrom_push_response(cdrom, ' ');
            cdrom_push_response(cdrom, 'U');
            cdrom_push_response(cdrom, '/');
            cdrom_push_response(cdrom, 'C');
            cdrom_send_ack(cdrom);
            break;
        default:
            cdrom_push_response(cdrom, cdrom_get_stat_byte(cdrom));
            cdrom_send_ack(cdrom);
            break;
        }
        break;
    }

    /* --- 0x1A GetID --- */
    case CDC_GETID:
        cdrom_push_response(cdrom, cdrom_get_stat_byte(cdrom));
        cdrom_send_ack(cdrom);
        cdrom->second_response_cmd = CDC_GETID;
        cdrom_schedule_second_response_event(cdrom, CDROM_ID_READ_DELAY);
        break;

    /* --- 0x1C Reset --- */
    case CDC_RESET:
        cdrom->drive_state = DRIVE_IDLE;
        cdrom->mode        = 0;
        cdrom->motor_on    = cdrom->disc_present;
        cdrom_push_response(cdrom, cdrom_get_stat_byte(cdrom));
        cdrom_send_ack(cdrom);
        cdrom->second_response_cmd = CDC_RESET;
        cdrom_schedule_second_response_event(cdrom, CDROM_RESET_DELAY);
        break;

    /* --- 0x1D GetQ --- */
    case CDC_GETQ: {
        /* Returns last SubQ data (10 bytes) */
        SubQ *q = &cdrom->last_subq;
        cdrom_push_response(cdrom, cdrom_get_stat_byte(cdrom));
        cdrom_push_response(cdrom, q->control_adr);
        cdrom_push_response(cdrom, q->track_bcd);
        cdrom_push_response(cdrom, q->index_bcd);
        cdrom_push_response(cdrom, q->rel_mm_bcd);
        cdrom_push_response(cdrom, q->rel_ss_bcd);
        cdrom_push_response(cdrom, q->rel_ff_bcd);
        cdrom_push_response(cdrom, q->reserved);
        cdrom_push_response(cdrom, q->abs_mm_bcd);
        cdrom_push_response(cdrom, q->abs_ss_bcd);
        cdrom_push_response(cdrom, q->abs_ff_bcd);
        cdrom_send_ack(cdrom);
        break;
    }

    /* --- 0x1E ReadTOC --- */
    case CDC_READTOC:
        cdrom_push_response(cdrom, cdrom_get_stat_byte(cdrom));
        cdrom_send_ack(cdrom);
        cdrom->second_response_cmd = CDC_READTOC;
        cdrom_schedule_second_response_event(cdrom, CDROM_INIT_DELAY);
        break;

    /* --- 0x1F VideoCD (not emulated) --- */
    case CDC_VIDEOCD:
        cdrom_send_error(cdrom, cdrom_get_stat_byte(cdrom) | STAT_BYTE_ERROR, ERROR_INVALID_COMMAND);
        break;

    default:
        LOG_CDROM_WARN("[CDROM] Unknown cmd 0x%02X (drive: %s)", (unsigned)cmd, cdrom_drive_state_name(cdrom->drive_state));
        cdrom_send_error(cdrom, cdrom_get_stat_byte(cdrom) | STAT_BYTE_ERROR, ERROR_INVALID_COMMAND);
        break;
    }

    fifo_clear(&cdrom->param_fifo);
    cdrom->current_command = CDC_NONE;
}

/* =========================================================================
 * Second Response
 * ========================================================================= */

void cdrom_execute_second_response(Cdrom *cdrom) {
    CdromCommand cmd = cdrom->second_response_cmd;
    cdrom->second_response_cmd = CDC_NONE;
    fifo_clear(&cdrom->response_fifo);
    LOG_CDROM_DEBUG("[CDROM] Second response: %s (0x%02X)", cdrom_cmd_name((uint8_t)cmd), (uint8_t)cmd);

    switch (cmd) {

    case CDC_GETID:
        LOG_CDROM_DEBUG("[CDROM] GetID second: disc_present=%d motor_on=%d shell_open=%d region='%c'",
                        cdrom->disc_present, cdrom->motor_on, cdrom->shell_open,
                        cdrom->disc_region ? cdrom->disc_region : '?');
        if (!cdrom->disc_present) {
            cdrom_send_error(cdrom, 0x08, 0x40);
        } else {
            cdrom_push_response(cdrom, cdrom_get_stat_byte(cdrom));
            cdrom_push_response(cdrom, 0x00);  /* licensed */
            cdrom_push_response(cdrom, 0x20);  /* mode 2 */
            cdrom_push_response(cdrom, 0x00);
            cdrom_push_response(cdrom, 'S');
            cdrom_push_response(cdrom, 'C');
            cdrom_push_response(cdrom, 'E');
            /* Reflects the disc's real licence region (cdrom_disc_detect_region),
             * not hardcoded — see DOCS/cdromdrive.md, GetID's 4th SCEx byte. */
            cdrom_push_response(cdrom, cdrom->disc_region ? cdrom->disc_region : 'A');
            cdrom_send_complete(cdrom);
        }
        break;

    case CDC_INIT:
        cdrom->motor_on    = cdrom->disc_present;
        cdrom->drive_state = DRIVE_IDLE;
        cdrom->mode        = 0;
        cdrom_push_response(cdrom, cdrom_get_stat_byte(cdrom));
        cdrom_send_complete(cdrom);
        break;

    case CDC_RESET:
        cdrom_push_response(cdrom, cdrom_get_stat_byte(cdrom));
        cdrom_send_complete(cdrom);
        break;

    case CDC_STOP:
        cdrom->motor_on    = false;
        cdrom->drive_state = DRIVE_IDLE;
        cdrom_push_response(cdrom, cdrom_get_stat_byte(cdrom));
        cdrom_send_complete(cdrom);
        break;

    case CDC_PAUSE:
        cdrom->drive_state = DRIVE_IDLE;
        cdrom_push_response(cdrom, cdrom_get_stat_byte(cdrom));
        cdrom_send_complete(cdrom);
        break;

    case CDC_MOTORON:
        cdrom_push_response(cdrom, cdrom_get_stat_byte(cdrom));
        cdrom_send_complete(cdrom);
        break;

    case CDC_SEEKL:
    case CDC_SEEKP:
        cdrom->current_lba = cdrom->target_lba;
        cdrom->drive_state = DRIVE_IDLE;
        /* update SubQ for new position */
        cdrom->last_subq = cdrom_disc_get_subq(&cdrom->disc, cdrom->current_lba);
        cdrom_push_response(cdrom, cdrom_get_stat_byte(cdrom));
        cdrom_send_complete(cdrom);
        if (cdrom->read_after_seek) {
            cdrom->read_after_seek = false;
            begin_reading(cdrom);
        } else if (cdrom->play_after_seek) {
            cdrom->play_after_seek = false;
            begin_playing(cdrom, 0);
        }
        break;

    case CDC_READT:
        for (int i = 0; i < cdrom->second_response_size; i++)
            cdrom_push_response(cdrom, cdrom->second_response_data[i]);
        cdrom_send_complete(cdrom);
        break;

    case CDC_READTOC:
        cdrom_push_response(cdrom, cdrom_get_stat_byte(cdrom));
        cdrom_send_complete(cdrom);
        break;

    default:
        break;
    }
}

/* =========================================================================
 * Drive Loop (sector reading / CDDA playing)
 * ========================================================================= */

void cdrom_execute_drive(Cdrom *cdrom) {
    if (cdrom->drive_state == DRIVE_READING) {
        /* Retrieve sector from async reader */
        uint8_t raw[2352];
        bool ok = cdrom_async_reader_wait(&cdrom->async_reader, raw);
        if (!ok) {
            /* No-disc → NOT_READY (0x80): BIOS retries. Disc error → SEEK_ERROR (0x04): BIOS aborts. */
            uint8_t err_reason = cdrom->disc_present ? 0x04 : 0x80;
            LOG_CDROM_DEBUG("[CDROM] Read failed at LBA %u (disc_present=%d, err=0x%02x)",
                            cdrom->current_lba, cdrom->disc_present, err_reason);
            cdrom_send_error(cdrom, cdrom_get_stat_byte(cdrom) | STAT_BYTE_ERROR, err_reason);
            cdrom->drive_state = DRIVE_IDLE;
            return;
        }

        /* Write into ring buffer */
        uint8_t widx = cdrom->current_write_buffer;
        SectorBuffer *sb = &cdrom->sector_buffers[widx];
        memcpy(sb->raw, raw, 2352);
        sb->lba   = cdrom->current_lba;
        sb->valid = true;

        cdrom->sectors_read_total++;

        /* Check XA subheader: submode byte at offset 18 */
        uint8_t submode = raw[18];
        bool is_xa_audio = (submode & 0x04) != 0;  /* bit 2 = AUDIO */
        bool is_realtime = (submode & 0x40) != 0;  /* bit 6 = REALTIME */
        /* Coding info (DOCS/cdromformat.md:664-671, DOCS/cdromdrive.md:265-278):
         *   bits 0-1  mono / stereo
         *   bit  2    sample rate, 0 = 37800 Hz, 1 = 18900 Hz
         *   bit  4    bits per sample, 0 = 4-bit, 1 = 8-bit
         * The rate and depth bits used to be read as 0x04 and 0x08, so every
         * stream that was not 37800 Hz 4-bit decoded as noise. */
        uint8_t coding   = raw[19];
        bool xa_stereo   = (coding & 0x01) != 0;
        bool xa_18900    = (coding & 0x04) != 0;
        bool xa_8bit     = (coding & 0x10) != 0;
        bool file_match  = !cdrom->xa_filter_enable
                           || (raw[16] == cdrom->xa_filter_file
                               && raw[17] == cdrom->xa_filter_channel);

        if (cdrom->xa_adpcm_enable && is_xa_audio && is_realtime && file_match) {
            cdrom->xa_sectors_total++;
            /* XA-ADPCM sector: decode to audio FIFO — NO INT1 */
            cdrom_audio_decode_xa(&cdrom->xa_adpcm_state, &cdrom->audio_fifo,
                                   raw + 24, xa_stereo, xa_8bit, xa_18900,
                                   cdrom->muted);

            cdrom->current_lba++;
            if (cdrom->drive_state == DRIVE_READING) {
                cdrom_async_reader_queue(&cdrom->async_reader, cdrom->current_lba);
                /* No INT1 → must self-schedule next drive event */
                uint32_t delay = cdrom->double_speed ? CDROM_READ_DELAY_2X : CDROM_READ_DELAY_1X;
                cdrom_schedule_drive_event(cdrom, delay);
            }
        } else {
            /* Data sector: sector size from mode bits 4-5 (nocash PSX-SPX) */
            if (cdrom->mode & 0x20) {       /* bit5: 2340 bytes from sync header */
                sb->data_start = 12;
                sb->data_size  = 2340;
            } else if (cdrom->mode & 0x10) { /* bit4: 2328 bytes (with subheader, no sync) */
                sb->data_start = 24;
                sb->data_size  = 2328;
            } else {                         /* default: 2048 user data */
                sb->data_start = 24;
                sb->data_size  = 2048;
            }
            sb->position = 0;

            cdrom->current_read_buffer  = widx;
            cdrom->current_write_buffer = (widx + 1) % CDROM_SECTOR_BUFFERS;

            /* Update SubQ */
            cdrom->current_subq_lba = cdrom->current_lba;
            cdrom->last_subq = cdrom_disc_get_subq(&cdrom->disc, cdrom->current_lba);

            LOG_CDROM_DEBUG("[CDROM] Sector LBA=%u -> INT1", cdrom->current_lba);

            /* INT1: data ready — INT ACK handler will schedule next drive event */
            fifo_clear(&cdrom->response_fifo);
            cdrom_push_response(cdrom, cdrom_get_stat_byte(cdrom));
            cdrom->interrupt_flag = CDROM_INT_DATA_READY;
            if (cdrom->inter) interconnect_trigger_cdrom_irq(cdrom->inter);
            lua_debug_notify("cdrom_int1");

            cdrom->current_lba++;
            if (cdrom->drive_state == DRIVE_READING)
                cdrom_async_reader_queue(&cdrom->async_reader, cdrom->current_lba);
        }

    } else if (cdrom->drive_state == DRIVE_PLAYING) {
        /* CDDA */
        uint8_t raw[2352];
        bool ok = cdrom_async_reader_wait(&cdrom->async_reader, raw);
        if (!ok) {
            LOG_CDROM_ERROR("[CDROM] CDDA read failed at LBA %u", cdrom->current_lba);
            cdrom->drive_state = DRIVE_IDLE;
            return;
        }

        /* Check for lead-out (track AA) */
        uint8_t track_num = cdrom_disc_get_track_at_lba(&cdrom->disc, cdrom->current_lba);
        bool is_lead_out  = (cdrom->current_lba >= cdrom->disc.total_sectors);
        bool track_is_audio = (track_num >= cdrom->disc.first_track)
                               ? cdrom->disc.tracks[track_num].is_audio
                               : false;

        if (is_lead_out) {
            /* End of disc: INT4 DataEnd */
            fifo_clear(&cdrom->response_fifo);
            cdrom_push_response(cdrom, cdrom_get_stat_byte(cdrom));
            cdrom->interrupt_flag = CDROM_INT_DATA_END;
            cdrom->drive_state    = DRIVE_IDLE;
            if (cdrom->inter) interconnect_trigger_cdrom_irq(cdrom->inter);
            return;
        }

        if (track_is_audio) {
            cdrom_audio_process_cdda(&cdrom->audio_fifo, raw, cdrom->muted);
        }

        /* Auto-pause on track change */
        if (cdrom->auto_pause) {
            static uint8_t prev_track = 0;
            uint8_t ct = cdrom_disc_get_track_at_lba(&cdrom->disc, cdrom->current_lba);
            if (prev_track != 0 && ct != prev_track) {
                /* INT4 */
                fifo_clear(&cdrom->response_fifo);
                cdrom_push_response(cdrom, cdrom_get_stat_byte(cdrom));
                cdrom->interrupt_flag = CDROM_INT_DATA_END;
                cdrom->drive_state    = DRIVE_IDLE;
                prev_track = 0;
                if (cdrom->inter) interconnect_trigger_cdrom_irq(cdrom->inter);
                return;
            }
            prev_track = ct;
        }

        /* Update SubQ */
        cdrom->current_subq_lba = cdrom->current_lba;
        cdrom->last_subq = cdrom_disc_get_subq(&cdrom->disc, cdrom->current_lba);

        /* Report mode: send INT1 with position every sector (pcsx-redux) */
        if (cdrom->report_enable && cdrom->interrupt_flag == CDROM_INT_NONE) {
            SubQ *sq = &cdrom->last_subq;
            fifo_clear(&cdrom->response_fifo);
            cdrom_push_response(cdrom, cdrom_get_stat_byte(cdrom));
            cdrom_push_response(cdrom, sq->track_bcd);
            cdrom_push_response(cdrom, sq->index_bcd);
            cdrom_push_response(cdrom, sq->rel_mm_bcd);
            cdrom_push_response(cdrom, sq->rel_ss_bcd | 0x80); /* bit7 = audio */
            cdrom_push_response(cdrom, sq->rel_ff_bcd);
            cdrom_push_response(cdrom, sq->abs_mm_bcd);
            cdrom_push_response(cdrom, sq->abs_ss_bcd);
            cdrom_push_response(cdrom, sq->abs_ff_bcd);
            cdrom->interrupt_flag = CDROM_INT_DATA_READY;
            if (cdrom->inter) interconnect_trigger_cdrom_irq(cdrom->inter);
        }

        cdrom->current_lba += cdrom->cdda_speed;

        /* Queue next */
        if (cdrom->drive_state == DRIVE_PLAYING)
            cdrom_async_reader_queue(&cdrom->async_reader, cdrom->current_lba);

        /* Schedule next drive event */
        uint32_t delay = cdrom->double_speed ? CDROM_READ_DELAY_2X : CDROM_READ_DELAY_1X;
        cdrom_schedule_drive_event(cdrom, delay);
    }
}
