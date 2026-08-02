/* SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 ZioZoni95
 *
 * Part of ZoniStation One, a PlayStation 1 emulator.
 * See LICENSE for the full licence text and THIRD-PARTY.md for the
 * components of this project that have other authors.
 */
#include "log.h"
#include "rxi_log.h"
#include <string.h>
#include <time.h>
#include <stdint.h>

// --- Global State ---
/* INFO by default (what CLAUDE.md documents). DEBUG had drifted in as the
 * default and costs real wall-clock time: the hot paths log per DMA transfer,
 * per GP0 command and per MDEC macroblock, which is thousands of formatted
 * lines per frame during FMV playback. Raise it per run with ZS1_LOG_LEVEL. */
LogLevel current_log_level = LOG_LEVEL_INFO;
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
    "GTE", "VRAM", "RAM", "DEBUG", "MDEC", "SPU"
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

/* Lines logged before any sink exists, replayed to the first one that registers.
 *
 * The ImGui log windows are a sink, and debug_ui_init() registers it after
 * init_sdl() has already run — so everything the startup path says, including
 * which GL driver the context came from and whether a ZS1_GPU request was
 * honoured, was dispatched to nobody and only ever visible with ZS1_LOG_STDERR.
 * Precisely the lines you want when the question is "why does this machine
 * render differently", and precisely the ones that were missing from the app.
 *
 * Bounded and never refilled: once a sink has registered, lines go straight
 * through and the backlog is spent. */
#define LOG_EARLY_MAX 256
typedef struct { int category; int level; char msg[480]; } EarlyLine;
static EarlyLine early_lines[LOG_EARLY_MAX];
static int  early_count   = 0;
static bool early_dropped = false;
static bool early_replayed = false;

static void early_record(int category, int level, const char* msg) {
    if (early_replayed) return;
    if (early_count >= LOG_EARLY_MAX) { early_dropped = true; return; }
    EarlyLine* e = &early_lines[early_count++];
    e->category = category;
    e->level    = level;
    snprintf(e->msg, sizeof(e->msg), "%s", msg);
}

void log_add_sink(LogSinkFn fn, void* udata) {
    if (sink_count >= MAX_LOG_SINKS) return;
    sinks[sink_count++] = (SinkEntry){ fn, udata };

    /* Hand the backlog to the first sink only. A second sink registering later
     * would otherwise see startup lines the first one has already shown. */
    if (early_replayed) return;
    early_replayed = true;
    for (int i = 0; i < early_count; i++)
        fn(early_lines[i].category, early_lines[i].level, early_lines[i].msg, udata);
    if (early_dropped)
        fn((int)LOG_CAT_SYSTEM, (int)LOG_LEVEL_WARN,
           "[SYSTEM] Startup log backlog overflowed; earlier lines were dropped "
           "(raise LOG_EARLY_MAX in log.c, or use ZS1_LOG_STDERR=1)", udata);
    early_count = 0;
}

// --- Filter (lightweight, no side effects) ---
bool log_should_print(LogCategory category, LogLevel level) {
    if (!log_initialized) log_init();
    if (level == LOG_LEVEL_SILENT || level > current_log_level) return false;
    if (category < 0 || category >= LOG_CAT_COUNT) return false;
    return category_states[category].enabled;
}

// --- Consecutive-duplicate collapsing ---
// Tight polling loops (e.g. a BIOS-syscall retry loop) can emit the exact
// same line hundreds of times in a row, which is pure noise: no new
// information past the first occurrence, but real overhead (string
// formatting + sink dispatch + file I/O) and a log file that's mostly
// unreadable repetition. Collapse strictly-consecutive identical
// (category, level, message) lines into a single "(xN repeats)" summary,
// flushed either when a different line arrives or after DUP_FLUSH_THRESHOLD
// repeats (so a genuinely infinite loop still surfaces periodic evidence
// it's still running, rather than going silent forever).
#define DUP_FLUSH_THRESHOLD 200
static LogCategory dup_last_category = (LogCategory)-1;
static LogLevel    dup_last_level    = LOG_LEVEL_SILENT;
static char        dup_last_msg[480] = {0};
static uint32_t    dup_repeat_count  = 0;

static void dispatch_line(LogCategory category, LogLevel level, const char* msg) {
    if (sink_count == 0) early_record((int)category, (int)level, msg);
    for (int i = 0; i < sink_count; i++)
        sinks[i].fn((int)category, (int)level, msg, sinks[i].udata);
    char full[560];
    snprintf(full, sizeof(full), "[%-12s] %s", category_names[category], msg);
    rxi_log_log(to_rxi_level(level), __FILE__, __LINE__, "%s", full);
}

static void dup_flush(void) {
    if (dup_repeat_count == 0) return;
    char note[560];
    snprintf(note, sizeof(note), "%s  (x%u repeats)", dup_last_msg, dup_repeat_count + 1);
    dispatch_line(dup_last_category, dup_last_level, note);
    dup_repeat_count = 0;
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

    if (category == dup_last_category && level == dup_last_level && strcmp(msg, dup_last_msg) == 0) {
        dup_repeat_count++;
        if (dup_repeat_count < DUP_FLUSH_THRESHOLD) return;
        dup_flush(); // periodic "still repeating" note, then keep collapsing
        return;
    }

    dup_flush(); // a different line arrived: surface the pending summary first
    dup_last_category = category;
    dup_last_level    = level;
    strncpy(dup_last_msg, msg, sizeof(dup_last_msg) - 1);
    dup_last_msg[sizeof(dup_last_msg) - 1] = '\0';

    dispatch_line(category, level, msg);
}

// --- BIOS/game TTY output — always emitted, bypasses the level/category gate ---
void log_print_tty(const char* msg) {
    if (!log_initialized) log_init();
    if (!msg) return;
    dispatch_line(LOG_CAT_BIOS, LOG_LEVEL_INFO, msg);
}

// --- Helpers ---
const char* log_category_name(LogCategory category) {
    if (category >= 0 && category < LOG_CAT_COUNT)
        return category_names[category];
    return "UNKNOWN";
}
