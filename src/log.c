#include "log.h"
#include <string.h>
#include <time.h>
#include <stdint.h>

// PCSX ReARMed-style logging system implementation

// --- Global State ---
static LogLevel current_log_level = LOG_LEVEL_INFO;
static LogCategoryState category_states[LOG_CAT_COUNT];
static FILE* log_output_file = NULL;
static bool log_initialized = false;

// --- Category Names ---
static const char* category_names[LOG_CAT_COUNT] = {
    "SYSTEM",
    "CPU",
    "IRQ", 
    "DMA",
    "GPU",
    "CDROM",
    "TIMER",
    "BIOS",
    "INTERCONNECT",
    "RENDERER",
    "EVENT",
    "GTE",
    "VRAM",
    "RAM",
    "DEBUG",
    "MDEC"
};

// --- Level Names ---
static const char* level_names[] = {
    "SILENT",
    "ERROR",
    "WARN",
    "INFO", 
    "DEBUG",
    "TRACE"
};

// --- Initialization ---
void log_init(void) {
    if (log_initialized) return;
    
    // Initialize all categories with default settings
    for (int i = 0; i < LOG_CAT_COUNT; i++) {
        category_states[i].count = 0;
        category_states[i].limit_first = UINT32_MAX; // No rate limiting
        category_states[i].limit_every = 1;          // Log every message
        category_states[i].enabled = true;           // All categories enabled by default
    }
    
    log_output_file = stderr; // Default output
    setvbuf(stderr, NULL, _IONBF, 0); // Unbuffered output to avoid overhead
    log_initialized = true;
}

// --- Configuration Functions ---
void log_set_level(LogLevel level) {
    current_log_level = level;
}

LogLevel log_get_current_level(void) {
    return current_log_level;
}

void log_set_category_enabled(LogCategory category, bool enabled) {
    if (!log_initialized) log_init();
    if (category >= 0 && category < LOG_CAT_COUNT) {
        category_states[category].enabled = enabled;
    }
}

void log_set_rate_limit(LogCategory category, uint32_t first_n, uint32_t every_n) {
    if (!log_initialized) log_init();
    if (category >= 0 && category < LOG_CAT_COUNT) {
        category_states[category].limit_first = first_n;
        category_states[category].limit_every = every_n;
    }
}

void log_set_output_file(const char* filename) {
    if (!log_initialized) log_init();
    
    if (log_output_file && log_output_file != stderr && log_output_file != stdout) {
        fclose(log_output_file);
    }
    
    if (filename) {
        log_output_file = fopen(filename, "a");
        if (!log_output_file) {
            log_output_file = stderr;
            fprintf(stderr, "[LOG] Failed to open log file '%s', using stderr\n", filename);
        } else {
            setvbuf(log_output_file, NULL, _IONBF, 0); // Unbuffered for files too
        }
    } else {
        log_output_file = stderr;
    }
}

// --- Rate Limiting Logic ---
bool log_should_print(LogCategory category, LogLevel level) {
    if (!log_initialized) log_init();
    
    // Check global log level
    if (level > current_log_level) {
        return false;
    }
    
    // Check category bounds
    if (category < 0 || category >= LOG_CAT_COUNT) {
        return false;
    }
    
    // Check if category is enabled
    if (!category_states[category].enabled) {
        return false;
    }
    
    return true;
}

// --- Core Logging Function ---
void log_print(LogCategory category, LogLevel level, const char* format, ...) {
    if (!log_should_print(category, level)) {
        return;
    }
    
    // Get current time
    time_t now = time(NULL);
    struct tm* tm_info = localtime(&now);
    
    // Print timestamp, level, and category
    fprintf(log_output_file, "[%02d:%02d:%02d][%s][%s] ", 
            tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec,
            level_names[level],
            category_names[category]);
    
    // Print the actual message
    va_list args;
    va_start(args, format);
    vfprintf(log_output_file, format, args);
    va_end(args);
    
    // Add newline if not present
    if (format[strlen(format) - 1] != '\n') {
        fprintf(log_output_file, "\n");
    }
    
    // Flush immediately to avoid buffering overhead
    fflush(log_output_file);
}

// --- Helper Functions ---
const char* log_category_name(LogCategory category) {
    if (category >= 0 && category < LOG_CAT_COUNT) {
        return category_names[category];
    }
    return "UNKNOWN";
}

// --- Debug Information ---
void log_print_stats(void) {
    if (!log_initialized) log_init();
    
    fprintf(log_output_file, "\n=== Logging Statistics ===\n");
    for (int i = 0; i < LOG_CAT_COUNT; i++) {
        LogCategoryState* state = &category_states[i];
        fprintf(log_output_file, "%12s: %8u messages (enabled=%s, limits=%u/%u)\n",
                category_names[i], state->count, 
                state->enabled ? "yes" : "no",
                state->limit_first, state->limit_every);
    }
    fprintf(log_output_file, "==========================\n\n");
    fflush(log_output_file);
}