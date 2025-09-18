/*
 * ZonistationOne - PlayStation One Emulator
 * Logging System Implementation
 */

#include "logger.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

static struct {
    FILE *output_file;
    log_level_t min_level;
    int initialized;
} logger_state = {
    .output_file = NULL,
    .min_level = LOG_INFO,
    .initialized = 0
};

static const char *level_names[] = {
    "DEBUG", "INFO", "WARN", "ERROR", "FATAL"
};

static const char *level_colors[] = {
    "\033[36m",  /* DEBUG - Cyan */
    "\033[32m",  /* INFO  - Green */
    "\033[33m",  /* WARN  - Yellow */
    "\033[31m",  /* ERROR - Red */
    "\033[35m"   /* FATAL - Magenta */
};

#define COLOR_RESET "\033[0m"

void logger_init(log_level_t min_level) {
    logger_state.output_file = stderr;
    logger_state.min_level = min_level;
    logger_state.initialized = 1;
}

void logger_shutdown(void) {
    if (logger_state.output_file && logger_state.output_file != stderr && logger_state.output_file != stdout) {
        fclose(logger_state.output_file);
    }
    logger_state.output_file = NULL;
    logger_state.initialized = 0;
}

void logger_set_file(FILE *file) {
    if (logger_state.initialized) {
        logger_state.output_file = file ? file : stderr;
    }
}

void logger_set_level(log_level_t level) {
    if (logger_state.initialized) {
        logger_state.min_level = level;
    }
}

void log_message(log_level_t level, const char *file, int line, const char *func, const char *format, ...) {
    if (!logger_state.initialized || level < logger_state.min_level) {
        return;
    }
    
    /* Get timestamp */
    time_t now;
    time(&now);
    struct tm *tm_info = localtime(&now);
    
    /* Extract filename from path */
    const char *filename = strrchr(file, '/');
    if (filename) {
        filename++; /* Skip the '/' */
    } else {
        filename = file;
    }
    
    /* Check if we should use colors (only for stderr/stdout) */
    int use_colors = (logger_state.output_file == stderr || logger_state.output_file == stdout);
    
    /* Print timestamp and log level */
    if (use_colors) {
        fprintf(logger_state.output_file, "[%02d:%02d:%02d] %s%-5s%s ",
                tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec,
                level_colors[level], level_names[level], COLOR_RESET);
    } else {
        fprintf(logger_state.output_file, "[%02d:%02d:%02d] %-5s ",
                tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec,
                level_names[level]);
    }
    
    /* Print source location for debug/error levels */
    if (level == LOG_DEBUG || level >= LOG_ERROR) {
        fprintf(logger_state.output_file, "%s:%d:%s() - ", filename, line, func);
    }
    
    /* Print the actual message */
    va_list args;
    va_start(args, format);
    vfprintf(logger_state.output_file, format, args);
    va_end(args);
    
    fprintf(logger_state.output_file, "\n");
    fflush(logger_state.output_file);
    
    /* Exit on fatal errors */
    if (level == LOG_FATAL) {
        exit(EXIT_FAILURE);
    }
}