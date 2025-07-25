#include "../include/log.h"
#include <stdio.h>
#include <stdarg.h>

// Define log level constants for use in this file
#define NC_LOGE 2
#define NC_LOGI 3
#define NC_LOGW 4
#define NC_LOGD 5
#define NC_LOGT 6

void nc_log_set_level(int level) {
    // Stub: do nothing
    (void)level;
}

void nc_log_msg(int level, const char* fmt, ...) {
    // Stub: print log level and message
    const char* level_str = "INFO";
    switch (level) {
        case NC_LOGE: level_str = "ERROR"; break;
        case NC_LOGI: level_str = "INFO"; break;
        case NC_LOGW: level_str = "WARN"; break;
        case NC_LOGD: level_str = "DEBUG"; break;
        case NC_LOGT: level_str = "TRACE"; break;
    }
    printf("[LOG][%s] ", level_str);
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    printf("\n");
} 