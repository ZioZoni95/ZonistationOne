#pragma once

#define LOG_LEVEL_NONE  0
#define LOG_LEVEL_ERROR 1
#define LOG_LEVEL_WARN  2
#define LOG_LEVEL_INFO  3
#define LOG_LEVEL_DEBUG 4

#ifndef LOG_LEVEL
#define LOG_LEVEL LOG_LEVEL_INFO
#endif

#if LOG_LEVEL >= LOG_LEVEL_ERROR
#define LOG_ERROR(fmt, ...) fprintf(stderr, "[ERROR] " fmt, ##__VA_ARGS__)
#else
#define LOG_ERROR(fmt, ...)
#endif

#if LOG_LEVEL >= LOG_LEVEL_WARN
#define LOG_WARN(fmt, ...) fprintf(stderr, "[WARN] " fmt, ##__VA_ARGS__)
#else
#define LOG_WARN(fmt, ...)
#endif

#if LOG_LEVEL >= LOG_LEVEL_INFO
#define LOG_INFO(fmt, ...) printf("[INFO] " fmt, ##__VA_ARGS__)
#else
#define LOG_INFO(fmt, ...)
#endif

#if LOG_LEVEL >= LOG_LEVEL_DEBUG
#define LOG_DEBUG(fmt, ...) printf("[DEBUG] " fmt, ##__VA_ARGS__)
#else
#define LOG_DEBUG(fmt, ...)
#endif 