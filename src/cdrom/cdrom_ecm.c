/* SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 ZioZoni95
 *
 * Part of ZoniStation One, a PlayStation 1 emulator.
 * See LICENSE for the full licence text and THIRD-PARTY.md for the
 * components of this project that have other authors.
 */
/*
 * ECM decoder: LUT-based random-access decoding of ECM disc images.
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

/* ---- LUT builder ---- */

/*
 * Scan the entire ECM stream and build the LUT.
 * For each Type 1/2/3 sector, records the file position of its block's
 * type/length byte. Type 0 literal bytes are skipped (they are not sectors).
 *
 * Returns true on success. On failure, the LUT is partially populated
 * and total_sectors reflects what was decoded before the error.
 */
static bool build_lut(EcmDecoder *dec) {
    FILE *fp = dec->fp;
    fseek(fp, ECM_HEADER_SIZE, SEEK_SET);

    uint32_t sector_idx = 0;

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

        /* End marker: raw encoded value 0xFFFFFFFF */
        if (raw_num == 0xFFFFFFFF) {
            /* Skip the 4-byte EDC at the end of the file */
            fseek(fp, 4, SEEK_CUR);
            break;
        }

        if (type == 0) {
            /* Literal bytes — skip, they are not sectors */
            fseek(fp, (long)count, SEEK_CUR);
        } else if (type >= 1 && type <= 3) {
            uint32_t comp_size = ECM_SECTOR_COMP_SIZE[type];
            for (uint32_t i = 0; i < count; i++) {
                if (sector_idx >= ECM_MAX_SECTORS) {
                    LOG_CDROM_WARN("[ECM] LUT full at sector %u", sector_idx);
                    dec->total_sectors = sector_idx;
                    return true;
                }
                dec->lut[sector_idx].file_pos = (uint32_t)block_start;
                sector_idx++;
                fseek(fp, (long)comp_size, SEEK_CUR);
            }
        } else {
            /* Unknown type — treat as literal and skip */
            fseek(fp, (long)count, SEEK_CUR);
        }
    }

    dec->total_sectors = sector_idx;
    return sector_idx > 0;
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
    dec->lut = calloc(ECM_MAX_SECTORS, sizeof(EcmLutEntry));
    if (!dec->lut) {
        free(dec);
        return NULL;
    }

    if (!build_lut(dec)) {
        LOG_CDROM_ERROR("[ECM] LUT build failed: no sectors found");
        free(dec->lut);
        free(dec);
        return NULL;
    }

    LOG_CDROM_INFO("[ECM] Decoded: %u sectors", dec->total_sectors);
    return dec;
}

void ecm_decoder_free(EcmDecoder *dec) {
    if (!dec) return;
    free(dec->lut);
    free(dec);
}

static inline uint8_t ecm_to_bcd(uint8_t val) {
    return ((val / 10) << 4) | (val % 10);
}

bool ecm_read_sector(EcmDecoder *dec, uint32_t rel_sector, uint32_t abs_lba, uint8_t *out_2352) {
    if (!dec || rel_sector >= dec->total_sectors) return false;

    FILE *fp = dec->fp;
    uint32_t file_pos = dec->lut[rel_sector].file_pos;

    /* We are positioned at the type/length byte of the block that contains
     * this sector. If this sector is not the first in a multi-sector block,
     * we must skip earlier sectors. We re-read the header to find the type,
     * then skip ahead. */

    fseek(fp, (long)file_pos, SEEK_SET);

    int type;
    uint32_t raw_num, count;
    int hdr_bytes = read_type_count(fp, &type, &raw_num, &count);
    if (hdr_bytes == 0) return false;

    if (type == 0) {
        /* Type 0 literal — the LUT should not have pointed here for a
         * normal sector read. This means the ECM image has literal data
         * where a sector was expected. Return zeroes. */
        memset(out_2352, 0, 2352);
        return true;
    }

    /* We need to figure out which sector within this block is our target.
     * We know lba, but not the block's starting sector. We must count
     * backwards from the block start by decoding the header, or we can
     * simply read forward from the block start, decoding each sector
     * until we reach the one we want.
     *
     * Efficient approach: the LUT for the FIRST sector in this block
     * would have the same file_pos. We can find our offset within the
     * block by scanning backward through the LUT until file_pos changes.
     */
    uint32_t block_first = rel_sector;
    while (block_first > 0 && dec->lut[block_first - 1].file_pos == file_pos)
        block_first--;

    uint32_t skip_sectors = rel_sector - block_first;

    /* Position after the type/length header */
    fseek(fp, (long)file_pos + hdr_bytes, SEEK_SET);

    /* Skip sectors we don't want */
    uint32_t comp_size = ECM_SECTOR_COMP_SIZE[type];
    if (skip_sectors > 0)
        fseek(fp, (long)comp_size * skip_sectors, SEEK_CUR);

    /* Now read and decode this sector */
    uint8_t sector[2352];
    memset(sector, 0, sizeof(sector));

    switch (type) {
    case 1: {
        /* Mode 1: ECM compressed = 3 addr + 2048 data (mode byte NOT in stream) */
        sector[0]  = 0x00;
        memset(sector + 1, 0xFF, 10);
        sector[11] = 0x00;
        if (fread(sector + 0x0C, 1, 3, fp) != 3) return false;
        sector[0x0F] = 0x01;
        if (fread(sector + 0x10, 1, 0x800, fp) != 0x800) return false;
        ecm_edc_generate(sector, 1);
        break;
    }
    case 2: {
        /* Mode 2 Form 1: ECM compressed = 4 subheader + 2048 data */
        sector[0]  = 0x00;
        memset(sector + 1, 0xFF, 10);
        sector[11] = 0x00;
        
        uint32_t msf_lba = abs_lba + 150;
        sector[0x0C] = ecm_to_bcd((uint8_t)((msf_lba / 75) / 60));
        sector[0x0D] = ecm_to_bcd((uint8_t)((msf_lba / 75) % 60));
        sector[0x0E] = ecm_to_bcd((uint8_t)(msf_lba % 75));

        sector[0x0F] = 0x02;
        if (fread(sector + 0x10, 1, 4, fp) != 4) return false;
        sector[0x14] = sector[0x10];
        sector[0x15] = sector[0x11];
        sector[0x16] = sector[0x12];
        sector[0x17] = sector[0x13];
        if (fread(sector + 0x18, 1, 0x800, fp) != 0x800) return false;
        ecm_edc_generate(sector, 2);
        break;
    }
    case 3: {
        /* Mode 2 Form 2: ECM compressed = 4 subheader + 2324 data */
        sector[0]  = 0x00;
        memset(sector + 1, 0xFF, 10);
        sector[11] = 0x00;
        
        uint32_t msf_lba = abs_lba + 150;
        sector[0x0C] = ecm_to_bcd((uint8_t)((msf_lba / 75) / 60));
        sector[0x0D] = ecm_to_bcd((uint8_t)((msf_lba / 75) % 60));
        sector[0x0E] = ecm_to_bcd((uint8_t)(msf_lba % 75));

        sector[0x0F] = 0x02;
        if (fread(sector + 0x10, 1, 4, fp) != 4) return false;
        sector[0x14] = sector[0x10];
        sector[0x15] = sector[0x11];
        sector[0x16] = sector[0x12];
        sector[0x17] = sector[0x13];
        if (fread(sector + 0x18, 1, 0x914, fp) != 0x914) return false;
        ecm_edc_generate(sector, 3);
        break;
    }
    default:
        return false;
    }

    memcpy(out_2352, sector, 2352);
    return true;
}
