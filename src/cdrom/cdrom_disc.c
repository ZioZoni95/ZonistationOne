/* SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 ZioZoni95
 *
 * Part of ZoniStation One, a PlayStation 1 emulator.
 * See LICENSE for the full licence text and THIRD-PARTY.md for the
 * components of this project that have other authors.
 */
/*
 * CDROM disc layer: CUE/BIN parsing, sector read, SubQ generation,
 * seek timing, and async reader thread.
 */

#include "cdrom_disc.h"
#include "cdrom_ecm.h"
#include "cdrom.h"
#include "log.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* =========================================================================
 * CUE Sheet Parser
 * ========================================================================= */

static uint32_t parse_msf_lba(const char *msf_str) {
    unsigned mm = 0, ss = 0, ff = 0;
    sscanf(msf_str, "%u:%u:%u", &mm, &ss, &ff);
    return mm*60*75 + ss*75 + ff;
}

static void close_all_track_files(CdromDisc *disc) {
    FILE       *prev_file = NULL;
    EcmDecoder *prev_ecm  = NULL;
    for (int i = 1; i <= disc->last_track; i++) {
        /* One decoder serves every track that came from the same FILE, exactly
         * as the handle below does, so it needs the same guard: freeing it per
         * track double-frees from the second track of a multi-track CUE on. */
        if (disc->tracks[i].ecm && disc->tracks[i].ecm != prev_ecm) {
            prev_ecm = disc->tracks[i].ecm;
            ecm_decoder_free(disc->tracks[i].ecm);
        }
        disc->tracks[i].ecm = NULL;
        if (disc->tracks[i].file && disc->tracks[i].file != prev_file) {
            fclose(disc->tracks[i].file);
            prev_file = disc->tracks[i].file;
        }
        disc->tracks[i].file = NULL;
    }
}

bool cdrom_disc_load(CdromDisc *disc, const char *cue_path) {
    memset(disc, 0, sizeof(*disc));

    /* Build directory prefix from cue_path */
    char dir[512] = {0};
    const char *last_slash = strrchr(cue_path, '/');
    if (last_slash) {
        size_t dlen = (size_t)(last_slash - cue_path + 1);
        strncpy(dir, cue_path, dlen < sizeof(dir)-1 ? dlen : sizeof(dir)-1);
    }

    /* Accept a bare .bin or .bin.ecm path for quick testing */
    size_t plen = strlen(cue_path);
    bool is_bin    = (plen >= 4 && strcmp(cue_path + plen - 4, ".bin") == 0);
    bool is_bin_ecm = (plen >= 8 && strcmp(cue_path + plen - 8, ".bin.ecm") == 0);
    if (is_bin || is_bin_ecm) {
        FILE *bin = fopen(cue_path, "rb");
        if (!bin) { LOG_CDROM_ERROR("[CDROM] Cannot open BIN: %s", cue_path); return false; }

        /* Check for ECM header */
        if (ecm_detect(bin)) {
            EcmDecoder *ecm = ecm_decoder_open(bin);
            if (!ecm) { fclose(bin); return false; }
            disc->first_track = 1;
            disc->last_track  = 1;
            disc->total_sectors = ecm->total_sectors;
            disc->tracks[1].number            = 1;
            disc->tracks[1].is_audio          = false;
            disc->tracks[1].start_lba         = 0;
            disc->tracks[1].file              = bin;
            disc->tracks[1].file_offset_bytes = 0;
            disc->tracks[1].ecm               = ecm;
            LOG_CDROM_INFO("[CDROM] Loaded ECM: %s (%u sectors)", cue_path, disc->total_sectors);
            return true;
        }

        fseek(bin, 0, SEEK_END);
        long fsz = ftell(bin);
        fseek(bin, 0, SEEK_SET);

        disc->first_track = 1;
        disc->last_track  = 1;
        disc->total_sectors = (uint32_t)(fsz / CDROM_RAW_SECTOR);
        disc->tracks[1].number            = 1;
        disc->tracks[1].is_audio          = false;
        disc->tracks[1].start_lba         = 0;
        disc->tracks[1].file              = bin;
        disc->tracks[1].file_offset_bytes = 0;
        LOG_CDROM_INFO("[CDROM] Loaded BIN: %s (%u sectors)", cue_path, disc->total_sectors);
        return true;
    }

    FILE *cue = fopen(cue_path, "r");
    if (!cue) { LOG_CDROM_ERROR("[CDROM] Cannot open CUE: %s", cue_path); return false; }

    /* --- Parse CUE --- */
    char    line[512];
    FILE   *cur_file    = NULL;
    EcmDecoder *cur_ecm = NULL;
    uint8_t cur_track   = 0;
    bool    multi_file  = false;
    uint32_t prev_file_end_lba = 0; /* cumulative LBA for multi-file discs */

    /* Per-track raw INDEX 01 MSF (within the current BIN file) */
    uint32_t idx01_lba[100] = {0};  /* file-relative LBA for each track */
    uint32_t idx00_lba[100] = {0};  /* pregap */
    /* PREGAP durations are NOT stored in the BIN
     * (cdromfileformats.md:14929-14933): every track after one is shifted by it
     * on the disc while its data stays where it is in the file. Ignoring these
     * lines put every following track at the wrong disc address. */
    uint32_t pregap_extra[100] = {0};
    FILE    *track_file[100] = {0};
    EcmDecoder *track_ecm[100] = {0};

    while (fgets(line, sizeof(line), cue)) {
        /* strip leading whitespace */
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;

        if (strncmp(p, "FILE", 4) == 0) {
            /* Close previous file if multi-file */
            char *q1 = strchr(p, '"');
            if (!q1) continue;
            char *q2 = strchr(q1+1, '"');
            if (!q2) continue;
            char fname[256] = {0};
            size_t flen = (size_t)(q2 - q1 - 1);
            strncpy(fname, q1+1, flen < sizeof(fname)-1 ? flen : sizeof(fname)-1);

            char full_path[768] = {0};
            snprintf(full_path, sizeof(full_path), "%s%s", dir, fname);

            /* If a FILE was already open, this is multi-file */
            if (cur_file) multi_file = true;

            cur_file = fopen(full_path, "rb");
            if (!cur_file) {
                LOG_CDROM_ERROR("[CDROM] Cannot open BIN: %s", full_path);
                fclose(cue);
                return false;
            }
            cur_ecm = NULL;
            if (ecm_detect(cur_file))
                cur_ecm = ecm_decoder_open(cur_file);
            LOG_CDROM_DEBUG("[CDROM] File: %s%s", full_path,
                            cur_ecm ? " [ECM]" : "");

        } else if (strncmp(p, "TRACK", 5) == 0) {
            unsigned tnum = 0;
            char ttype[32] = {0};
            sscanf(p + 5, " %u %31s", &tnum, ttype);
            if (tnum < 1 || tnum > 99) continue;

            cur_track = (uint8_t)tnum;
            if (cur_track > disc->last_track) disc->last_track = cur_track;
            if (disc->first_track == 0 || cur_track < disc->first_track)
                disc->first_track = cur_track;

            disc->tracks[cur_track].number   = cur_track;
            disc->tracks[cur_track].is_audio = (strncmp(ttype, "AUDIO", 5) == 0);
            track_file[cur_track] = cur_file;
            track_ecm[cur_track]  = cur_ecm;

        } else if (strncmp(p, "PREGAP", 6) == 0) {
            char msf[16] = {0};
            sscanf(p + 6, " %15s", msf);
            if (cur_track) pregap_extra[cur_track] = parse_msf_lba(msf);

        } else if (strncmp(p, "INDEX", 5) == 0) {
            unsigned idx_num = 0;
            char msf[16] = {0};
            sscanf(p + 5, " %u %15s", &idx_num, msf);
            uint32_t lba = parse_msf_lba(msf);
            if (cur_track == 0) continue;
            if (idx_num == 1) idx01_lba[cur_track] = lba;
            if (idx_num == 0) idx00_lba[cur_track] = lba;
        }
    }
    fclose(cue);

    if (disc->last_track == 0) {
        LOG_CDROM_ERROR("[CDROM] CUE parse failed: no tracks found");
        return false;
    }

    /* --- Compute start_lba and file_offset_bytes --- */
    if (!multi_file) {
        /* Single BIN: INDEX 01 MSF is the file-relative sector number, and a
         * PREGAP shifts this track and every later one along the disc without
         * occupying any bytes in the file — so the disc address carries the
         * accumulated shift while file_offset_bytes stays as written. */
        uint32_t shift = 0;
        for (int i = disc->first_track; i <= disc->last_track; i++) {
            shift += pregap_extra[i];
            disc->tracks[i].file              = track_file[i] ? track_file[i]
                                                               : track_file[disc->first_track];
            disc->tracks[i].start_lba         = idx01_lba[i] + shift;
            disc->tracks[i].pregap_lba        = idx00_lba[i] ? idx00_lba[i] + shift
                                                             : (pregap_extra[i] ? idx01_lba[i] + shift - pregap_extra[i] : 0);
            disc->tracks[i].file_offset_bytes = idx01_lba[i] * CDROM_RAW_SECTOR;
            disc->tracks[i].ecm               = track_ecm[i];
        }
        /* total_sectors: ECM tracks carry their own count; otherwise
         * derive from the shared BIN file plus the accumulated gaps. */
        if (disc->tracks[disc->first_track].ecm) {
            disc->total_sectors = disc->tracks[disc->first_track].ecm->total_sectors + shift;
        } else {
            FILE *f = disc->tracks[disc->first_track].file;
            if (f) {
                fseek(f, 0, SEEK_END);
                disc->total_sectors = (uint32_t)(ftell(f) / CDROM_RAW_SECTOR) + shift;
                fseek(f, 0, SEEK_SET);
            }
        }
    } else {
        /* Multi-file: each track starts where the previous ended */
        uint32_t accum_lba = 0;
        for (int i = disc->first_track; i <= disc->last_track; i++) {
            disc->tracks[i].file              = track_file[i];
            disc->tracks[i].ecm               = track_ecm[i];
            disc->tracks[i].start_lba         = accum_lba;
            disc->tracks[i].pregap_lba        = (idx00_lba[i] > 0)
                                                ? accum_lba - (idx01_lba[i] - idx00_lba[i])
                                                : 0;
            disc->tracks[i].file_offset_bytes = 0;
            if (track_ecm[i]) {
                accum_lba += track_ecm[i]->total_sectors;
            } else if (track_file[i]) {
                fseek(track_file[i], 0, SEEK_END);
                uint32_t sectors = (uint32_t)(ftell(track_file[i]) / CDROM_RAW_SECTOR);
                fseek(track_file[i], 0, SEEK_SET);
                accum_lba += sectors;
            }
        }
        disc->total_sectors = accum_lba;
        (void)prev_file_end_lba;
    }

    LOG_CDROM_INFO("[CDROM] Loaded: %u tracks, %u sectors (multi=%d)",
                   disc->last_track, disc->total_sectors, multi_file);
    for (int i = disc->first_track; i <= disc->last_track; i++) {
        LOG_CDROM_DEBUG("[CDROM] Track %02u: LBA=%u audio=%d offset=%u",
                        i, disc->tracks[i].start_lba,
                        disc->tracks[i].is_audio,
                        disc->tracks[i].file_offset_bytes);
    }
    return true;
}

void cdrom_disc_unload(CdromDisc *disc) {
    close_all_track_files(disc);
    memset(disc, 0, sizeof(*disc));
}

/* =========================================================================
 * Sector Read
 * ========================================================================= */

bool cdrom_disc_read_sector(CdromDisc *disc, uint32_t lba, uint8_t *out_2352) {
    if (!disc || disc->last_track == 0) return false;

    /* Find track */
    int tnum = disc->last_track;
    for (int i = disc->first_track; i <= disc->last_track; i++) {
        if (i == disc->last_track || disc->tracks[i+1].start_lba > lba) {
            tnum = i;
            break;
        }
    }
    CdromTrack *t = &disc->tracks[tnum];
    if (!t->file) return false;

    /* Sectors inside a PREGAP are silence and are not in the BIN at all
     * (cdromfileformats.md:14929-14933). Reading them through this track's
     * mapping would hand back the beginning of the next track's data. */
    if (lba < t->start_lba) {
        memset(out_2352, 0, CDROM_RAW_SECTOR);
        return true;
    }
    uint32_t rel_sector = lba - t->start_lba;

    /* ECM track: delegate to the decoder */
    if (t->ecm)
        return ecm_read_sector(t->ecm, rel_sector, lba, out_2352);
    if (tnum < disc->last_track) {
        uint32_t this_off = t->file_offset_bytes / CDROM_RAW_SECTOR;
        uint32_t next_off = disc->tracks[tnum + 1].file_offset_bytes / CDROM_RAW_SECTOR;
        if (next_off > this_off && rel_sector >= (next_off - this_off)) {
            memset(out_2352, 0, CDROM_RAW_SECTOR);
            return true;
        }
    }
    long offset = (long)t->file_offset_bytes + (long)rel_sector * CDROM_RAW_SECTOR;

    if (fseek(t->file, offset, SEEK_SET) != 0) return false;
    if (fread(out_2352, 1, CDROM_RAW_SECTOR, t->file) != CDROM_RAW_SECTOR) return false;
    return true;
}

/* Reads the disc's licence string (system area sector 4, Mode2/Form1 user
 * data starts at raw+24, licence text at +0x20 per DOCS/cdromformat.md) and
 * returns the region letter real hardware/BIOS uses: 'A'=America, 'E'=Europe,
 * 'I'=Japan, or 0 if the sector couldn't be read. This is the actual data
 * GetID's region byte and the boot logo's SCEx trademark text are meant to
 * reflect — see DOCS/cdromdrive.md ("The 'SCEx' string is displayed in the
 * intro"). */
char cdrom_disc_detect_region(CdromDisc *disc) {
    uint8_t raw[CDROM_RAW_SECTOR];
    if (!cdrom_disc_read_sector(disc, 4, raw)) return 0;

    const char *text = (const char *)(raw + 24 + 0x20);
    const int text_len = 2048 - 0x20;
    for (int i = 0; i + 4 <= text_len; i++) {
        if (memcmp(text + i, "Euro", 4) == 0) return 'E';
        if (memcmp(text + i, "Amer", 4) == 0) return 'A';
    }
    /* No EU/US marker: Japanese licence text ends "...Entertainment Inc." with
     * no further region word, so absence of either marker means Japan. */
    return 'I';
}

/* =========================================================================
 * SubQ Generation
 * ========================================================================= */

SubQ cdrom_disc_get_subq(CdromDisc *disc, uint32_t lba) {
    SubQ q = {0};

    /* Absolute MSF is the same in every case (add the 150-frame lead-in). */
    uint32_t abs_lba = lba + 150;
    q.abs_mm_bcd = cdrom_to_bcd((uint8_t)((abs_lba / 75) / 60));
    q.abs_ss_bcd = cdrom_to_bcd((uint8_t)((abs_lba / 75) % 60));
    q.abs_ff_bcd = cdrom_to_bcd((uint8_t)(abs_lba % 75));

    /* Lead-out: track AAh, index fixed 01h, relative MSF counting up from
     * 00:00:00 (cdromformat.md:229-236). We used to report the last track and a
     * relative address that kept climbing, so nothing could tell the end of the
     * disc from the middle of the last track. */
    if (disc->total_sectors && lba >= disc->total_sectors) {
        q.control_adr = 0x41;              /* data-style ADR=1 lead-out */
        q.track_bcd   = 0xAA;
        q.index_bcd   = 0x01;
        uint32_t rel = lba - disc->total_sectors;
        q.rel_mm_bcd = cdrom_to_bcd((uint8_t)((rel / 75) / 60));
        q.rel_ss_bcd = cdrom_to_bcd((uint8_t)((rel / 75) % 60));
        q.rel_ff_bcd = cdrom_to_bcd((uint8_t)(rel % 75));
        return q;
    }

    /* Find track */
    int tnum = disc->last_track ? disc->last_track : 1;
    for (int i = disc->first_track; i <= disc->last_track; i++) {
        if (i == disc->last_track || disc->tracks[i+1].start_lba > lba) {
            tnum = i;
            break;
        }
    }
    /* A pregap belongs to the track that FOLLOWS it: subchannel Q there reports
     * the next track with index 00h and a relative address counting DOWN to its
     * start (cdromformat.md:219-226). Autopause and CD players both read this. */
    bool in_pregap = false;
    if (tnum < disc->last_track) {
        CdromTrack *nx = &disc->tracks[tnum + 1];
        if (nx->pregap_lba && lba >= nx->pregap_lba && lba < nx->start_lba) {
            tnum = tnum + 1;
            in_pregap = true;
        }
    }

    CdromTrack *t = &disc->tracks[tnum];

    /* control: 0x01=2-channel audio, 0x04=data. ADR=1 (current position) */
    q.control_adr = t->is_audio ? 0x01 : 0x41;  /* ADR=1 in nibble 0 */

    /* Relative MSF: counts up from the track start, and down through a pregap.
     * The old unconditional `lba - start_lba` underflowed to nonsense there. */
    uint32_t rel_lba;
    if (in_pregap)             rel_lba = t->start_lba - lba;
    else if (lba >= t->start_lba) rel_lba = lba - t->start_lba;
    else                       rel_lba = 0;
    q.rel_mm_bcd = cdrom_to_bcd((uint8_t)((rel_lba / 75) / 60));
    q.rel_ss_bcd = cdrom_to_bcd((uint8_t)((rel_lba / 75) % 60));
    q.rel_ff_bcd = cdrom_to_bcd((uint8_t)(rel_lba % 75));

    q.track_bcd = cdrom_to_bcd((uint8_t)tnum);
    q.index_bcd = in_pregap ? 0x00 : 0x01;   /* 00h = pause/pregap (:222) */

    return q;
}

uint8_t cdrom_disc_get_track_at_lba(CdromDisc *disc, uint32_t lba) {
    if (!disc || disc->last_track == 0) return 1;
    for (int i = disc->first_track; i <= disc->last_track; i++) {
        if (i == disc->last_track || disc->tracks[i+1].start_lba > lba)
            return (uint8_t)i;
    }
    return disc->last_track;
}

/* =========================================================================
 * Seek Timing
 * ========================================================================= */

uint32_t cdrom_disc_get_seek_ticks(uint32_t from_lba, uint32_t to_lba) {
    if (from_lba == to_lba) return CDROM_SEEK_FAST_DELAY;
    uint32_t dist = from_lba > to_lba ? from_lba - to_lba : to_lba - from_lba;
    /* Two regimes. A short move steps the head track by track and costs roughly
     * 7ms per sector; past a handful of sectors the drive switches to a coarse
     * seek whose cost is nearly flat over a wide range (~51ms was observed for
     * anything between 147 and 164 sectors), plus a proportional term for
     * genuinely long moves.
     *
     * DOCS/cdromdrive.md:1896-1908 states outright that the seek timings are
     * undocumented and "probably quite complicated" — the drive splits the
     * distance into coarse and fine steps and the data density varies along the
     * spiral. So these two slopes are calibration against observed drive
     * behaviour, not a documented curve. What they replace was a single flat
     * step: everything up to 10 sectors cost the same 53ms, which made a
     * one-sector nudge as expensive as a 10-sector move and a 2-sector move
     * 4x too expensive. */
    uint64_t fine   = (uint64_t)dist * CDROM_SEEK_FINE_PER_LBA;
    uint32_t extra  = (dist * CDROM_SECTOR_TIME) / 1000u;
    uint32_t cap    = CDROM_SECTOR_TIME * 60u;
    uint32_t coarse = CDROM_SEEK_DELAY + (extra < cap ? extra : cap);
    return fine < (uint64_t)coarse ? (uint32_t)fine : coarse;
}

/* =========================================================================
 * Async Reader Thread
 * ========================================================================= */

static void *async_reader_thread(void *arg) {
    CdromAsyncReader *r = (CdromAsyncReader *)arg;

    for (;;) {
        pthread_mutex_lock(&r->mutex);
        while (!r->has_request && !r->shutdown)
            pthread_cond_wait(&r->cond_req, &r->mutex);
        if (r->shutdown) { pthread_mutex_unlock(&r->mutex); break; }

        uint32_t lba = r->requested_lba;
        r->has_request = false;
        r->busy        = true;
        pthread_mutex_unlock(&r->mutex);

        /* Into a local: r->sector is shared, and filling it with the mutex
         * released let the consumer memcpy a half-written sector. */
        uint8_t tmp[CDROM_RAW_SECTOR];
        bool ok = cdrom_disc_read_sector(r->disc, lba, tmp);

        pthread_mutex_lock(&r->mutex);
        memcpy(r->sector, tmp, CDROM_RAW_SECTOR);
        r->ready_lba    = lba;
        r->read_ok      = ok;
        r->sector_ready = true;
        r->busy         = false;
        pthread_cond_signal(&r->cond_done);
        pthread_mutex_unlock(&r->mutex);
    }
    return NULL;
}

void cdrom_async_reader_init(CdromAsyncReader *r, CdromDisc *disc) {
    memset(r, 0, sizeof(*r));
    r->disc = disc;
    pthread_mutex_init(&r->mutex, NULL);
    pthread_cond_init(&r->cond_req, NULL);
    pthread_cond_init(&r->cond_done, NULL);
    pthread_create(&r->thread, NULL, async_reader_thread, r);
}

void cdrom_async_reader_shutdown(CdromAsyncReader *r) {
    pthread_mutex_lock(&r->mutex);
    r->shutdown = true;
    pthread_cond_signal(&r->cond_req);
    pthread_mutex_unlock(&r->mutex);
    pthread_join(r->thread, NULL);
    pthread_mutex_destroy(&r->mutex);
    pthread_cond_destroy(&r->cond_req);
    pthread_cond_destroy(&r->cond_done);
    memset(r, 0, sizeof(*r));
}

void cdrom_async_reader_queue(CdromAsyncReader *r, uint32_t lba) {
    pthread_mutex_lock(&r->mutex);
    r->requested_lba = lba;
    r->has_request   = true;
    r->sector_ready  = false;
    pthread_cond_signal(&r->cond_req);
    pthread_mutex_unlock(&r->mutex);
}

CdromSectorStatus cdrom_async_reader_poll(CdromAsyncReader *r, uint8_t *out_sector,
                                          uint32_t want_lba) {
    pthread_mutex_lock(&r->mutex);

    if (r->shutdown) { pthread_mutex_unlock(&r->mutex); return CDROM_SECTOR_FAILED; }

    if (r->sector_ready) {
        if (r->ready_lba == want_lba) {
            bool ok = r->read_ok;
            if (ok) memcpy(out_sector, r->sector, CDROM_RAW_SECTOR);
            r->sector_ready = false;
            pthread_mutex_unlock(&r->mutex);
            return ok ? CDROM_SECTOR_READY : CDROM_SECTOR_FAILED;
        }
        r->sector_ready = false;   /* a sector we no longer want */
    }

    /* Nothing in flight for what we want, so ask. Covers both a queue() that
     * was overwritten by a later one and a caller that never queued. */
    if (!r->busy && !r->has_request) {
        r->requested_lba = want_lba;
        r->has_request   = true;
        pthread_cond_signal(&r->cond_req);
    }
    pthread_mutex_unlock(&r->mutex);
    return CDROM_SECTOR_PENDING;
}
