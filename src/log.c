#include "log.h"
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static int global_log_level = LOG_LEVEL_INFO;
static int log_single_file_mode = 0;
static FILE* single_log_file = NULL;

#define MAX_COMPONENTS 16
#define MAX_LOG_LINES 5000  // Reduced from 10000 for better performance
#define MAX_LOG_SIZE_MB 10  // Maximum log file size in MB

// Per-component rate limiting
static struct {
    char name[32];
    FILE* file;
    int line_count;
    int debug_counter;
    int trace_counter;
    int last_debug_log;
    int last_trace_log;
} log_files[MAX_COMPONENTS];
static int log_file_count = 0;

int log_rate_limit_enabled = 1;  // Enable by default
int log_rate_limit_n = 100;      // Much more aggressive rate limiting

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
        for (int i = 0; i < log_file_count; ++i) {
            if (strcmp(log_files[i].name, component) == 0) { 
                idx = i; 
                break; 
            }
        }
        
        if (idx == -1) {
            // New component, initialize counters
            if (log_file_count < MAX_COMPONENTS) {
                idx = log_file_count;
                log_files[idx].debug_counter = 0;
                log_files[idx].trace_counter = 0;
                log_files[idx].last_debug_log = 0;
                log_files[idx].last_trace_log = 0;
            } else {
                return 1; // Too many components
            }
        }
        
        if (level == LOG_LEVEL_DEBUG) {
            log_files[idx].debug_counter++;
            // Log first 10, then every 100th
            if (log_files[idx].debug_counter <= 10) return 0;
            if (log_files[idx].debug_counter % 100 == 0) return 0;
            return 1;
        } else if (level == LOG_LEVEL_TRACE) {
            log_files[idx].trace_counter++;
            // Log first 5, then every 500th
            if (log_files[idx].trace_counter <= 5) return 0;
            if (log_files[idx].trace_counter % 500 == 0) return 0;
            return 1;
        }
    }
    
    return 0;
}

static FILE* get_log_file(const char* component) {
    for (int i = 0; i < log_file_count; ++i) {
        if (strcmp(log_files[i].name, component) == 0) {
            // Check for log rotation
            if (log_files[i].line_count >= MAX_LOG_LINES) {
                // Close current file
                fclose(log_files[i].file);
                // Rotate: rename to _old.txt
                char old_path[128];
                snprintf(old_path, sizeof(old_path), "logs/%s_old.txt", component);
                char path[128];
                snprintf(path, sizeof(path), "logs/%s.txt", component);
                remove(old_path); // Remove old backup if exists
                rename(path, old_path); // Rename current to old
                // Open new file
                log_files[i].file = fopen(path, "w");
                log_files[i].line_count = 0;
            }
            return log_files[i].file;
        }
    }
    // Open new file
    if (log_file_count >= MAX_COMPONENTS) return NULL;
    mkdir("logs", 0777); // Ensure logs/ exists
    char path[128];
    snprintf(path, sizeof(path), "logs/%s.txt", component);
    FILE* f = fopen(path, "a");
    if (!f) return NULL;
    strncpy(log_files[log_file_count].name, component, sizeof(log_files[log_file_count].name)-1);
    log_files[log_file_count].name[sizeof(log_files[log_file_count].name)-1] = '\0';
    log_files[log_file_count].file = f;
    log_files[log_file_count].line_count = 0;
    log_files[log_file_count].debug_counter = 0;
    log_files[log_file_count].trace_counter = 0;
    log_files[log_file_count].last_debug_log = 0;
    log_files[log_file_count].last_trace_log = 0;
    ++log_file_count;
    return f;
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
    
    // Single file mode
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
    
    // Per-component file mode
    FILE* f = get_log_file(component);
    if (!f) return;
    
    va_list args;
    va_start(args, fmt);
    vfprintf(f, fmt, args);
    fprintf(f, "\n");
    va_end(args);
    
    // Increment line count for this component
    for (int i = 0; i < log_file_count; ++i) {
        if (strcmp(log_files[i].name, component) == 0) {
            log_files[i].line_count++;
            break;
        }
    }
    fflush(f);
} 