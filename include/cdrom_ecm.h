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

/* Bytes a modelled block contributes: the sector minus the 12-byte sync and
 * the 4-byte header, which the container stores as a literal block just before
 * it. */
#define ECM_MODELLED_PAYLOAD 2336u

/* One run of the reconstructed image.
 *
 * The container alternates two kinds of block. Type 0 is literal bytes copied
 * to the output as they are; types 1-3 are sectors whose error codes were
 * stripped and are regenerated here. Both produce output, so the index is
 * built over *output byte offsets* rather than over a sector count.
 *
 * Every modelled sector is preceded by a 16-byte literal carrying its sync and
 * header, so a modelled block contributes the remaining 2336 bytes. Ignoring
 * the literals entirely still reconstructs a disc whose sectors are all
 * modelled — which is why Crash 3 came out byte-exact — but a disc that stores
 * whole sectors literally loses them: Dino Crisis has 60.5 MB of them, and
 * every sector after the first landed 4 too early, so the BIOS read the ISO
 * descriptor where it was not. */
typedef struct {
    uint64_t out_start;   /* first output byte this run produces */
    uint32_t out_len;     /* output bytes produced */
    uint32_t file_pos;    /* payload offset in the ECM file */
    uint32_t count;       /* literal bytes (type 0) or sectors (types 1-3) */
    uint8_t  type;        /* 0 = literal, 1-3 = modelled sector */
} EcmRun;

typedef struct EcmDecoder {
    FILE     *fp;            /* open file handle (caller owns close) */
    EcmRun   *runs;
    uint32_t  run_count;
    uint32_t  run_capacity;
    uint64_t  out_bytes;     /* size of the reconstructed image */
    uint32_t  total_sectors; /* out_bytes / 2352 */
} EcmDecoder;

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
