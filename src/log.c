#include "log.h"
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

static int global_log_level = LOG_LEVEL_INFO;
static int log_single_file_mode = 0;
static FILE* single_log_file = NULL;

// Per-component rate limiting counters
static struct {
    char name[32];
    int debug_counter;
    int trace_counter;
} log_counters[16];
static int log_counter_count = 0;

int log_rate_limit_enabled = 1;  // Enable by default
int log_rate_limit_n = 100;      // Aggressive rate limiting

void log_set_rate_limit(int enabled, int n) {
    log_rate_limit_enabled = enabled;
    log_rate_limit_n = n > 0 ? n : 100;
}

// Early filtering - return true if we should skip this log message
static int should_skip_log(const char* component, int level) {
    // Always allow FATAL and ERROR
    if (level <= LOG_LEVEL_ERROR) return 0;
    
    // Always allow WARN and INFO
    if (level <= LOG_LEVEL_INFO) return 0;
    
    // Rate limiting for DEBUG and TRACE
    if (log_rate_limit_enabled && (level == LOG_LEVEL_DEBUG || level == LOG_LEVEL_TRACE)) {
        // Find component index
        int idx = -1;
        for (int i = 0; i < log_counter_count; ++i) {
            if (strcmp(log_counters[i].name, component) == 0) { 
                idx = i; 
                break; 
            }
        }
        
        if (idx == -1) {
            // New component, initialize counters
            if (log_counter_count < 16) {
                idx = log_counter_count;
                strncpy(log_counters[idx].name, component, sizeof(log_counters[idx].name)-1);
                log_counters[idx].name[sizeof(log_counters[idx].name)-1] = '\0';
                log_counters[idx].debug_counter = 0;
                log_counters[idx].trace_counter = 0;
                log_counter_count++;
            } else {
                return 1; // Too many components
            }
        }
        
        if (level == LOG_LEVEL_DEBUG) {
            log_counters[idx].debug_counter++;
            // Log first 10, then every 100th
            if (log_counters[idx].debug_counter <= 10) return 0;
            if (log_counters[idx].debug_counter % 100 == 0) return 0;
            return 1;
        } else if (level == LOG_LEVEL_TRACE) {
            log_counters[idx].trace_counter++;
            // Log first 5, then every 500th
            if (log_counters[idx].trace_counter <= 5) return 0;
            if (log_counters[idx].trace_counter % 500 == 0) return 0;
            return 1;
        }
    }
    
    return 0;
}

void log_set_level(int level) {
    global_log_level = level;
}

void log_set_single_file(int enabled) {
    log_single_file_mode = enabled;
    if (enabled && !single_log_file) {
        single_log_file = fopen("emulator_log.txt", "w");
    } else if (!enabled && single_log_file) {
        fclose(single_log_file);
        single_log_file = NULL;
    }
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

void log_component(const char* component, int level, const char* fmt, ...) {
    if (level > global_log_level) return;
    
    // Early filtering for performance
    if (should_skip_log(component, level)) return;
    
    // Single file mode (if enabled)
    if (log_single_file_mode && single_log_file) {
        va_list args;
        va_start(args, fmt);
        fprintf(single_log_file, "[%s] ", component);
        vfprintf(single_log_file, fmt, args);
        fprintf(single_log_file, "\n");
        va_end(args);
        fflush(single_log_file);
        return;
    }
    
    // Terminal output only - much faster!
    const char* level_str = "INFO";
    switch (level) {
        case LOG_LEVEL_FATAL: level_str = "FATAL"; break;
        case LOG_LEVEL_ERROR: level_str = "ERROR"; break;
        case LOG_LEVEL_WARN:  level_str = "WARN";  break;
        case LOG_LEVEL_INFO:  level_str = "INFO";  break;
        case LOG_LEVEL_DEBUG: level_str = "DEBUG"; break;
        case LOG_LEVEL_TRACE: level_str = "TRACE"; break;
    }
    
    fprintf(stderr, "[%s][%s] ", level_str, component);
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fprintf(stderr, "\n");
} 