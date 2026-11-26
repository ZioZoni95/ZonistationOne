/* SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 ZioZoni95
 *
 * Part of ZoniStation One, a PlayStation 1 emulator.
 * See LICENSE for the full licence text and THIRD-PARTY.md for the
 * components of this project that have other authors.
 */
/* Disc image loading: the paths that take a file the user hands over.
 *
 * Regressions for the 2026-11-26 review:
 *   - an upper-case DISC.BIN was sent to the CUE parser, found no tracks and
 *     left the machine in "BIOS-only mode" with no hint why;
 *   - the ECM block header had no length limit, so a file of continuation bytes
 *     shifted past the width of the type (UB, caught by UBSan), and a run whose
 *     byte count did not fit 32 bits wrapped instead of being refused.
 *
 * Run it under -fsanitize=undefined to see the second one: a build without the
 * fix passes the checks below by luck and fails the sanitizer.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include "log.h"

LogLevel current_log_level = LOG_LEVEL_SILENT;
void log_print(LogCategory category, LogLevel level, const char *format, ...) {
    (void)category; (void)level; (void)format;
}

#include "../src/cdrom/ecm_edc.c"
#include "../src/cdrom/cdrom_ecm.c"
#include "../src/cdrom/cdrom_disc.c"

static int g_checks, g_failed;
#define CHECK(cond, msg) do { g_checks++; if (!(cond)) { g_failed++; \
    printf("FAIL: %s (line %d)\n", msg, __LINE__); } } while (0)

static char g_dir[256];

static void write_file(const char *name, const void *data, size_t len) {
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", g_dir, name);
    FILE *f = fopen(path, "wb");
    if (!f || fwrite(data, 1, len, f) != len) { printf("cannot write %s\n", path); exit(2); }
    fclose(f);
}

static void path_of(char *out, size_t n, const char *name) {
    snprintf(out, n, "%s/%s", g_dir, name);
}

static void remove_all(void) {
    char cmd[300];
    snprintf(cmd, sizeof(cmd), "rm -rf '%s'", g_dir);
    if (system(cmd) != 0) { /* best effort */ }
}

int main(void) {
    snprintf(g_dir, sizeof(g_dir), "/tmp/zs1_disc_test_XXXXXX");
    if (!mkdtemp(g_dir)) { perror("mkdtemp"); return 2; }

    /* 20 raw sectors of zeros: enough for the loader, which only sizes the file. */
    static uint8_t bin[2352 * 20];
    write_file("disc.bin", bin, sizeof(bin));
    write_file("UPPER.BIN", bin, sizeof(bin));
    write_file("Mixed.Bin", bin, sizeof(bin));

    char p[512];
    CdromDisc d;

    path_of(p, sizeof(p), "disc.bin");
    CHECK(cdrom_disc_load(&d, p) && d.total_sectors == 20, "lower-case .bin loads");
    cdrom_disc_unload(&d);

    path_of(p, sizeof(p), "UPPER.BIN");
    CHECK(cdrom_disc_load(&d, p) && d.last_track == 1 && d.total_sectors == 20,
          "upper-case .BIN loads as a single raw track");
    cdrom_disc_unload(&d);

    path_of(p, sizeof(p), "Mixed.Bin");
    CHECK(cdrom_disc_load(&d, p) && d.total_sectors == 20, "mixed-case .Bin loads");
    cdrom_disc_unload(&d);

    /* ECM: "ECM\0" then a block header of continuation bytes that never ends. */
    {
        uint8_t ecm[4 + 32];
        memcpy(ecm, "ECM\0", 4);
        memset(ecm + 4, 0xFF, sizeof(ecm) - 4);
        write_file("endless.bin.ecm", ecm, sizeof(ecm));
        path_of(p, sizeof(p), "endless.bin.ecm");
        CHECK(!cdrom_disc_load(&d, p), "an endless block header is refused, not shifted past 32 bits");
    }

    /* ECM: a Mode 1 run of 2,097,152 sectors, which is 4.9 GB of output and does not
     * fit the 32-bit run length. Header bytes: type 1, count-1 = 0x1FFFFF. */
    {
        uint8_t ecm[4 + 4] = { 'E', 'C', 'M', 0, 0xFD, 0xFF, 0xFF, 0x03 };
        write_file("huge.bin.ecm", ecm, sizeof(ecm));
        path_of(p, sizeof(p), "huge.bin.ecm");
        CHECK(!cdrom_disc_load(&d, p), "a run longer than 32 bits of output is refused");
    }

    remove_all();
    printf("disc_load_test: %d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
