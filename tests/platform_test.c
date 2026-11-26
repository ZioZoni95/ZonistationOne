/* SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 ZioZoni95
 *
 * Part of ZoniStation One, a PlayStation 1 emulator.
 * See LICENSE for the full licence text and THIRD-PARTY.md for the
 * components of this project that have other authors.
 */
/* src/utils/platform.c, the calls that differ between Linux and Windows.
 *
 * The one that matters: memory cards, savestates and their thumbnails are
 * written to a temporary file and renamed over the old one. On Windows a plain
 * rename() refuses an existing destination (checked 2026-11-26, MinGW-w64
 * UCRT: "File exists"), so the first save worked and every later one failed.
 * zs_rename_replace() must replace, and must leave the new contents in place
 * and no temporary file behind. */
#include "../src/utils/platform.c"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_checks, g_failed;
#define CHECK(cond, msg) do { g_checks++; if (!(cond)) { g_failed++; \
    printf("FAIL: %s (line %d)\n", msg, __LINE__); } } while (0)

static bool write_text(const char* path, const char* text) {
    FILE* f = fopen(path, "wb");
    if (!f) return false;
    bool ok = fputs(text, f) >= 0;
    return (fclose(f) == 0) && ok;
}

static bool read_is(const char* path, const char* text) {
    char buf[64] = {0};
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    return n == strlen(text) && memcmp(buf, text, n) == 0;
}

static bool exists(const char* path) {
    FILE* f = fopen(path, "rb");
    if (f) fclose(f);
    return f != NULL;
}

int main(void) {
    char dir[256], a[300], b[300];
    const char* base = getenv("TMPDIR");
    if (!base || !*base) base = getenv("TEMP");
    if (!base || !*base) base = "/tmp";
    snprintf(dir, sizeof(dir), "%s/zs1_platform_test_%u", base, (unsigned)time(NULL));
    snprintf(a, sizeof(a), "%s/card.mcd.tmp", dir);
    snprintf(b, sizeof(b), "%s/card.mcd", dir);

    CHECK(zs_mkdir(dir), "a new directory is created");
    CHECK(zs_mkdir(dir), "an existing directory is not an error");

    /* rename onto nothing, then onto an existing file: the second is the save
     * that failed on Windows. */
    CHECK(write_text(a, "first"), "temporary file written");
    CHECK(zs_rename_replace(a, b), "rename onto a missing destination");
    CHECK(read_is(b, "first") && !exists(a), "destination holds it, temporary is gone");

    CHECK(write_text(a, "second"), "temporary file written again");
    CHECK(zs_rename_replace(a, b), "rename onto an existing destination replaces it");
    CHECK(read_is(b, "second") && !exists(a), "destination holds the new contents, temporary is gone");

    CHECK(!zs_rename_replace(a, b), "a missing source is a failure, not a silent success");
    CHECK(read_is(b, "second"), "a failed rename leaves the destination alone");

    remove(b);
    CHECK(zs_rmdir(dir), "the emptied directory is removed");

    /* Environment: what the A/B switches (ZS1_*) are read through. */
    CHECK(zs_setenv("ZS1_PLATFORM_TEST", "1", 1) && getenv("ZS1_PLATFORM_TEST") &&
          strcmp(getenv("ZS1_PLATFORM_TEST"), "1") == 0, "setenv sets");
    CHECK(zs_setenv("ZS1_PLATFORM_TEST", "2", 0) && strcmp(getenv("ZS1_PLATFORM_TEST"), "1") == 0,
          "overwrite=0 keeps an existing value");
    CHECK(zs_setenv("ZS1_PLATFORM_TEST", "3", 1) && strcmp(getenv("ZS1_PLATFORM_TEST"), "3") == 0,
          "overwrite=1 replaces it");
    CHECK(zs_unsetenv("ZS1_PLATFORM_TEST") && getenv("ZS1_PLATFORM_TEST") == NULL, "unsetenv removes it");

    /* Separators: a Windows path uses '\\', and both kinds can be mixed. */
    const char* p1 = "games/disc.cue";
    const char* p2 = "games\\disc.cue";
    const char* p3 = "C:\\psx/games\\sub/disc.cue";
    CHECK(zs_last_sep(p1) == p1 + 5, "forward slash found");
    CHECK(zs_last_sep(p2) == p2 + 5, "backslash found");
    CHECK(zs_last_sep(p3) && strcmp(zs_last_sep(p3), "/disc.cue") == 0, "the last of mixed separators");
    CHECK(zs_last_sep("disc.cue") == NULL && zs_last_sep(NULL) == NULL, "no separator, no pointer");

    time_t t = 0;
    struct tm tmv;
    CHECK(zs_localtime(&t, &tmv) == &tmv && tmv.tm_year >= 69, "localtime fills the caller's struct");

    printf("platform_test (%s): %d checks, %d failed\n", zs_platform_name(), g_checks, g_failed);
    return g_failed ? 1 : 0;
}
