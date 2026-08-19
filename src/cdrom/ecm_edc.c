/* SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 ZioZoni95
 *
 * Part of ZoniStation One, a PlayStation 1 emulator.
 * See LICENSE for the full licence text and THIRD-PARTY.md for the
 * components of this project that have other authors.
 */
/*
 * EDC/ECC generation for CD-ROM sectors.
 *
 * Implements the Error Detection Code (CRC32) and Error Correction Code
 * (Reed-Solomon Product Code) as specified in ECMA-130, 2nd Edition.
 * Polynomial arithmetic is in GF(2^8) with irreducible polynomial
 * P(x) = x^8 + x^4 + x^3 + x^2 + 1  (0x11D).
 *
 * Written from the ECMA-130 specification. No code copied from any
 * third-party emulator or ECM tool.
 */

#include "ecm_edc.h"
#include <string.h>

/* ---- GF(2^8) LUTs ---- */

/* Forward multiplication: ecc_f[i] = (i << 1) ^ (i & 0x80 ? 0x11D : 0) */
static uint8_t ecc_f_lut[256];
/* Backward (inverse): ecc_b[i ^ ecc_f[i]] = i */
static uint8_t ecc_b_lut[256];
/* EDC CRC32 table, polynomial 0xD8018001 */
static uint32_t edc_lut[256];

void ecm_edc_init(void) {
    for (unsigned i = 0; i < 256; i++) {
        unsigned j = (i << 1) ^ (i & 0x80 ? 0x11D : 0);
        ecc_f_lut[i] = (uint8_t)j;
        ecc_b_lut[i ^ j] = (uint8_t)i;

        uint32_t edc = (uint32_t)i;
        for (unsigned k = 0; k < 8; k++)
            edc = (edc >> 1) ^ (edc & 1 ? 0xD8018001u : 0);
        edc_lut[i] = edc;
    }
}

/* ---- EDC (CRC32) ---- */

static uint32_t edc_compute(const uint8_t *src, unsigned size) {
    uint32_t edc = 0;
    for (unsigned i = 0; i < size; i++)
        edc = (edc >> 8) ^ edc_lut[(edc ^ src[i]) & 0xFF];
    return edc;
}

static void edc_write(uint8_t *dest, uint32_t edc) {
    dest[0] = (uint8_t)(edc);
    dest[1] = (uint8_t)(edc >> 8);
    dest[2] = (uint8_t)(edc >> 16);
    dest[3] = (uint8_t)(edc >> 24);
}

/* ---- ECC (Reed-Solomon Product Code) ---- */

/*
 * Compute one block of ECC (either P or Q channel).
 *
 * The generator polynomial is (x+1)(x+2) = x^2 + 3x + 2.
 * In GF(2^8): log(3) = 0x19, log(2) = 0x01.
 *
 * Parameters match the ECMA-130 layout:
 *   major_count  — number of ECC bytes in the output
 *   minor_count  — number of data bytes per ECC line
 *   major_mult   — stride for the starting index of each line
 *   minor_inc    — byte advance within a line
 *   src          — data region (from byte 0x0C of the sector)
 *   dest         — where to write the ECC bytes
 */
static void ecc_compute_block(const uint8_t *src,
                              unsigned major_count, unsigned minor_count,
                              unsigned major_mult, unsigned minor_inc,
                              uint8_t *dest) {
    unsigned size = major_count * minor_count;
    for (unsigned major = 0; major < major_count; major++) {
        unsigned index = (major >> 1) * major_mult + (major & 1);
        uint8_t ecc_a = 0;
        uint8_t ecc_b = 0;
        for (unsigned minor = 0; minor < minor_count; minor++) {
            uint8_t temp = src[index];
            index += minor_inc;
            if (index >= size) index -= size;
            ecc_a ^= temp;
            ecc_b ^= temp;
            ecc_a = ecc_f_lut[ecc_a];
        }
        ecc_a = ecc_b_lut[ecc_f_lut[ecc_a] ^ ecc_b];
        dest[major]                = ecc_a;
        dest[major + major_count]  = (uint8_t)(ecc_a ^ ecc_b);
    }
}

/*
 * Generate ECC P and Q codes for a sector.
 *   zeroaddress: if true, zero bytes 12-15 before computing, restore after.
 *                 (Mode 1 and Mode 2 require this; the address field must
 *                  be zeroed for the ECC computation per ECMA-130.)
 */
static void ecc_generate(uint8_t *sector, int zeroaddress) {
    uint8_t saved_addr[4];
    if (zeroaddress) {
        memcpy(saved_addr, sector + 12, 4);
        memset(sector + 12, 0, 4);
    }

    /* ECC P: 86 columns, 24 rows, stride 2, minor_inc 86 */
    ecc_compute_block(sector + 0x0C, 86, 24, 2, 86, sector + 0x81C);

    /* ECC Q: 52 columns, 43 rows, stride 86, minor_inc 88 */
    ecc_compute_block(sector + 0x0C, 52, 43, 86, 88, sector + 0x8C8);

    if (zeroaddress)
        memcpy(sector + 12, saved_addr, 4);
}

/* ---- Public: generate EDC + ECC for one 2352-byte sector ---- */

void ecm_edc_generate(uint8_t *sector, int type) {
    switch (type) {
    case 1: /* Mode 1 */
        /* EDC over bytes [0x00 .. 0x80F] (2064 bytes), written at 0x810 */
        edc_write(sector + 0x810, edc_compute(sector, 0x810));
        /* 8 reserved zero bytes */
        memset(sector + 0x814, 0, 8);
        /* ECC P+Q */
        ecc_generate(sector, 0);
        break;

    case 2: /* Mode 2 Form 1 */
        /* EDC over bytes [0x10 .. 0x817] (2056 = 8 subheader + 2048 data),
         * written at 0x818 */
        edc_write(sector + 0x818, edc_compute(sector + 0x10, 0x808));
        /* ECC P+Q */
        ecc_generate(sector, 1);
        break;

    case 3: /* Mode 2 Form 2 */
        /* EDC over bytes [0x10 .. 0x92B] (2332 = 8 subheader + 2324 data),
         * written at 0x92C. No ECC for Form 2. */
        edc_write(sector + 0x92C, edc_compute(sector + 0x10, 0x91C));
        break;
    }
}
