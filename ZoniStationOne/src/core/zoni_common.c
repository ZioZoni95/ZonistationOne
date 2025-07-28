/**
 * @file zoni_common.c
 * @brief Common functions implementation for ZoniStationOne
 */

#include "zoni_common.h"
#include <stdarg.h>
#include <time.h>

// Global log level
static zoni_log_level_t g_log_level = ZONI_LOG_INFO;

// Log level strings
static const char* log_level_strings[] = {
    "ERROR",
    "WARNING", 
    "INFO",
    "DEBUG",
    "TRACE"
};

// Log colors for terminal output
static const char* log_colors[] = {
    "\033[31m",  // Red for ERROR
    "\033[33m",  // Yellow for WARNING
    "\033[32m",  // Green for INFO
    "\033[36m",  // Cyan for DEBUG
    "\033[35m"   // Magenta for TRACE
};

#define LOG_COLOR_RESET "\033[0m"

void zoni_log(zoni_log_level_t level, const char* format, ...) {
    if (level > g_log_level) {
        return;
    }
    
    time_t now;
    struct tm* timeinfo;
    char time_str[32];
    
    time(&now);
    timeinfo = localtime(&now);
    strftime(time_str, sizeof(time_str), "%H:%M:%S", timeinfo);
    
    // Print timestamp and level
    printf("%s[%s] %s: ", log_colors[level], time_str, log_level_strings[level]);
    
    // Print the actual message
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    
    // Reset color and add newline
    printf("%s\n", LOG_COLOR_RESET);
    fflush(stdout);
}

void zoni_set_log_level(zoni_log_level_t level) {
    g_log_level = level;
}

zoni_log_level_t zoni_get_log_level(void) {
    return g_log_level;
}

// Error string conversion
const char* zoni_error_to_string(zoni_error_t error) {
    switch (error) {
        case ZONI_SUCCESS:
            return "Success";
        case ZONI_ERROR_INVALID_PARAMETER:
            return "Invalid parameter";
        case ZONI_ERROR_OUT_OF_MEMORY:
            return "Out of memory";
        case ZONI_ERROR_FILE_NOT_FOUND:
            return "File not found";
        case ZONI_ERROR_INVALID_FORMAT:
            return "Invalid format";
        case ZONI_ERROR_NOT_IMPLEMENTED:
            return "Not implemented";
        case ZONI_ERROR_INITIALIZATION_FAILED:
            return "Initialization failed";
        default:
            return "Unknown error";
    }
}

// Version information
const char* zoni_get_version_string(void) {
    return ZONI_VERSION_STRING;
}

void zoni_get_version_numbers(int* major, int* minor, int* patch) {
    if (major) *major = ZONI_VERSION_MAJOR;
    if (minor) *minor = ZONI_VERSION_MINOR;
    if (patch) *patch = ZONI_VERSION_PATCH;
}

// Memory utilities
void* zoni_malloc(size_t size) {
    void* ptr = malloc(size);
    if (!ptr) {
        zoni_log(ZONI_LOG_ERROR, "Failed to allocate %zu bytes", size);
    }
    return ptr;
}

void* zoni_calloc(size_t count, size_t size) {
    void* ptr = calloc(count, size);
    if (!ptr) {
        zoni_log(ZONI_LOG_ERROR, "Failed to allocate %zu elements of %zu bytes", count, size);
    }
    return ptr;
}

void* zoni_realloc(void* ptr, size_t size) {
    void* new_ptr = realloc(ptr, size);
    if (!new_ptr && size > 0) {
        zoni_log(ZONI_LOG_ERROR, "Failed to reallocate %zu bytes", size);
    }
    return new_ptr;
}

void zoni_free(void* ptr) {
    if (ptr) {
        free(ptr);
    }
}

// String utilities
char* zoni_strdup(const char* str) {
    if (!str) return NULL;
    
    size_t len = strlen(str) + 1;
    char* new_str = zoni_malloc(len);
    if (new_str) {
        memcpy(new_str, str, len);
    }
    return new_str;
}

char* zoni_strndup(const char* str, size_t n) {
    if (!str) return NULL;
    
    size_t len = strlen(str);
    if (len > n) len = n;
    
    char* new_str = zoni_malloc(len + 1);
    if (new_str) {
        memcpy(new_str, str, len);
        new_str[len] = '\0';
    }
    return new_str;
}

// File utilities
bool zoni_file_exists(const char* path) {
    if (!path) return false;
    
    FILE* file = fopen(path, "rb");
    if (file) {
        fclose(file);
        return true;
    }
    return false;
}

size_t zoni_file_size(const char* path) {
    if (!path) return 0;
    
    FILE* file = fopen(path, "rb");
    if (!file) return 0;
    
    fseek(file, 0, SEEK_END);
    size_t size = ftell(file);
    fclose(file);
    
    return size;
}

// Time utilities
u64 zoni_get_time_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (u64)ts.tv_sec * 1000 + (u64)ts.tv_nsec / 1000000;
}

u64 zoni_get_time_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (u64)ts.tv_sec * 1000000 + (u64)ts.tv_nsec / 1000;
}

// Math utilities
u32 zoni_next_power_of_2(u32 n) {
    if (n == 0) return 1;
    
    n--;
    n |= n >> 1;
    n |= n >> 2;
    n |= n >> 4;
    n |= n >> 8;
    n |= n >> 16;
    n++;
    
    return n;
}

bool zoni_is_power_of_2(u32 n) {
    return n != 0 && (n & (n - 1)) == 0;
}

u32 zoni_log2(u32 n) {
    u32 result = 0;
    while (n >>= 1) {
        result++;
    }
    return result;
} 