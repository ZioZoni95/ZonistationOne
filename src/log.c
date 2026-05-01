#include "log.h"
#include "rxi_log.h"
#include <string.h>
#include <time.h>
#include <stdint.h>

// --- Global State ---
LogLevel current_log_level = LOG_LEVEL_WARN;
static LogCategoryState category_states[LOG_CAT_COUNT];
static bool log_initialized = false;
static FILE* log_file_handle = NULL;

// --- Custom Sinks (ImGui etc.) ---
#define MAX_LOG_SINKS 8
typedef struct { LogSinkFn fn; void* udata; } SinkEntry;
static SinkEntry sinks[MAX_LOG_SINKS];
static int sink_count = 0;

// --- Category Name Strings (fixed-width for alignment) ---
static const char* category_names[LOG_CAT_COUNT] = {
    "SYSTEM", "CPU", "IRQ", "DMA", "GPU", "CDROM",
    "TIMER", "BIOS", "BUS", "RENDERER", "EVENT",
    "GTE", "VRAM", "RAM", "DEBUG", "MDEC"
};

// Our LogLevel → rxi level (SILENT/SILENT never reaches rxi)
// Our:  SILENT=0 ERROR=1 WARN=2 INFO=3 DEBUG=4 TRACE=5
// rxi:  TRACE=0  DEBUG=1 INFO=2  WARN=3 ERROR=4 FATAL=5
static int to_rxi_level(LogLevel level) {
    static const int map[] = {
        RXI_LOG_FATAL,  /* SILENT — shouldn't reach here */
        RXI_LOG_ERROR,
        RXI_LOG_WARN,
        RXI_LOG_INFO,
        RXI_LOG_DEBUG,
        RXI_LOG_TRACE,
    };
    if (level < 0 || level > LOG_LEVEL_TRACE) return RXI_LOG_TRACE;
    return map[level];
}

// --- Initialization ---
void log_init(void) {
    if (log_initialized) return;

    for (int i = 0; i < LOG_CAT_COUNT; i++) {
        category_states[i].count       = 0;
        category_states[i].limit_first = UINT32_MAX;
        category_states[i].limit_every = 1;
        category_states[i].enabled     = true;
    }

    // rxi: accept everything (our filter decides what reaches here)
    rxi_log_set_level(RXI_LOG_TRACE);
    
    // Disable stdout/stderr output entirely so we only log to ImGui sinks
    rxi_log_set_quiet(true);

    log_initialized = true;
}

// --- Configuration ---
void log_set_level(LogLevel level) { current_log_level = level; }

LogLevel log_get_current_level(void) { return current_log_level; }

void log_set_category_enabled(LogCategory category, bool enabled) {
    if (!log_initialized) log_init();
    if (category >= 0 && category < LOG_CAT_COUNT)
        category_states[category].enabled = enabled;
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

    if (log_file_handle) {
        fclose(log_file_handle);
        log_file_handle = NULL;
    }

    if (filename) {
        log_file_handle = fopen(filename, "a");
        if (!log_file_handle) {
            fprintf(stderr, "[LOG] Failed to open '%s'\n", filename);
            return;
        }
        rxi_log_add_fp(log_file_handle, RXI_LOG_TRACE);
    }
}

void log_set_stderr_quiet(bool quiet) {
    rxi_log_set_quiet(quiet);
}

void log_add_sink(LogSinkFn fn, void* udata) {
    if (sink_count < MAX_LOG_SINKS)
        sinks[sink_count++] = (SinkEntry){ fn, udata };
}

// --- Filter (lightweight, no side effects) ---
bool log_should_print(LogCategory category, LogLevel level) {
    if (!log_initialized) log_init();
    if (level == LOG_LEVEL_SILENT || level > current_log_level) return false;
    if (category < 0 || category >= LOG_CAT_COUNT) return false;
    return category_states[category].enabled;
}

// --- Core Logging ---
void log_print(LogCategory category, LogLevel level, const char* format, ...) {
    if (!log_initialized) log_init();
    if (level == LOG_LEVEL_SILENT || level > current_log_level) return;
    if (category < 0 || category >= LOG_CAT_COUNT) return;
    if (!category_states[category].enabled) return;

    // Rate limiting: increment count, then decide
    uint32_t count = ++category_states[category].count;
    uint32_t first = category_states[category].limit_first;
    uint32_t every = category_states[category].limit_every;
    if (count > first) {
        if (every == 0 || (count - first) % every != 0) return;
    }

    // Format the raw message
    char msg[480];
    va_list args;
    va_start(args, format);
    vsnprintf(msg, sizeof(msg), format, args);
    va_end(args);

    // Dispatch to custom sinks (ImGui) — raw message + category/level
    for (int i = 0; i < sink_count; i++)
        sinks[i].fn((int)category, (int)level, msg, sinks[i].udata);

    // Dispatch to rxi (colored stderr + optional file) — prefixed with [CAT]
    char full[512];
    snprintf(full, sizeof(full), "[%-12s] %s", category_names[category], msg);
    rxi_log_log(to_rxi_level(level), __FILE__, __LINE__, "%s", full);
}

// --- Helpers ---
const char* log_category_name(LogCategory category) {
    if (category >= 0 && category < LOG_CAT_COUNT)
        return category_names[category];
    return "UNKNOWN";
}
