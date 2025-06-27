#include "log.h"
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>

static int global_log_level = LOG_LEVEL_INFO;

void log_set_level(int level) {
    global_log_level = level;
}

void log_msg(int level, const char* fmt, ...) {
    if (level > global_log_level) return;
    const char* level_str = "INFO";
    switch (level) {
        case LOG_LEVEL_FATAL: level_str = "FATAL"; break;
        case LOG_LEVEL_ERROR: level_str = "ERROR"; break;
        case LOG_LEVEL_WARN:  level_str = "WARN";  break;
        case LOG_LEVEL_INFO:  level_str = "INFO";  break;
        case LOG_LEVEL_DEBUG: level_str = "DEBUG"; break;
        case LOG_LEVEL_TRACE: level_str = "TRACE"; break;
    }
    fprintf(stderr, "[%s] ", level_str);
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fprintf(stderr, "\n");
    if (level == LOG_LEVEL_FATAL) {
        // Optionally abort on fatal
        fflush(stderr);
        abort();
    }
}

int log_get_level(void) { return global_log_level; } 