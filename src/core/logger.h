/*
 * ZonistationOne - PlayStation One Emulator
 * Logging System
 */

#ifndef PSX_LOGGER_H
#define PSX_LOGGER_H

#include <stdio.h>
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    LOG_DEBUG = 0,
    LOG_INFO,
    LOG_WARN,
    LOG_ERROR,
    LOG_FATAL
} log_level_t;

/* Logging functions */
void logger_init(log_level_t min_level);
void logger_shutdown(void);
void logger_set_file(FILE *file);
void logger_set_level(log_level_t level);

void log_message(log_level_t level, const char *file, int line, const char *func, const char *format, ...);

/* Convenience macros */
#define log_debug(...) log_message(LOG_DEBUG, __FILE__, __LINE__, __func__, __VA_ARGS__)
#define log_info(...)  log_message(LOG_INFO,  __FILE__, __LINE__, __func__, __VA_ARGS__)
#define log_warn(...)  log_message(LOG_WARN,  __FILE__, __LINE__, __func__, __VA_ARGS__)
#define log_error(...) log_message(LOG_ERROR, __FILE__, __LINE__, __func__, __VA_ARGS__)
#define log_fatal(...) log_message(LOG_FATAL, __FILE__, __LINE__, __func__, __VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif /* PSX_LOGGER_H */