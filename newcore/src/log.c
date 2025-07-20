#include "../include/log.h"
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define NC_MAX_COMPONENTS 16
#define NC_MAX_LOG_LINES 10000

static int nc_global_log_level = NC_LOG_INFO;

struct nc_log_file {
    char name[32];
    FILE* file;
    int line_count;
};
static struct nc_log_file nc_log_files[NC_MAX_COMPONENTS];
static int nc_log_file_count = 0;

int nc_log_rate_limit_enabled = 0;
int nc_log_rate_limit_n = 1000;
void nc_log_set_rate_limit(int enabled, int n) {
    nc_log_rate_limit_enabled = enabled;
    nc_log_rate_limit_n = n > 0 ? n : 1000;
}

static FILE* nc_get_log_file(const char* component) {
    for (int i = 0; i < nc_log_file_count; ++i) {
        if (strcmp(nc_log_files[i].name, component) == 0) {
            if (nc_log_files[i].line_count >= NC_MAX_LOG_LINES) {
                fclose(nc_log_files[i].file);
                char old_path[128];
                snprintf(old_path, sizeof(old_path), "logs/%s_old.txt", component);
                char path[128];
                snprintf(path, sizeof(path), "logs/%s.txt", component);
                remove(old_path);
                rename(path, old_path);
                nc_log_files[i].file = fopen(path, "w");
                nc_log_files[i].line_count = 0;
            }
            return nc_log_files[i].file;
        }
    }
    if (nc_log_file_count >= NC_MAX_COMPONENTS) return NULL;
    mkdir("logs", 0777);
    char path[128];
    snprintf(path, sizeof(path), "logs/%s.txt", component);
    FILE* f = fopen(path, "a");
    if (!f) return NULL;
    strncpy(nc_log_files[nc_log_file_count].name, component, sizeof(nc_log_files[nc_log_file_count].name)-1);
    nc_log_files[nc_log_file_count].name[sizeof(nc_log_files[nc_log_file_count].name)-1] = '\0';
    nc_log_files[nc_log_file_count].file = f;
    nc_log_files[nc_log_file_count].line_count = 0;
    ++nc_log_file_count;
    return f;
}

void nc_log_set_level(int level) {
    nc_global_log_level = level;
}

void nc_log_msg(int level, const char* fmt, ...) {
    if (level > nc_global_log_level) return;
    const char* level_str = "INFO";
    switch (level) {
        case NC_LOG_FATAL: level_str = "FATAL"; break;
        case NC_LOG_ERROR: level_str = "ERROR"; break;
        case NC_LOG_WARN:  level_str = "WARN";  break;
        case NC_LOG_INFO:  level_str = "INFO";  break;
        case NC_LOG_DEBUG: level_str = "DEBUG"; break;
        case NC_LOG_TRACE: level_str = "TRACE"; break;
    }
    fprintf(stderr, "[%s] ", level_str);
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fprintf(stderr, "\n");
    if (level == NC_LOG_FATAL) {
        fflush(stderr);
        abort();
    }
}

int nc_log_get_level(void) { return nc_global_log_level; }

void nc_log_component(const char* component, int level, const char* fmt, ...) {
    if (level > nc_global_log_level) return;
    if (nc_log_rate_limit_enabled && (level == NC_LOG_DEBUG || level == NC_LOG_TRACE)) {
        static int counters[NC_MAX_COMPONENTS] = {0};
        int idx = -1;
        for (int i = 0; i < nc_log_file_count; ++i) {
            if (strcmp(nc_log_files[i].name, component) == 0) { idx = i; break; }
        }
        if (idx == -1) {
            idx = nc_log_file_count;
        }
        counters[idx]++;
        if (counters[idx] % nc_log_rate_limit_n != 0 && counters[idx] <= nc_log_rate_limit_n) {
            if (counters[idx] > nc_log_rate_limit_n) return;
        }
    }
    FILE* f = nc_get_log_file(component);
    if (!f) return;
    va_list args;
    va_start(args, fmt);
    vfprintf(f, fmt, args);
    fprintf(f, "\n");
    va_end(args);
    for (int i = 0; i < nc_log_file_count; ++i) {
        if (strcmp(nc_log_files[i].name, component) == 0) {
            nc_log_files[i].line_count++;
            break;
        }
    }
    fflush(f);
} 