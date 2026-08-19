/* SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 ZioZoni95
 *
 * Part of ZoniStation One, a PlayStation 1 emulator.
 * See LICENSE for the full licence text and THIRD-PARTY.md for the
 * components of this project that have other authors.
 */
/*
 * ECM (Error Code Modeler) decoder for CD-ROM disc images.
 *
 * Decodes .bin.ecm files on the fly, reconstructing the full 2352-byte
 * raw sectors that the CDROM layer expects. A lookup table built at load
 * time maps decoded sector numbers to file positions for O(1) random
 * access.
 *
 * Format reference: kidoz/ecm/doc/FORMAT.md (Neill Corlett, public).
 * Algorithm: decoded entirely from the format specification; no code
 * copied from any third-party emulator or ECM tool.
 */
#ifndef CDROM_ECM_H
#define CDROM_ECM_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#define ECM_HEADER_SIZE 4  /* "ECM\0" */

/* Maximum sectors for an 80-minute CD (75 frames/s × 60 s × 80 min) */
#define ECM_MAX_SECTORS (75 * 60 * 80)

/* Per-sector entry in the lookup table.
 * file_pos: byte offset in the ECM file where this sector's data starts
 *           (the type/length byte of the block containing this sector).
 * The type/length bytes are NOT included — file_pos points to the first
 * data byte of the block. For Type 0 this is the literal data; for
 * Type 1-3 this is the ADDR/FLAGS/DATA payload. */
typedef struct {
    uint32_t file_pos;
} EcmLutEntry;

typedef struct EcmDecoder {
    FILE        *fp;            /* open file handle (caller owns close) */
    EcmLutEntry *lut;           /* LUT[decoded_sector] = file position */
    uint32_t     total_sectors; /* number of decoded sectors */
} EcmDecoder;

/*
 * Detect and open an ECM file.
 * Reads the magic header, builds the LUT by scanning the stream,
 * and returns a fully initialised decoder. Returns NULL on failure.
 * The caller is responsible for calling ecm_decoder_free() when done.
 *
 * After this call, fp is positioned at the start of the ECM data
 * (ready for sector reads).
 */
EcmDecoder *ecm_decoder_open(FILE *fp);

/*
 * Free all resources associated with an ECM decoder.
 * Does NOT close the file handle (caller manages that).
 */
void ecm_decoder_free(EcmDecoder *dec);

/*
 * Read one decoded 2352-byte sector.
 * rel_sector must be < dec->total_sectors.
 * abs_lba is the absolute disc LBA to reconstruct MSF.
 * Returns true on success, false on I/O error.
 */
bool ecm_read_sector(EcmDecoder *dec, uint32_t rel_sector, uint32_t abs_lba, uint8_t *out_2352);

/*
 * Quick check: read 4 bytes and return true if they match "ECM\0".
 * fp is rewound to the start afterwards.
 */
bool ecm_detect(FILE *fp);

#endif /* CDROM_ECM_H */
