/*
 * CDROM disc layer: CUE/BIN parsing, sector read, SubQ generation,
 * seek timing, and async reader thread.
 */

#include "cdrom_disc.h"
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
    FILE *prev = NULL;
    for (int i = 1; i <= disc->last_track; i++) {
        if (disc->tracks[i].file && disc->tracks[i].file != prev) {
            fclose(disc->tracks[i].file);
            prev = disc->tracks[i].file;
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

    /* Accept a bare .bin path for quick testing */
    size_t plen = strlen(cue_path);
    if (plen >= 4 && strcmp(cue_path + plen - 4, ".bin") == 0) {
        FILE *bin = fopen(cue_path, "rb");
        if (!bin) { LOG_CDROM_ERROR("[CDROM] Cannot open BIN: %s", cue_path); return false; }
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
    uint8_t cur_track   = 0;
    bool    multi_file  = false;
    uint32_t prev_file_end_lba = 0; /* cumulative LBA for multi-file discs */

    /* Per-track raw INDEX 01 MSF (within the current BIN file) */
    uint32_t idx01_lba[100] = {0};  /* file-relative LBA for each track */
    uint32_t idx00_lba[100] = {0};  /* pregap */
    FILE    *track_file[100] = {0};

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
            LOG_CDROM_DEBUG("[CDROM] File: %s", full_path);

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
        /* Single BIN: INDEX 01 MSF is the file-relative sector number */
        for (int i = disc->first_track; i <= disc->last_track; i++) {
            disc->tracks[i].file              = track_file[i] ? track_file[i]
                                                               : track_file[disc->first_track];
            disc->tracks[i].start_lba         = idx01_lba[i];
            disc->tracks[i].pregap_lba        = idx00_lba[i];
            disc->tracks[i].file_offset_bytes = idx01_lba[i] * CDROM_RAW_SECTOR;
        }
        /* total_sectors from the shared BIN file */
        FILE *f = disc->tracks[disc->first_track].file;
        if (f) {
            fseek(f, 0, SEEK_END);
            disc->total_sectors = (uint32_t)(ftell(f) / CDROM_RAW_SECTOR);
            fseek(f, 0, SEEK_SET);
        }
    } else {
        /* Multi-file: each track starts where the previous ended */
        uint32_t accum_lba = 0;
        for (int i = disc->first_track; i <= disc->last_track; i++) {
            disc->tracks[i].file              = track_file[i];
            disc->tracks[i].start_lba         = accum_lba;
            disc->tracks[i].pregap_lba        = (idx00_lba[i] > 0)
                                                ? accum_lba - (idx01_lba[i] - idx00_lba[i])
                                                : 0;
            disc->tracks[i].file_offset_bytes = 0;
            if (track_file[i]) {
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

    uint32_t rel_sector = lba - t->start_lba;
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

    /* Find track */
    int tnum = disc->last_track ? disc->last_track : 1;
    for (int i = disc->first_track; i <= disc->last_track; i++) {
        if (i == disc->last_track || disc->tracks[i+1].start_lba > lba) {
            tnum = i;
            break;
        }
    }

    CdromTrack *t = &disc->tracks[tnum];

    /* control: 0x01=2-channel audio, 0x04=data. ADR=1 (current position) */
    q.control_adr = t->is_audio ? 0x01 : 0x41;  /* ADR=1 in nibble 0 */

    /* Absolute MSF (add 150-frame lead-in) */
    uint32_t abs_lba = lba + 150;
    uint8_t abs_mm = (uint8_t)((abs_lba / 75) / 60);
    uint8_t abs_ss = (uint8_t)((abs_lba / 75) % 60);
    uint8_t abs_ff = (uint8_t)(abs_lba % 75);
    q.abs_mm_bcd = cdrom_to_bcd(abs_mm);
    q.abs_ss_bcd = cdrom_to_bcd(abs_ss);
    q.abs_ff_bcd = cdrom_to_bcd(abs_ff);

    /* Relative MSF (relative to track start) */
    uint32_t rel_lba  = lba - t->start_lba;
    uint8_t rel_mm = (uint8_t)((rel_lba / 75) / 60);
    uint8_t rel_ss = (uint8_t)((rel_lba / 75) % 60);
    uint8_t rel_ff = (uint8_t)(rel_lba % 75);
    q.rel_mm_bcd = cdrom_to_bcd(rel_mm);
    q.rel_ss_bcd = cdrom_to_bcd(rel_ss);
    q.rel_ff_bcd = cdrom_to_bcd(rel_ff);

    q.track_bcd = cdrom_to_bcd((uint8_t)tnum);
    q.index_bcd = 0x01;  /* always INDEX 01 (simplification) */

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
    /* Tier-based curve matching pcsx-redux hardware tests.
     * Short: cdReadTime*4 base. Long: add proportional term, cap at ~60s. */
    uint32_t base = CDROM_SEEK_DELAY;
    if (dist <= 10) return base;
    uint32_t extra = (dist * CDROM_SECTOR_TIME) / 1000u;
    uint32_t cap   = CDROM_SECTOR_TIME * 60u;
    return base + (extra < cap ? extra : cap);
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
        pthread_mutex_unlock(&r->mutex);

        bool ok = cdrom_disc_read_sector(r->disc, lba, r->sector);

        pthread_mutex_lock(&r->mutex);
        r->sector_ready = ok;
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

bool cdrom_async_reader_wait(CdromAsyncReader *r, uint8_t *out_sector) {
    pthread_mutex_lock(&r->mutex);
    while (!r->sector_ready && !r->shutdown)
        pthread_cond_wait(&r->cond_done, &r->mutex);
    bool ok = r->sector_ready;
    if (ok) memcpy(out_sector, r->sector, CDROM_RAW_SECTOR);
    r->sector_ready = false;
    pthread_mutex_unlock(&r->mutex);
    return ok;
}
