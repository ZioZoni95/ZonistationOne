/* SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 ZioZoni95
 *
 * Part of ZoniStation One, a PlayStation 1 emulator.
 * See LICENSE for the full licence text and THIRD-PARTY.md for the
 * components of this project that have other authors.
 */
#ifndef CDROM_DISC_H
#define CDROM_DISC_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <pthread.h>

/* Forward declaration — full definition in cdrom_ecm.h */
typedef struct EcmDecoder EcmDecoder;

#define PSX_SYSCLK_HZ         33868800U
#define CDROM_RAW_SECTOR      2352
#define CDROM_SEEK_MIN_DELAY  30000

typedef struct {
    uint8_t  number;
    bool     is_audio;
    uint32_t start_lba;         /* PSX LBA of first sector */
    uint32_t pregap_lba;        /* PSX LBA of INDEX 00, 0 if absent */
    FILE    *file;               /* open BIN file handle (may be shared) */
    uint32_t file_offset_bytes; /* byte offset in file for sector start_lba */
    EcmDecoder *ecm;            /* non-NULL if this track is an ECM image */
} CdromTrack;

/* One LibCrypt patch: the subchannel Q a protected sector really carries on
 * the pressed disc, which a 2352-byte dump cannot hold because Q lives outside
 * the sector data. An .sbi file is a list of these
 * (DOCS/cdromformat.md describes the Q layout; the container itself is the
 * de-facto format every emulator reads). */
typedef struct {
    uint32_t lba;      /* absolute LBA the entry replaces the Q of */
    uint8_t  q[10];    /* control/adr, track, index, rel MSF, zero, abs MSF */
} SbiEntry;

typedef struct {
    uint8_t    first_track;
    uint8_t    last_track;
    uint32_t   total_sectors;
    CdromTrack tracks[100];
    SbiEntry  *sbi;             /* NULL when the disc needs no patches */
    uint32_t   sbi_count;
} CdromDisc;

typedef struct {
    uint8_t control_adr;
    uint8_t track_bcd;
    uint8_t index_bcd;
    uint8_t rel_mm_bcd;
    uint8_t rel_ss_bcd;
    uint8_t rel_ff_bcd;
    uint8_t reserved;
    uint8_t abs_mm_bcd;
    uint8_t abs_ss_bcd;
    uint8_t abs_ff_bcd;
} SubQ;

typedef struct {
    pthread_t       thread;
    pthread_mutex_t mutex;
    pthread_cond_t  cond_req;
    pthread_cond_t  cond_done;
    uint32_t        requested_lba;
    bool            has_request;
    bool            sector_ready;
    bool            shutdown;
    /* Which sector the buffer actually holds, and whether reading it worked.
     * Without the tag there was nothing tying the published buffer to an LBA:
     * queueing sector N+1 while the reader was still fetching N made the reader
     * publish N as ready, and the consumer took it as N+1. */
    uint32_t        ready_lba;
    bool            read_ok;
    bool            busy;           /* a read is in flight */
    uint8_t         sector[CDROM_RAW_SECTOR];
    CdromDisc      *disc;
} CdromAsyncReader;

/* Disc API */
bool     cdrom_disc_load(CdromDisc *disc, const char *cue_path);
void     cdrom_disc_unload(CdromDisc *disc);
bool     cdrom_disc_read_sector(CdromDisc *disc, uint32_t lba, uint8_t *out_2352);
char     cdrom_disc_detect_region(CdromDisc *disc);
SubQ     cdrom_disc_get_subq(CdromDisc *disc, uint32_t lba);
bool     cdrom_disc_load_sbi(CdromDisc *disc, const char *sbi_path);
bool     cdrom_disc_sbi_covers(const CdromDisc *disc, uint32_t lba);
uint8_t  cdrom_disc_get_track_at_lba(CdromDisc *disc, uint32_t lba);
uint32_t cdrom_disc_get_seek_ticks(uint32_t from_lba, uint32_t to_lba);

/* Async reader API */
void cdrom_async_reader_init(CdromAsyncReader *r, CdromDisc *disc);
void cdrom_async_reader_shutdown(CdromAsyncReader *r);
void cdrom_async_reader_queue(CdromAsyncReader *r, uint32_t lba);
typedef enum {
    CDROM_SECTOR_READY,     /* out_sector filled with want_lba */
    CDROM_SECTOR_FAILED,    /* that LBA could not be read */
    CDROM_SECTOR_PENDING    /* not here yet — ask again shortly */
} CdromSectorStatus;

/* Non-blocking. Returns PENDING rather than waiting, and issues the request
 * itself if nothing is in flight for want_lba. The emulation thread must never
 * block on disc I/O: while it is stopped no VBlank fires and the audio ring
 * drains, so a cold read shows up as a dropped frame and an audible gap. */
CdromSectorStatus cdrom_async_reader_poll(CdromAsyncReader *r, uint8_t *out_sector,
                                          uint32_t want_lba);

static inline uint8_t cdrom_to_bcd(uint8_t v)  { return (uint8_t)(((v/10)<<4)|(v%10)); }
static inline uint8_t cdrom_from_bcd(uint8_t b) { return (uint8_t)(((b>>4)*10)+(b&0xF)); }

/* Convert PSX LBA → MM:SS:FF (adds 150-frame lead-in for CD addressing) */
static inline void cdrom_lba_to_msf(uint32_t lba, uint8_t *mm, uint8_t *ss, uint8_t *ff) {
    uint32_t abs = lba + 150;
    *ff = (uint8_t)(abs % 75);
    *ss = (uint8_t)((abs / 75) % 60);
    *mm = (uint8_t)((abs / 75) / 60);
}

#endif /* CDROM_DISC_H */
