/* SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2020 rxi
 *
 * Included in ZoniStation One (GPL-3.0-or-later); this file keeps its own
 * MIT terms. See THIRD-PARTY.md.
 */
/**
 * rxi/log.c — renamed to rxi_* namespace to avoid conflicts.
 * Original: Copyright (c) 2020 rxi (MIT License)
 */

#ifndef RXI_LOG_H
#define RXI_LOG_H

#include <stdio.h>
#include <stdarg.h>
#include <stdbool.h>
#include <time.h>

#define RXI_LOG_VERSION "0.1.0"

typedef struct {
  va_list ap;
  const char *fmt;
  const char *file;
  struct tm *time;
  void *udata;
  int line;
  int level;
} rxi_log_Event;

typedef void (*rxi_log_LogFn)(rxi_log_Event *ev);
typedef void (*rxi_log_LockFn)(bool lock, void *udata);

enum { RXI_LOG_TRACE, RXI_LOG_DEBUG, RXI_LOG_INFO, RXI_LOG_WARN, RXI_LOG_ERROR, RXI_LOG_FATAL };

const char* rxi_log_level_string(int level);
void rxi_log_set_lock(rxi_log_LockFn fn, void *udata);
void rxi_log_set_level(int level);
void rxi_log_set_quiet(bool enable);
int  rxi_log_add_callback(rxi_log_LogFn fn, void *udata, int level);
int  rxi_log_add_fp(FILE *fp, int level);

void rxi_log_log(int level, const char *file, int line, const char *fmt, ...);

#endif
