#ifndef LOG_H
#define LOG_H

#include <stdio.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>

// PCSX ReARMed-style logging system with component categories and rate limiting

// --- Log Levels ---
typedef enum {
    LOG_LEVEL_SILENT = 0,
    LOG_LEVEL_ERROR = 1,
    LOG_LEVEL_WARN = 2,
    LOG_LEVEL_INFO = 3,
    LOG_LEVEL_DEBUG = 4,
    LOG_LEVEL_TRACE = 5
} LogLevel;

// --- Log Categories (PCSX ReARMed style) ---
typedef enum {
    LOG_CAT_SYSTEM = 0,     // General system messages
    LOG_CAT_CPU = 1,        // CPU execution, instructions
    LOG_CAT_IRQ = 2,        // Interrupt handling
    LOG_CAT_DMA = 3,        // DMA transfers
    LOG_CAT_GPU = 4,        // Graphics operations
    LOG_CAT_CDROM = 5,      // CD-ROM operations
    LOG_CAT_TIMER = 6,      // Timer events
    LOG_CAT_BIOS = 7,       // BIOS calls and operations
    LOG_CAT_INTERCONNECT = 8, // Memory/IO interconnect
    LOG_CAT_RENDERER = 9,   // OpenGL rendering
    LOG_CAT_EVENT = 10,     // Event scheduler
    LOG_CAT_GTE = 11,       // Geometry Transform Engine
    LOG_CAT_VRAM = 12,      // VRAM operations
    LOG_CAT_RAM = 13,       // RAM operations
    LOG_CAT_DEBUG = 14,     // Debugger operations
    LOG_CAT_COUNT = 15      // Total number of categories
} LogCategory;

// --- Rate Limiting Structure ---
typedef struct {
    uint32_t count;         // How many times this has been logged
    uint32_t limit_first;   // Log first N messages
    uint32_t limit_every;   // Then log every Nth message
    bool enabled;           // Is this category enabled?
} LogCategoryState;

// --- Core Logging Functions ---
void log_init(void);
void log_set_level(LogLevel level);
void log_set_category_enabled(LogCategory category, bool enabled);
void log_set_rate_limit(LogCategory category, uint32_t first_n, uint32_t every_n);
void log_set_output_file(const char* filename);
bool log_should_print(LogCategory category, LogLevel level);

// Internal logging function
void log_print(LogCategory category, LogLevel level, const char* format, ...);

// --- Direct Component-Specific Macros (PCSX ReARMed style) ---
// These call log_print directly to avoid circular definitions
#define LOG_SYSTEM_ERROR(...) log_print(LOG_CAT_SYSTEM, LOG_LEVEL_ERROR, __VA_ARGS__)
#define LOG_SYSTEM_WARN(...) log_print(LOG_CAT_SYSTEM, LOG_LEVEL_WARN, __VA_ARGS__)
#define LOG_SYSTEM_INFO(...) log_print(LOG_CAT_SYSTEM, LOG_LEVEL_INFO, __VA_ARGS__)
#define LOG_SYSTEM_DEBUG(...) log_print(LOG_CAT_SYSTEM, LOG_LEVEL_DEBUG, __VA_ARGS__)
#define LOG_SYSTEM_TRACE(...) log_print(LOG_CAT_SYSTEM, LOG_LEVEL_TRACE, __VA_ARGS__)

#define LOG_CPU_ERROR(...) log_print(LOG_CAT_CPU, LOG_LEVEL_ERROR, __VA_ARGS__)
#define LOG_CPU_WARN(...) log_print(LOG_CAT_CPU, LOG_LEVEL_WARN, __VA_ARGS__)
#define LOG_CPU_INFO(...) log_print(LOG_CAT_CPU, LOG_LEVEL_INFO, __VA_ARGS__)
#define LOG_CPU_DEBUG(...) log_print(LOG_CAT_CPU, LOG_LEVEL_DEBUG, __VA_ARGS__)
#define LOG_CPU_TRACE(...) log_print(LOG_CAT_CPU, LOG_LEVEL_TRACE, __VA_ARGS__)

#define LOG_IRQ_ERROR(...) log_print(LOG_CAT_IRQ, LOG_LEVEL_ERROR, __VA_ARGS__)
#define LOG_IRQ_WARN(...) log_print(LOG_CAT_IRQ, LOG_LEVEL_WARN, __VA_ARGS__)
#define LOG_IRQ_INFO(...) log_print(LOG_CAT_IRQ, LOG_LEVEL_INFO, __VA_ARGS__)
#define LOG_IRQ_DEBUG(...) log_print(LOG_CAT_IRQ, LOG_LEVEL_DEBUG, __VA_ARGS__)
#define LOG_IRQ_TRACE(...) log_print(LOG_CAT_IRQ, LOG_LEVEL_TRACE, __VA_ARGS__)

#define LOG_DMA_ERROR(...) log_print(LOG_CAT_DMA, LOG_LEVEL_ERROR, __VA_ARGS__)
#define LOG_DMA_WARN(...) log_print(LOG_CAT_DMA, LOG_LEVEL_WARN, __VA_ARGS__)
#define LOG_DMA_INFO(...) log_print(LOG_CAT_DMA, LOG_LEVEL_INFO, __VA_ARGS__)
#define LOG_DMA_DEBUG(...) log_print(LOG_CAT_DMA, LOG_LEVEL_DEBUG, __VA_ARGS__)
#define LOG_DMA_TRACE(...) log_print(LOG_CAT_DMA, LOG_LEVEL_TRACE, __VA_ARGS__)

#define LOG_GPU_ERROR(...) log_print(LOG_CAT_GPU, LOG_LEVEL_ERROR, __VA_ARGS__)
#define LOG_GPU_WARN(...) log_print(LOG_CAT_GPU, LOG_LEVEL_WARN, __VA_ARGS__)
#define LOG_GPU_INFO(...) log_print(LOG_CAT_GPU, LOG_LEVEL_INFO, __VA_ARGS__)
#define LOG_GPU_DEBUG(...) log_print(LOG_CAT_GPU, LOG_LEVEL_DEBUG, __VA_ARGS__)
#define LOG_GPU_TRACE(...) log_print(LOG_CAT_GPU, LOG_LEVEL_TRACE, __VA_ARGS__)

#define LOG_CDROM_ERROR(...) log_print(LOG_CAT_CDROM, LOG_LEVEL_ERROR, __VA_ARGS__)
#define LOG_CDROM_WARN(...) log_print(LOG_CAT_CDROM, LOG_LEVEL_WARN, __VA_ARGS__)
#define LOG_CDROM_INFO(...) log_print(LOG_CAT_CDROM, LOG_LEVEL_INFO, __VA_ARGS__)
#define LOG_CDROM_DEBUG(...) log_print(LOG_CAT_CDROM, LOG_LEVEL_DEBUG, __VA_ARGS__)
#define LOG_CDROM_TRACE(...) log_print(LOG_CAT_CDROM, LOG_LEVEL_TRACE, __VA_ARGS__)

#define LOG_TIMER_ERROR(...) log_print(LOG_CAT_TIMER, LOG_LEVEL_ERROR, __VA_ARGS__)
#define LOG_TIMER_WARN(...) log_print(LOG_CAT_TIMER, LOG_LEVEL_WARN, __VA_ARGS__)
#define LOG_TIMER_INFO(...) log_print(LOG_CAT_TIMER, LOG_LEVEL_INFO, __VA_ARGS__)
#define LOG_TIMER_DEBUG(...) log_print(LOG_CAT_TIMER, LOG_LEVEL_DEBUG, __VA_ARGS__)
#define LOG_TIMER_TRACE(...) log_print(LOG_CAT_TIMER, LOG_LEVEL_TRACE, __VA_ARGS__)

#define LOG_SIO_ERROR(...) log_print(LOG_CAT_SYSTEM, LOG_LEVEL_ERROR, __VA_ARGS__)
#define LOG_SIO_WARN(...) log_print(LOG_CAT_SYSTEM, LOG_LEVEL_WARN, __VA_ARGS__)
#define LOG_SIO_INFO(...) log_print(LOG_CAT_SYSTEM, LOG_LEVEL_INFO, __VA_ARGS__)
#define LOG_SIO_DEBUG(...) log_print(LOG_CAT_SYSTEM, LOG_LEVEL_DEBUG, __VA_ARGS__)
#define LOG_SIO_TRACE(...) log_print(LOG_CAT_SYSTEM, LOG_LEVEL_TRACE, __VA_ARGS__)

#define LOG_BIOS_ERROR(...) log_print(LOG_CAT_BIOS, LOG_LEVEL_ERROR, __VA_ARGS__)
#define LOG_BIOS_WARN(...) log_print(LOG_CAT_BIOS, LOG_LEVEL_WARN, __VA_ARGS__)
#define LOG_BIOS_INFO(...) log_print(LOG_CAT_BIOS, LOG_LEVEL_INFO, __VA_ARGS__)
#define LOG_BIOS_DEBUG(...) log_print(LOG_CAT_BIOS, LOG_LEVEL_DEBUG, __VA_ARGS__)
#define LOG_BIOS_TRACE(...) log_print(LOG_CAT_BIOS, LOG_LEVEL_TRACE, __VA_ARGS__)

#define LOG_INTERCONNECT_ERROR(...) log_print(LOG_CAT_INTERCONNECT, LOG_LEVEL_ERROR, __VA_ARGS__)
#define LOG_INTERCONNECT_WARN(...) log_print(LOG_CAT_INTERCONNECT, LOG_LEVEL_WARN, __VA_ARGS__)
#define LOG_INTERCONNECT_INFO(...) log_print(LOG_CAT_INTERCONNECT, LOG_LEVEL_INFO, __VA_ARGS__)
#define LOG_INTERCONNECT_DEBUG(...) log_print(LOG_CAT_INTERCONNECT, LOG_LEVEL_DEBUG, __VA_ARGS__)
#define LOG_INTERCONNECT_TRACE(...) log_print(LOG_CAT_INTERCONNECT, LOG_LEVEL_TRACE, __VA_ARGS__)

#define LOG_RENDERER_ERROR(...) log_print(LOG_CAT_RENDERER, LOG_LEVEL_ERROR, __VA_ARGS__)
#define LOG_RENDERER_WARN(...) log_print(LOG_CAT_RENDERER, LOG_LEVEL_WARN, __VA_ARGS__)
#define LOG_RENDERER_INFO(...) log_print(LOG_CAT_RENDERER, LOG_LEVEL_INFO, __VA_ARGS__)
#define LOG_RENDERER_DEBUG(...) log_print(LOG_CAT_RENDERER, LOG_LEVEL_DEBUG, __VA_ARGS__)
#define LOG_RENDERER_TRACE(...) log_print(LOG_CAT_RENDERER, LOG_LEVEL_TRACE, __VA_ARGS__)

#define LOG_EVENT_ERROR(...) log_print(LOG_CAT_EVENT, LOG_LEVEL_ERROR, __VA_ARGS__)
#define LOG_EVENT_WARN(...) log_print(LOG_CAT_EVENT, LOG_LEVEL_WARN, __VA_ARGS__)
#define LOG_EVENT_INFO(...) log_print(LOG_CAT_EVENT, LOG_LEVEL_INFO, __VA_ARGS__)
#define LOG_EVENT_DEBUG(...) log_print(LOG_CAT_EVENT, LOG_LEVEL_DEBUG, __VA_ARGS__)
#define LOG_EVENT_TRACE(...) log_print(LOG_CAT_EVENT, LOG_LEVEL_TRACE, __VA_ARGS__)

#define LOG_GTE_ERROR(...) log_print(LOG_CAT_GTE, LOG_LEVEL_ERROR, __VA_ARGS__)
#define LOG_GTE_WARN(...) log_print(LOG_CAT_GTE, LOG_LEVEL_WARN, __VA_ARGS__)
#define LOG_GTE_INFO(...) log_print(LOG_CAT_GTE, LOG_LEVEL_INFO, __VA_ARGS__)
#define LOG_GTE_DEBUG(...) log_print(LOG_CAT_GTE, LOG_LEVEL_DEBUG, __VA_ARGS__)
#define LOG_GTE_TRACE(...) log_print(LOG_CAT_GTE, LOG_LEVEL_TRACE, __VA_ARGS__)

#define LOG_VRAM_ERROR(...) log_print(LOG_CAT_VRAM, LOG_LEVEL_ERROR, __VA_ARGS__)
#define LOG_VRAM_WARN(...) log_print(LOG_CAT_VRAM, LOG_LEVEL_WARN, __VA_ARGS__)
#define LOG_VRAM_INFO(...) log_print(LOG_CAT_VRAM, LOG_LEVEL_INFO, __VA_ARGS__)
#define LOG_VRAM_DEBUG(...) log_print(LOG_CAT_VRAM, LOG_LEVEL_DEBUG, __VA_ARGS__)
#define LOG_VRAM_TRACE(...) log_print(LOG_CAT_VRAM, LOG_LEVEL_TRACE, __VA_ARGS__)

#define LOG_RAM_ERROR(...) log_print(LOG_CAT_RAM, LOG_LEVEL_ERROR, __VA_ARGS__)
#define LOG_RAM_WARN(...) log_print(LOG_CAT_RAM, LOG_LEVEL_WARN, __VA_ARGS__)
#define LOG_RAM_INFO(...) log_print(LOG_CAT_RAM, LOG_LEVEL_INFO, __VA_ARGS__)
#define LOG_RAM_DEBUG(...) log_print(LOG_CAT_RAM, LOG_LEVEL_DEBUG, __VA_ARGS__)
#define LOG_RAM_TRACE(...) log_print(LOG_CAT_RAM, LOG_LEVEL_TRACE, __VA_ARGS__)

#define LOG_DEBUGGER_ERROR(...) log_print(LOG_CAT_DEBUG, LOG_LEVEL_ERROR, __VA_ARGS__)
#define LOG_DEBUGGER_WARN(...) log_print(LOG_CAT_DEBUG, LOG_LEVEL_WARN, __VA_ARGS__)
#define LOG_DEBUGGER_INFO(...) log_print(LOG_CAT_DEBUG, LOG_LEVEL_INFO, __VA_ARGS__)
#define LOG_DEBUGGER_DEBUG(...) log_print(LOG_CAT_DEBUG, LOG_LEVEL_DEBUG, __VA_ARGS__)
#define LOG_DEBUGGER_TRACE(...) log_print(LOG_CAT_DEBUG, LOG_LEVEL_TRACE, __VA_ARGS__)

// Legacy compatibility for custom macros
#define LOG_TIMERS_ERROR(...) LOG_TIMER_ERROR(__VA_ARGS__)
#define LOG_TIMERS_WARN(...) LOG_TIMER_WARN(__VA_ARGS__)
#define LOG_TIMERS_INFO(...) LOG_TIMER_INFO(__VA_ARGS__)
#define LOG_TIMERS_DEBUG(...) LOG_TIMER_DEBUG(__VA_ARGS__)




// --- Simple fallback macros for old code (no categories) ---
#define LOG_INFO(...)    log_print(LOG_CAT_SYSTEM, LOG_LEVEL_INFO, __VA_ARGS__)
#define LOG_ERROR(...)   log_print(LOG_CAT_SYSTEM, LOG_LEVEL_ERROR, __VA_ARGS__)
#define LOG_WARN(...)    log_print(LOG_CAT_SYSTEM, LOG_LEVEL_WARN, __VA_ARGS__)
#define LOG_DEBUG(...)   log_print(LOG_CAT_SYSTEM, LOG_LEVEL_DEBUG, __VA_ARGS__)
#define LOG_TRACE(...)   log_print(LOG_CAT_SYSTEM, LOG_LEVEL_TRACE, __VA_ARGS__)

// Logging macro usage:
// Use LOG_<CATEGORY>_<LEVEL>(...) for all logging calls, e.g. LOG_CPU_INFO(...), LOG_CDROM_WARN(...)
// Only use fallback LOG_INFO/LOG_ERROR/etc. for generic system messages.
// Avoid legacy macros like LOG_CPU_IMPORTANT, LOG_CDROM_IMPORTANT.

// Old function compatibility
LogLevel log_get_current_level(void);
#define log_get_level() log_get_current_level()
const char* log_category_name(LogCategory category);

#endif // LOG_H