/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Decode a .bin.ecm to a plain .bin, so a reference emulator that cannot read
 * the container can run the same image. Usage: ecm_dump <in.bin.ecm> <out.bin> */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include "cdrom_ecm.h"
#include "ecm_edc.h"

int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: %s <image.bin.ecm> <out.bin>\n", argv[0]); return 2; }
    ecm_edc_init();
    FILE *fp = fopen(argv[1], "rb");
    if (!fp) { perror(argv[1]); return 1; }
    EcmDecoder *dec = ecm_decoder_open(fp);
    if (!dec) { fprintf(stderr, "not an ECM file\n"); fclose(fp); return 1; }
    FILE *out = fopen(argv[2], "wb");
    if (!out) { perror(argv[2]); return 1; }
    uint8_t sector[2352];
    for (uint32_t i = 0; i < dec->total_sectors; i++) {
        if (!ecm_read_sector(dec, i, i, sector)) { fprintf(stderr, "decode failed at %u\n", i); return 1; }
        fwrite(sector, 1, 2352, out);
    }
    printf("%u sectors written\n", dec->total_sectors);
    fclose(out); ecm_decoder_free(dec); fclose(fp);
    return 0;
}
