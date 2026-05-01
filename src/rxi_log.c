/*
 * rxi/log.c — renamed to rxi_* namespace to avoid conflicts.
 * Original: Copyright (c) 2020 rxi (MIT License)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 */

#include "rxi_log.h"
#include <string.h>

#define RXI_MAX_CALLBACKS 32

typedef struct {
  rxi_log_LogFn fn;
  void *udata;
  int level;
} RxiCallback;

static struct {
  void *udata;
  rxi_log_LockFn lock;
  int level;
  bool quiet;
  RxiCallback callbacks[RXI_MAX_CALLBACKS];
} L;

static const char *level_strings[] = {
  "TRACE", "DEBUG", "INFO", "WARN", "ERROR", "FATAL"
};

static const char *level_colors[] = {
  "\x1b[2m",    /* TRACE — dim */
  "\x1b[36m",   /* DEBUG — cyan */
  "\x1b[32m",   /* INFO  — green */
  "\x1b[33m",   /* WARN  — yellow */
  "\x1b[31m",   /* ERROR — red */
  "\x1b[35m"    /* FATAL — magenta */
};

static void stderr_callback(rxi_log_Event *ev) {
  char buf[16];
  buf[strftime(buf, sizeof(buf), "%H:%M:%S", ev->time)] = '\0';
  fprintf(
    ev->udata,
    "%s %s%-5s\x1b[0m ",
    buf, level_colors[ev->level], level_strings[ev->level]);
  vfprintf(ev->udata, ev->fmt, ev->ap);
  fprintf(ev->udata, "\x1b[0m\n");
  fflush(ev->udata);
}

static void file_callback(rxi_log_Event *ev) {
  char buf[64];
  buf[strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", ev->time)] = '\0';
  fprintf(ev->udata, "%s %-5s ", buf, level_strings[ev->level]);
  vfprintf(ev->udata, ev->fmt, ev->ap);
  fprintf(ev->udata, "\n");
  fflush(ev->udata);
}

static void lock(void)   { if (L.lock) { L.lock(true,  L.udata); } }
static void unlock(void) { if (L.lock) { L.lock(false, L.udata); } }

const char* rxi_log_level_string(int level) {
  return level_strings[level];
}

void rxi_log_set_lock(rxi_log_LockFn fn, void *udata) {
  L.lock  = fn;
  L.udata = udata;
}

void rxi_log_set_level(int level) { L.level = level; }
void rxi_log_set_quiet(bool enable) { L.quiet = enable; }

int rxi_log_add_callback(rxi_log_LogFn fn, void *udata, int level) {
  for (int i = 0; i < RXI_MAX_CALLBACKS; i++) {
    if (!L.callbacks[i].fn) {
      L.callbacks[i] = (RxiCallback){ fn, udata, level };
      return 0;
    }
  }
  return -1;
}

int rxi_log_add_fp(FILE *fp, int level) {
  return rxi_log_add_callback(file_callback, fp, level);
}

static void init_event(rxi_log_Event *ev, void *udata) {
  if (!ev->time) {
    time_t t = time(NULL);
    ev->time = localtime(&t);
  }
  ev->udata = udata;
}

void rxi_log_log(int level, const char *file, int line, const char *fmt, ...) {
  (void)file; (void)line;  /* not used in our output format */

  rxi_log_Event ev = {
    .fmt   = fmt,
    .file  = file,
    .line  = line,
    .level = level,
  };

  lock();

  if (!L.quiet && level >= L.level) {
    init_event(&ev, stderr);
    va_start(ev.ap, fmt);
    stderr_callback(&ev);
    va_end(ev.ap);
  }

  for (int i = 0; i < RXI_MAX_CALLBACKS && L.callbacks[i].fn; i++) {
    RxiCallback *cb = &L.callbacks[i];
    if (level >= cb->level) {
      init_event(&ev, cb->udata);
      va_start(ev.ap, fmt);
      cb->fn(&ev);
      va_end(ev.ap);
    }
  }

  unlock();
}
