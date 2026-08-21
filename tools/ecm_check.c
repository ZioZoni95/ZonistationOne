/* SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 ZioZoni95
 *
 * Part of ZoniStation One, a PlayStation 1 emulator.
 * See LICENSE for the full licence text and THIRD-PARTY.md for the
 * components of this project that have other authors.
 *
 * ecm_check — prove an ECM image decodes to the original .bin, and say what
 * the disc's filesystem looks like.
 *
 * The container appends the EDC (CRC32, polynomial D8018001h) of the *entire*
 * decoded output as its last four bytes, so decoding the whole image and
 * running that CRC over the result proves the reconstruction byte-identical to
 * the .bin it was made from — in one pass, without keeping the .bin around.
 * That check is what stopped a wrong LibCrypt theory once already
 * (docs/ecm_libcrypt_discovery.md); it deserved a tool instead of being redone
 * by hand each time.
 *
 * With -iso it also walks the ISO9660 primary volume descriptor and the root
 * directory, which answers the other half of "why does this disc not boot":
 * whether the executable the BIOS is looking for is where the filesystem says.
 *
 *   make tools/ecm_check
 *   ./tools/ecm_check "games/Some Game.bin.ecm" [-iso]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "cdrom_ecm.h"
#include "ecm_edc.h"

/* The EDC polynomial the format uses for its whole-file checksum. */
static uint32_t edc_table[256];

static void edc_table_init(void) {
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t edc = i;
        for (int j = 0; j < 8; j++)
            edc = (edc >> 1) ^ (edc & 1 ? 0xD8018001u : 0);
        edc_table[i] = edc;
    }
}

static uint32_t edc_update(uint32_t edc, const uint8_t *src, size_t len) {
    while (len--) edc = (edc >> 8) ^ edc_table[(edc ^ *src++) & 0xFF];
    return edc;
}

/* Both ISO9660 multi-byte forms are stored little-endian first. */
static uint32_t iso_u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* User data of a raw 2352-byte sector: Mode 1 starts at 16, Mode 2 Form 1 at 24. */
static const uint8_t *user_data(const uint8_t *sector, int *mode_out) {
    int mode = sector[0x0F];
    if (mode_out) *mode_out = mode;
    return (mode == 2) ? sector + 24 : sector + 16;
}

static void dump_directory(EcmDecoder *dec, uint32_t extent_lba, uint32_t extent_len) {
    uint8_t sector[2352];
    uint32_t sectors = (extent_len + 2047) / 2048;
    printf("  root directory at LBA %u, %u bytes (%u sectors)\n", extent_lba, extent_len, sectors);

    for (uint32_t s = 0; s < sectors; s++) {
        if (!ecm_read_sector(dec, extent_lba + s, extent_lba + s, sector)) {
            printf("  ! read failed at LBA %u\n", extent_lba + s);
            return;
        }
        const uint8_t *d = user_data(sector, NULL);
        uint32_t off = 0;
        while (off < 2048) {
            uint8_t rec_len = d[off];
            if (rec_len == 0) break;
            if (off + rec_len > 2048) break;
            uint32_t lba  = iso_u32(d + off + 2);
            uint32_t size = iso_u32(d + off + 10);
            uint8_t  flags = d[off + 25];
            uint8_t  name_len = d[off + 32];
            char name[64];
            uint32_t n = name_len < sizeof(name) - 1 ? name_len : sizeof(name) - 1;
            memcpy(name, d + off + 33, n);
            name[n] = '\0';
            if (name_len == 1 && (name[0] == '\0' || name[0] == '\1'))
                snprintf(name, sizeof(name), "%s", name[0] == '\0' ? "." : "..");
            printf("    %-24s LBA %-8u %10u bytes%s\n", name, lba, size,
                   (flags & 0x02) ? "  [dir]" : "");
            off += rec_len;
        }
    }
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <image.bin.ecm> [-iso]\n", argv[0]);
        return 2;
    }
    bool want_iso = (argc > 2 && strcmp(argv[2], "-iso") == 0);

    edc_table_init();
    ecm_edc_init();

    FILE *fp = fopen(argv[1], "rb");
    if (!fp) { perror(argv[1]); return 1; }

    /* The stored checksum is the last four bytes of the container. */
    if (fseek(fp, -4, SEEK_END) != 0) { perror("seek"); fclose(fp); return 1; }
    uint8_t tail[4];
    if (fread(tail, 1, 4, fp) != 4) { perror("read"); fclose(fp); return 1; }
    uint32_t stored = (uint32_t)tail[0] | ((uint32_t)tail[1] << 8) |
                      ((uint32_t)tail[2] << 16) | ((uint32_t)tail[3] << 24);
    rewind(fp);

    EcmDecoder *dec = ecm_decoder_open(fp);
    if (!dec) { fprintf(stderr, "not an ECM file (or its index could not be built)\n"); fclose(fp); return 1; }

    printf("%s\n  %u sectors, %.1f MB decoded\n", argv[1], dec->total_sectors,
           (double)dec->total_sectors * 2352.0 / (1024.0 * 1024.0));

    uint8_t sector[2352];
    uint32_t edc = 0;
    for (uint32_t i = 0; i < dec->total_sectors; i++) {
        if (!ecm_read_sector(dec, i, i, sector)) {
            printf("  ! decode failed at sector %u — the container is short or damaged\n", i);
            ecm_decoder_free(dec); fclose(fp); return 1;
        }
        edc = edc_update(edc, sector, 2352);
    }

    bool match = (edc == stored);
    printf("  whole-file EDC: computed %08X, stored %08X — %s\n", edc, stored,
           match ? "byte-identical to the original .bin" : "MISMATCH, the image is not intact");

    /* Where does the filesystem actually start? A dump that carries the disc's
     * two-second pregap, or a track that begins somewhere other than LBA 0,
     * puts the primary volume descriptor 150 sectors further along — which
     * looks exactly like a corrupt image if only LBA 16 is checked. */
    if (want_iso) {
        printf("  CD001 signature found at sector:");
        bool any = false;
        for (uint32_t i = 0; i < 400 && i < dec->total_sectors; i++) {
            if (!ecm_read_sector(dec, i, i, sector)) break;
            for (int off = 16; off <= 24; off += 8)
                if (memcmp(sector + off + 1, "CD001", 5) == 0) {
                    printf(" %u(+%d)", i, off);
                    any = true;
                }
        }
        printf("%s\n", any ? "" : " none in the first 400");
    }

    if (want_iso) {
        /* The primary volume descriptor always sits at LBA 16. */
        if (ecm_read_sector(dec, 16, 16, sector)) {
            int mode = 0;
            const uint8_t *d = user_data(sector, &mode);
            printf("  sector 16 is mode %d, descriptor id %.5s type %u\n", mode, d + 1, d[0]);
            if (memcmp(d + 1, "CD001", 5) == 0) {
                char volid[33];
                memcpy(volid, d + 40, 32); volid[32] = '\0';
                for (int i = 31; i >= 0 && volid[i] == ' '; i--) volid[i] = '\0';
                printf("  volume \"%s\", %u sectors of %u bytes\n",
                       volid, iso_u32(d + 80), iso_u32(d + 128));
                /* The root directory record lives at offset 156 of the PVD. */
                dump_directory(dec, iso_u32(d + 156 + 2), iso_u32(d + 156 + 10));
            } else {
                printf("  ! no CD001 signature — this is not an ISO9660 data track\n");
            }
        }
    }

    ecm_decoder_free(dec);
    fclose(fp);
    return match ? 0 : 1;
}
