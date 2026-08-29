/* SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 ZioZoni95
 *
 * Part of ZoniStation One, a PlayStation 1 emulator.
 * See LICENSE for the full licence text and THIRD-PARTY.md for the
 * components of this project that have other authors.
 */
/*
 * ECM decoder: random-access decoding of ECM disc images.
 *
 * The LUT maps each decoded sector number to the file position of the
 * type/length byte that starts its block. Multi-sector blocks (Type 1-3
 * with count > 1) share one type/length byte; the decoder for a sector
 * in the middle of such a block skips earlier sectors in the block.
 *
 * Written from the ECM format specification. No code copied from any
 * third-party emulator or ECM tool.
 */

#include "cdrom_ecm.h"
#include "ecm_edc.h"
#include "log.h"
#include <stdlib.h>
#include <string.h>

/* Compressed data size per sector type (bytes read from the ECM file).
 * Type 0 = 1 byte per literal byte, handled separately. */
static const uint32_t ECM_SECTOR_COMP_SIZE[4] = {
    0,      /* Type 0: literal, variable */
    0x803,  /* Type 1 (Mode 1):       3 addr + 2048 data (mode byte hardcoded) */
    0x804,  /* Type 2 (Mode 2 F1):    4 subheader + 2048 data */
    0x918   /* Type 3 (Mode 2 F2):    4 subheader + 2324 data */
};

/* ---- helpers ---- */

/* Read the variable-length type/count header.
 * Returns the number of bytes consumed (1-5), or 0 on EOF/error.
 * Sets *type (0-3).
 * Sets *raw_num to the encoded count (before +1). 0xFFFFFFFF = end marker.
 * Sets *count to the actual count (raw_num + 1). */
static int read_type_count(FILE *fp, int *type, uint32_t *raw_num, uint32_t *count) {
    int c = fgetc(fp);
    if (c == EOF) return 0;

    *type   = c & 3;
    uint32_t num = (uint32_t)((c >> 2) & 0x1F);
    int bits = 5;
    int bytes = 1;

    while (c & 0x80) {
        c = fgetc(fp);
        if (c == EOF) return 0;
        num |= ((uint32_t)(c & 0x7F)) << bits;
        bits += 7;
        bytes++;
    }

    *raw_num = num;
    *count   = num + 1;
    return bytes;
}

/* ---- run index ---- */

static bool runs_push(EcmDecoder *dec, uint8_t type, uint32_t file_pos,
                      uint32_t count, uint32_t out_len) {
    if (dec->run_count == dec->run_capacity) {
        uint32_t cap = dec->run_capacity ? dec->run_capacity * 2 : 4096;
        EcmRun *grown = realloc(dec->runs, (size_t)cap * sizeof(EcmRun));
        if (!grown) return false;
        dec->runs = grown;
        dec->run_capacity = cap;
    }
    EcmRun *r = &dec->runs[dec->run_count++];
    r->out_start = dec->out_bytes;
    r->out_len   = out_len;
    r->file_pos  = file_pos;
    r->count     = count;
    r->type      = type;
    dec->out_bytes += out_len;
    return true;
}

/* Scan the container once and record every block as a run of output bytes. */
static bool build_runs(EcmDecoder *dec) {
    FILE *fp = dec->fp;
    fseek(fp, ECM_HEADER_SIZE, SEEK_SET);

    for (;;) {
        long block_start = ftell(fp);
        if (block_start < 0) break;

        int c = fgetc(fp);
        if (c == EOF) break;
        fseek(fp, block_start, SEEK_SET);

        int type;
        uint32_t raw_num, count;
        int hdr_bytes = read_type_count(fp, &type, &raw_num, &count);
        if (hdr_bytes == 0) break;

        /* End marker, followed by the EDC of the whole decoded output. */
        if (raw_num == 0xFFFFFFFFu) break;

        uint32_t payload = (uint32_t)(block_start + hdr_bytes);

        if (type == 0) {
            if (!runs_push(dec, 0, payload, count, count)) return false;
            fseek(fp, (long)count, SEEK_CUR);
        } else {
            uint32_t comp_size = ECM_SECTOR_COMP_SIZE[type];
            if (!runs_push(dec, (uint8_t)type, payload, count, count * ECM_MODELLED_PAYLOAD))
                return false;
            fseek(fp, (long)comp_size * (long)count, SEEK_CUR);
        }
    }

    dec->total_sectors = (uint32_t)(dec->out_bytes / 2352u);
    if (dec->out_bytes % 2352u)
        LOG_CDROM_WARN("[ECM] Image is %llu bytes — not a whole number of sectors",
                       (unsigned long long)dec->out_bytes);
    return dec->total_sectors > 0;
}

/* The run covering an output offset. */
static const EcmRun *find_run(const EcmDecoder *dec, uint64_t out_off) {
    uint32_t lo = 0, hi = dec->run_count;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        const EcmRun *r = &dec->runs[mid];
        if (out_off < r->out_start)                     hi = mid;
        else if (out_off >= r->out_start + r->out_len)  lo = mid + 1;
        else return r;
    }
    return NULL;
}

/* ---- public API ---- */

bool ecm_detect(FILE *fp) {
    if (!fp) return false;
    fseek(fp, 0, SEEK_SET);
    uint8_t hdr[4];
    if (fread(hdr, 1, 4, fp) != 4) return false;
    fseek(fp, 0, SEEK_SET);
    return (hdr[0] == 'E' && hdr[1] == 'C' && hdr[2] == 'M' && hdr[3] == 0x00);
}

EcmDecoder *ecm_decoder_open(FILE *fp) {
    if (!fp) return NULL;
    if (!ecm_detect(fp)) {
        LOG_CDROM_ERROR("[ECM] Not an ECM file");
        return NULL;
    }

    ecm_edc_init();

    EcmDecoder *dec = calloc(1, sizeof(*dec));
    if (!dec) return NULL;
    dec->fp = fp;

    if (!build_runs(dec)) {
        LOG_CDROM_ERROR("[ECM] Index build failed: no sectors found");
        free(dec->runs);
        free(dec);
        return NULL;
    }

    LOG_CDROM_INFO("[ECM] Decoded: %u sectors (%u blocks)", dec->total_sectors, dec->run_count);
    return dec;
}

void ecm_decoder_free(EcmDecoder *dec) {
    if (!dec) return;
    free(dec->runs);
    free(dec);
}

static inline uint8_t ecm_to_bcd(uint8_t val) {
    return ((val / 10) << 4) | (val % 10);
}

/* Rebuild one modelled sector into `sector` (all 2352 bytes; the caller emits
 * the 2336 that follow the header). The MSF is reconstructed from the LBA
 * because the format strips it, and Mode 1's ECC covers the header, so it has
 * to be right even though the header bytes themselves come from the literal. */
static bool decode_modelled(EcmDecoder *dec, const EcmRun *run, uint32_t index_in_run,
                            uint32_t abs_lba, uint8_t *sector) {
    FILE *fp = dec->fp;
    uint32_t comp_size = ECM_SECTOR_COMP_SIZE[run->type];
    if (fseek(fp, (long)run->file_pos + (long)comp_size * (long)index_in_run, SEEK_SET) != 0)
        return false;

    memset(sector, 0, 2352);
    sector[0] = 0x00;
    memset(sector + 1, 0xFF, 10);
    sector[11] = 0x00;

    if (run->type == 1) {
        if (fread(sector + 0x0C, 1, 3, fp) != 3) return false;
        sector[0x0F] = 0x01;
        if (fread(sector + 0x10, 1, 0x800, fp) != 0x800) return false;
        ecm_edc_generate(sector, 1);
        return true;
    }

    uint32_t msf_lba = abs_lba + 150;
    sector[0x0C] = ecm_to_bcd((uint8_t)((msf_lba / 75) / 60));
    sector[0x0D] = ecm_to_bcd((uint8_t)((msf_lba / 75) % 60));
    sector[0x0E] = ecm_to_bcd((uint8_t)(msf_lba % 75));
    sector[0x0F] = 0x02;

    if (fread(sector + 0x10, 1, 4, fp) != 4) return false;
    memcpy(sector + 0x14, sector + 0x10, 4);      /* the subheader is duplicated */

    size_t data = (run->type == 2) ? 0x800 : 0x914;
    if (fread(sector + 0x18, 1, data, fp) != data) return false;
    ecm_edc_generate(sector, (run->type == 2) ? 2 : 3);
    return true;
}

bool ecm_read_sector(EcmDecoder *dec, uint32_t rel_sector, uint32_t abs_lba, uint8_t *out_2352) {
    if (!dec || !out_2352 || rel_sector >= dec->total_sectors) return false;

    uint64_t out_off   = (uint64_t)rel_sector * 2352u;
    uint32_t remaining = 2352;
    uint8_t *dst = out_2352;

    while (remaining > 0) {
        const EcmRun *run = find_run(dec, out_off);
        if (!run) return false;

        uint32_t in_run = (uint32_t)(out_off - run->out_start);
        uint32_t chunk  = run->out_len - in_run;
        if (chunk > remaining) chunk = remaining;

        if (run->type == 0) {
            if (fseek(dec->fp, (long)run->file_pos + (long)in_run, SEEK_SET) != 0) return false;
            if (fread(dst, 1, chunk, dec->fp) != chunk) return false;
        } else {
            uint32_t index_in_run = in_run / ECM_MODELLED_PAYLOAD;
            uint32_t off_in_sec   = in_run % ECM_MODELLED_PAYLOAD;
            if (chunk > ECM_MODELLED_PAYLOAD - off_in_sec)
                chunk = ECM_MODELLED_PAYLOAD - off_in_sec;

            /* This sector's own LBA, from where its output lands. */
            uint64_t sec_out = run->out_start + (uint64_t)index_in_run * ECM_MODELLED_PAYLOAD;
            uint32_t sec_lba = abs_lba + (uint32_t)(sec_out / 2352u) - rel_sector;

            uint8_t sector[2352];
            if (!decode_modelled(dec, run, index_in_run, sec_lba, sector)) return false;
            memcpy(dst, sector + 16 + off_in_sec, chunk);
        }

        dst       += chunk;
        out_off   += chunk;
        remaining -= chunk;
    }
    return true;
}
