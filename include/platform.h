/* SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 ZioZoni95
 *
 * Part of ZoniStation One, a PlayStation 1 emulator.
 * See LICENSE for the full licence text and THIRD-PARTY.md for the
 * components of this project that have other authors.
 */
#ifndef PLATFORM_H
#define PLATFORM_H

/* The few host calls that differ between Linux and Windows, in one place.
 *
 * Everything else in the tree is ISO C, SDL3 or a POSIX call that MinGW-w64
 * provides as it is (pthread through winpthreads, dirent.h, strings.h,
 * access()). What is here is what either does not exist on Windows or exists
 * with different behaviour — rename() above all, which on Windows refuses to
 * replace an existing file and so turns every save after the first into a
 * failure (verified on 2026-11-26 with MinGW-w64 UCRT: "File exists").
 *
 * On Linux each function is the POSIX call it replaces, unchanged. */

#include <stdbool.h>
#include <stddef.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Create one directory level. Succeeds if it already exists. */
bool zs_mkdir(const char* path);

/* Remove an empty directory. */
bool zs_rmdir(const char* path);

/* Rename from onto to, replacing to if it exists, atomically where the host
 * allows it. This is what the temp-file-then-rename saves (memory cards,
 * savestates, thumbnails) rely on: on Windows a plain rename() fails when the
 * destination exists, and removing it first would give up the crash safety
 * those saves exist for. */
bool zs_rename_replace(const char* from, const char* to);

/* setenv/unsetenv. overwrite=0 leaves an existing value alone, as POSIX. */
bool zs_setenv(const char* name, const char* value, int overwrite);
bool zs_unsetenv(const char* name);

/* localtime_r. Returns out, or NULL on failure. */
struct tm* zs_localtime(const time_t* t, struct tm* out);

/* Last path separator in path, or NULL. Windows accepts both '/' and '\\', and
 * a path typed in a Windows shell uses the second, so both count everywhere. */
static inline const char* zs_last_sep(const char* path) {
    const char* last = NULL;
    for (const char* p = path; p && *p; p++)
        if (*p == '/' || *p == '\\') last = p;
    return last;
}

/* Name of the host OS family, for logs and the Host HW panel. */
static inline const char* zs_platform_name(void) {
#ifdef _WIN32
    return "Windows";
#else
    return "Linux";
#endif
}

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_H */
