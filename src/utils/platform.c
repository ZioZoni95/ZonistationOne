/* SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 ZioZoni95
 *
 * Part of ZoniStation One, a PlayStation 1 emulator.
 * See LICENSE for the full licence text and THIRD-PARTY.md for the
 * components of this project that have other authors.
 *
 * platform.c — see platform.h.
 */
/* setenv/unsetenv/localtime_r are POSIX, hidden by -std=c99. Must precede every
 * include. Harmless on Windows, where none of them is used. */
#define _POSIX_C_SOURCE 200809L

#include "platform.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef _WIN32
#include <direct.h>
/* The one Win32 call this file needs, declared here instead of pulling in
 * windows.h. Unit tests #include this file next to emulator sources, and
 * windows.h brings thousands of macros that collide with names in them
 * (winerror.h's ERROR_NOT_READY against include/cdrom.h, for one). The types
 * are the ones windows.h uses: BOOL is int, DWORD unsigned long, WINAPI
 * __stdcall, so the two declarations agree if both are ever seen. */
__declspec(dllimport) int __stdcall MoveFileExA(const char* existing, const char* replacement,
                                                unsigned long flags);
#define ZS_MOVEFILE_REPLACE_EXISTING 0x00000001ul
#define ZS_MOVEFILE_WRITE_THROUGH    0x00000008ul
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

#ifdef _WIN32

bool zs_mkdir(const char* path) {
    return _mkdir(path) == 0 || errno == EEXIST;
}

bool zs_rmdir(const char* path) {
    return _rmdir(path) == 0;
}

/* MOVEFILE_REPLACE_EXISTING replaces the destination in one call;
 * MOVEFILE_WRITE_THROUGH returns only once the move is on disk, which is the
 * point of writing to a temporary file first. Both files are in the same
 * directory, so the move never has to fall back to a copy. */
bool zs_rename_replace(const char* from, const char* to) {
    return MoveFileExA(from, to, ZS_MOVEFILE_REPLACE_EXISTING | ZS_MOVEFILE_WRITE_THROUGH) != 0;
}

bool zs_setenv(const char* name, const char* value, int overwrite) {
    if (!overwrite && getenv(name)) return true;
    return _putenv_s(name, value) == 0;
}

/* _putenv_s with an empty value removes the variable. */
bool zs_unsetenv(const char* name) {
    return _putenv_s(name, "") == 0;
}

/* localtime_s has its arguments the other way round and returns an errno. */
struct tm* zs_localtime(const time_t* t, struct tm* out) {
    return localtime_s(out, t) == 0 ? out : NULL;
}

#else /* POSIX */

bool zs_mkdir(const char* path) {
    return mkdir(path, 0755) == 0 || errno == EEXIST;
}

bool zs_rmdir(const char* path) {
    return rmdir(path) == 0;
}

/* rename() replaces the destination atomically on one filesystem. */
bool zs_rename_replace(const char* from, const char* to) {
    return rename(from, to) == 0;
}

bool zs_setenv(const char* name, const char* value, int overwrite) {
    return setenv(name, value, overwrite) == 0;
}

bool zs_unsetenv(const char* name) {
    return unsetenv(name) == 0;
}

struct tm* zs_localtime(const time_t* t, struct tm* out) {
    return localtime_r(t, out);
}

#endif
