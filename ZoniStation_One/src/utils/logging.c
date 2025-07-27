#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <unistd.h>

#include "zonistation_common.h"

// Global log level
static zs_log_level_t g_log_level = ZS_LOG_LEVEL_INFO;

// Log level strings
static const char* LOG_LEVEL_STRINGS[] = {
    "ERROR",
    "WARN",
    "INFO",
    "DEBUG",
    "TRACE"
};

// Log colors (for terminals that support it)
static const char* LOG_COLORS[] = {
    "\033[31m", // Red for ERROR
    "\033[33m", // Yellow for WARN
    "\033[32m", // Green for INFO
    "\033[36m", // Cyan for DEBUG
    "\033[35m"  // Magenta for TRACE
};

static const char* LOG_COLOR_RESET = "\033[0m";

void zs_set_log_level(zs_log_level_t level) {
    g_log_level = level;
}

void zs_log(zs_log_level_t level, const char* file, int line, const char* fmt, ...) {
    if (level > g_log_level) {
        return;
    }
    
    // Get current time
    time_t now = time(NULL);
    struct tm* tm_info = localtime(&now);
    char time_str[26];
    strftime(time_str, 26, "%Y-%m-%d %H:%M:%S", tm_info);
    
    // Get filename without path
    const char* filename = strrchr(file, '/');
    if (filename == NULL) {
        filename = strrchr(file, '\\');
    }
    if (filename == NULL) {
        filename = file;
    } else {
        filename++; // Skip the slash
    }
    
    // Check if we're outputting to a terminal
    int is_terminal = 0;
#ifdef ZS_PLATFORM_LINUX
    is_terminal = isatty(fileno(stderr));
#endif
    
    // Print timestamp and level
    if (is_terminal && level < ZS_ARRAY_SIZE(LOG_COLORS)) {
        fprintf(stderr, "%s[%s] %s:%d %s: ", 
                LOG_COLORS[level], time_str, filename, line, LOG_LEVEL_STRINGS[level]);
    } else {
        fprintf(stderr, "[%s] %s:%d %s: ", 
                time_str, filename, line, LOG_LEVEL_STRINGS[level]);
    }
    
    // Print the actual message
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    
    // Reset color and add newline
    if (is_terminal && level < ZS_ARRAY_SIZE(LOG_COLORS)) {
        fprintf(stderr, "%s\n", LOG_COLOR_RESET);
    } else {
        fprintf(stderr, "\n");
    }
    
    // Flush to ensure immediate output
    fflush(stderr);
} 