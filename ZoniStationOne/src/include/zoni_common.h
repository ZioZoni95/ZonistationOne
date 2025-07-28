/**
 * @file zoni_common.h
 * @brief Common definitions and includes for ZoniStationOne
 * 
 * This file contains the basic types, constants, and common includes
 * used throughout the ZoniStationOne emulator.
 */

#ifndef ZONI_COMMON_H
#define ZONI_COMMON_H

#ifdef __cplusplus
extern "C" {
#endif

// System includes
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#include <math.h>
#include <time.h>
#include <ctype.h>

// Platform-specific includes
#ifdef _WIN32
    #include <windows.h>
    #define strcasecmp _stricmp
    #define strncasecmp _strnicmp
#else
    #include <unistd.h>
    #include <sys/types.h>
    #include <sys/stat.h>
#endif

// Basic type definitions
typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;
typedef int64_t s64;

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

// Boolean type (for compatibility)
#ifndef bool
    typedef uint8_t bool;
    #define true 1
    #define false 0
#endif

// Common constants
#define ZONI_VERSION_MAJOR 0
#define ZONI_VERSION_MINOR 1
#define ZONI_VERSION_PATCH 0

#define ZONI_VERSION_STRING "0.1.0"

// PlayStation hardware constants
#define PSX_MEMORY_SIZE (8 * 1024 * 1024)  // 8MB RAM
#define PSX_BIOS_SIZE (512 * 1024)         // 512KB BIOS
#define PSX_SCRATCHPAD_SIZE (1024)         // 1KB scratchpad

// Memory regions
#define PSX_RAM_BASE 0x00000000
#define PSX_BIOS_BASE 0x1FC00000
#define PSX_SCRATCHPAD_BASE 0x1F800000

// CPU constants
#define PSX_CPU_CLOCK_NTSC 33868800  // 33.8688 MHz
#define PSX_CPU_CLOCK_PAL 33868800   // Same for PAL

// Video constants
#define PSX_SCREEN_WIDTH 320
#define PSX_SCREEN_HEIGHT 240
#define PSX_SCREEN_WIDTH_PAL 320
#define PSX_SCREEN_HEIGHT_PAL 288

// Audio constants
#define PSX_SPU_SAMPLE_RATE 44100
#define PSX_SPU_BUFFER_SIZE 1024

// Error codes
typedef enum {
    ZONI_SUCCESS = 0,
    ZONI_ERROR_INVALID_PARAMETER = -1,
    ZONI_ERROR_OUT_OF_MEMORY = -2,
    ZONI_ERROR_FILE_NOT_FOUND = -3,
    ZONI_ERROR_INVALID_FORMAT = -4,
    ZONI_ERROR_NOT_IMPLEMENTED = -5,
    ZONI_ERROR_INITIALIZATION_FAILED = -6
} zoni_error_t;

// Logging levels
typedef enum {
    ZONI_LOG_ERROR = 0,
    ZONI_LOG_WARNING = 1,
    ZONI_LOG_INFO = 2,
    ZONI_LOG_DEBUG = 3,
    ZONI_LOG_TRACE = 4
} zoni_log_level_t;

// Logging function
void zoni_log(zoni_log_level_t level, const char* format, ...);

// Additional utility functions
void zoni_set_log_level(zoni_log_level_t level);
zoni_log_level_t zoni_get_log_level(void);
const char* zoni_error_to_string(zoni_error_t error);
const char* zoni_get_version_string(void);
void zoni_get_version_numbers(int* major, int* minor, int* patch);

// Memory utilities
void* zoni_malloc(size_t size);
void* zoni_calloc(size_t count, size_t size);
void* zoni_realloc(void* ptr, size_t size);
void zoni_free(void* ptr);

// String utilities
char* zoni_strdup(const char* str);
char* zoni_strndup(const char* str, size_t n);

// File utilities
bool zoni_file_exists(const char* path);
size_t zoni_file_size(const char* path);

// Time utilities
u64 zoni_get_time_ms(void);
u64 zoni_get_time_us(void);

// Math utilities
u32 zoni_next_power_of_2(u32 n);
bool zoni_is_power_of_2(u32 n);
u32 zoni_log2(u32 n);

// Utility macros
#define ZONI_UNUSED(x) (void)(x)
#define ZONI_ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))
#define ZONI_MIN(a, b) ((a) < (b) ? (a) : (b))
#define ZONI_MAX(a, b) ((a) > (b) ? (a) : (b))
#define ZONI_CLAMP(x, min, max) ZONI_MIN(ZONI_MAX(x, min), max)

// Assertion macro
#ifdef DEBUG
    #define ZONI_ASSERT(condition) assert(condition)
#else
    #define ZONI_ASSERT(condition) ((void)0)
#endif

#ifdef __cplusplus
}
#endif

#endif // ZONI_COMMON_H 