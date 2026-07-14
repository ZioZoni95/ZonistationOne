#ifndef CDROM_DISC_H
#define CDROM_DISC_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <pthread.h>

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
} CdromTrack;

typedef struct {
    uint8_t    first_track;
    uint8_t    last_track;
    uint32_t   total_sectors;
    CdromTrack tracks[100];
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
    uint8_t         sector[CDROM_RAW_SECTOR];
    CdromDisc      *disc;
} CdromAsyncReader;

/* Disc API */
bool     cdrom_disc_load(CdromDisc *disc, const char *cue_path);
void     cdrom_disc_unload(CdromDisc *disc);
bool     cdrom_disc_read_sector(CdromDisc *disc, uint32_t lba, uint8_t *out_2352);
char     cdrom_disc_detect_region(CdromDisc *disc);
SubQ     cdrom_disc_get_subq(CdromDisc *disc, uint32_t lba);
uint8_t  cdrom_disc_get_track_at_lba(CdromDisc *disc, uint32_t lba);
uint32_t cdrom_disc_get_seek_ticks(uint32_t from_lba, uint32_t to_lba);

/* Async reader API */
void cdrom_async_reader_init(CdromAsyncReader *r, CdromDisc *disc);
void cdrom_async_reader_shutdown(CdromAsyncReader *r);
void cdrom_async_reader_queue(CdromAsyncReader *r, uint32_t lba);
bool cdrom_async_reader_wait(CdromAsyncReader *r, uint8_t *out_sector);

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
