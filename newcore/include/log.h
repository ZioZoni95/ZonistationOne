#ifndef NEWCORE_LOG_H
#define NEWCORE_LOG_H

#include <stdio.h>
#include <stdarg.h>
#include <stdbool.h>

// Log level definitions
#define NC_LOG_FATAL 0
#define NC_LOG_ERROR 1
#define NC_LOG_WARN  2
#define NC_LOG_INFO  3
#define NC_LOG_DEBUG 4
#define NC_LOG_TRACE 5

// Set the global log level
void nc_log_set_level(int level);

// Log a message at a given level
void nc_log_msg(int level, const char* fmt, ...);

// Log level convenience macros
#define NC_LOGF(...) nc_log_msg(NC_LOG_FATAL, __VA_ARGS__)
#define NC_LOGE(...) nc_log_msg(NC_LOG_ERROR, __VA_ARGS__)
#define NC_LOGW(...) nc_log_msg(NC_LOG_WARN,  __VA_ARGS__)
#define NC_LOGI(...) nc_log_msg(NC_LOG_INFO,  __VA_ARGS__)
#define NC_LOGD(...) nc_log_msg(NC_LOG_DEBUG, __VA_ARGS__)
#define NC_LOGT(...) nc_log_msg(NC_LOG_TRACE, __VA_ARGS__)

int nc_log_get_level(void);

// Component-based logging
void nc_log_component(const char* component, int level, const char* fmt, ...);

// Example per-component macros (expand as needed)
#define NC_LOG_CPU_DEBUG(fmt, ...) nc_log_component("cpu", NC_LOG_DEBUG, fmt, ##__VA_ARGS__)
#define NC_LOG_GPU_INFO(fmt, ...)  nc_log_component("gpu", NC_LOG_INFO, fmt, ##__VA_ARGS__)
// ... add more as needed

// Rate-limiting for debug/trace logs
extern int nc_log_rate_limit_enabled;
extern int nc_log_rate_limit_n;
void nc_log_set_rate_limit(int enabled, int n);

#endif // NEWCORE_LOG_H 