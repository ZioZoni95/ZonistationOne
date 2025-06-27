#include "log.h"
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static int global_log_level = LOG_LEVEL_INFO;

#define MAX_COMPONENTS 16
#define MAX_LOG_LINES 10000

static struct {
    char name[32];
    FILE* file;
    int line_count;
} log_files[MAX_COMPONENTS];
static int log_file_count = 0;

int log_rate_limit_enabled = 0;
int log_rate_limit_n = 1000;
void log_set_rate_limit(int enabled, int n) {
    log_rate_limit_enabled = enabled;
    log_rate_limit_n = n > 0 ? n : 1000;
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
    ++log_file_count;
    return f;
}

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

void log_component(const char* component, int level, const char* fmt, ...) {
    if (level > global_log_level) return;
    // Global rate-limiting for DEBUG/TRACE
    if (log_rate_limit_enabled && (level == LOG_LEVEL_DEBUG || level == LOG_LEVEL_TRACE)) {
        static int counters[MAX_COMPONENTS] = {0};
        int idx = -1;
        for (int i = 0; i < log_file_count; ++i) {
            if (strcmp(log_files[i].name, component) == 0) { idx = i; break; }
        }
        if (idx == -1) {
            idx = log_file_count;
            // Will be incremented when file is opened below
        }
        counters[idx]++;
        if (counters[idx] % log_rate_limit_n != 0 && counters[idx] <= log_rate_limit_n) {
            // Only log the first N, then every Nth
            if (counters[idx] > log_rate_limit_n) return;
        }
    }
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