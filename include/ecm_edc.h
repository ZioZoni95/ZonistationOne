/* SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 ZioZoni95
 *
 * Part of ZoniStation One, a PlayStation 1 emulator.
 * See LICENSE for the full licence text and THIRD-PARTY.md for the
 * components of this project that have other authors.
 */
/*
 * EDC/ECC computation for CD-ROM sectors (ECMA-130 / Yellow Book).
 * Used by the ECM decoder to reconstruct stripped error codes.
 *
 * Algorithm reference: ECMA-130, 2nd Edition (1996).
 * This implementation is written from the specification, not from any
 * third-party emulator source.
 */
#ifndef ECM_EDC_H
#define ECM_EDC_H

#include <stdint.h>
#include <stdbool.h>

/* Initialise the three LUTs (ecc_f, ecc_b, edc). Call once at startup. */
void ecm_edc_init(void);

/*
 * Generate EDC + ECC for a 2352-byte raw sector in place.
 *   type == 1: Mode 1
 *   type == 2: Mode 2 Form 1
 *   type == 3: Mode 2 Form 2
 *
 * For type 2/3 the caller must have already set sector[0x0F] = 0x02 and
 * filled sector[0x10..0x13] with the duplicated subheader bytes.
 */
void ecm_edc_generate(uint8_t *sector, int type);

#endif /* ECM_EDC_H */
