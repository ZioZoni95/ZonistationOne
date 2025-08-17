#ifndef LOG_H
#define LOG_H

#include <stdio.h>
#include <stdarg.h>
#include <stdbool.h>

// Log levels
#define LOG_LEVEL_FATAL 0
#define LOG_LEVEL_ERROR 1
#define LOG_LEVEL_WARN  2
#define LOG_LEVEL_INFO  3
#define LOG_LEVEL_DEBUG 4
#define LOG_LEVEL_TRACE 5

// Set the global log level (default: INFO)
void log_set_level(int level);

// Logging function
void log_msg(int level, const char* fmt, ...);

// Convenience macros
#define LOG_FATAL(...) log_msg(LOG_LEVEL_FATAL, __VA_ARGS__)
#define LOG_ERROR(...) log_msg(LOG_LEVEL_ERROR, __VA_ARGS__)
#define LOG_WARN(...)  log_msg(LOG_LEVEL_WARN,  __VA_ARGS__)
#define LOG_INFO(...)  log_msg(LOG_LEVEL_INFO,  __VA_ARGS__)
#define LOG_DEBUG(...) log_msg(LOG_LEVEL_DEBUG, __VA_ARGS__)
#define LOG_TRACE(...) log_msg(LOG_LEVEL_TRACE, __VA_ARGS__)

int log_get_level(void);

// Component-based logging
void log_component(const char* component, int level, const char* fmt, ...);

// Per-component macros (expand as needed)
#define LOG_CPU_DEBUG(fmt, ...) log_component("cpu", LOG_LEVEL_DEBUG, fmt, ##__VA_ARGS__)
#define LOG_CPU_INFO(fmt, ...)  log_component("cpu", LOG_LEVEL_INFO, fmt, ##__VA_ARGS__)
#define LOG_CPU_WARN(fmt, ...)  log_component("cpu", LOG_LEVEL_WARN, fmt, ##__VA_ARGS__)
#define LOG_CPU_ERROR(fmt, ...) log_component("cpu", LOG_LEVEL_ERROR, fmt, ##__VA_ARGS__)
#define LOG_CPU_TRACE(fmt, ...) log_component("cpu", LOG_LEVEL_TRACE, fmt, ##__VA_ARGS__)
#define LOG_CPU_IMPORTANT(...)   log_component("cpu", LOG_LEVEL_INFO, __VA_ARGS__)

#define LOG_GPU_DEBUG(fmt, ...) log_component("gpu", LOG_LEVEL_DEBUG, fmt, ##__VA_ARGS__)
#define LOG_GPU_INFO(fmt, ...)  log_component("gpu", LOG_LEVEL_INFO, fmt, ##__VA_ARGS__)
#define LOG_GPU_WARN(fmt, ...)  log_component("gpu", LOG_LEVEL_WARN, fmt, ##__VA_ARGS__)
#define LOG_GPU_ERROR(fmt, ...) log_component("gpu", LOG_LEVEL_ERROR, fmt, ##__VA_ARGS__)
#define LOG_GPU_TRACE(fmt, ...) log_component("gpu", LOG_LEVEL_TRACE, fmt, ##__VA_ARGS__)

#define LOG_BIOS_DEBUG(fmt, ...) log_component("bios", LOG_LEVEL_DEBUG, fmt, ##__VA_ARGS__)
#define LOG_BIOS_INFO(fmt, ...)  log_component("bios", LOG_LEVEL_INFO, fmt, ##__VA_ARGS__)
#define LOG_BIOS_WARN(fmt, ...)  log_component("bios", LOG_LEVEL_WARN, fmt, ##__VA_ARGS__)
#define LOG_BIOS_ERROR(fmt, ...) log_component("bios", LOG_LEVEL_ERROR, fmt, ##__VA_ARGS__)
#define LOG_BIOS_TRACE(fmt, ...) log_component("bios", LOG_LEVEL_TRACE, fmt, ##__VA_ARGS__)

#define LOG_INTERCONNECT_DEBUG(fmt, ...) log_component("interconnect", LOG_LEVEL_DEBUG, fmt, ##__VA_ARGS__)
#define LOG_INTERCONNECT_INFO(fmt, ...)  log_component("interconnect", LOG_LEVEL_INFO, fmt, ##__VA_ARGS__)
#define LOG_INTERCONNECT_WARN(fmt, ...)  log_component("interconnect", LOG_LEVEL_WARN, fmt, ##__VA_ARGS__)
#define LOG_INTERCONNECT_ERROR(fmt, ...) log_component("interconnect", LOG_LEVEL_ERROR, fmt, ##__VA_ARGS__)
#define LOG_INTERCONNECT_TRACE(fmt, ...) log_component("interconnect", LOG_LEVEL_TRACE, fmt, ##__VA_ARGS__)

#define LOG_RENDERER_DEBUG(fmt, ...) log_component("renderer", LOG_LEVEL_DEBUG, fmt, ##__VA_ARGS__)
#define LOG_RENDERER_INFO(fmt, ...)  log_component("renderer", LOG_LEVEL_INFO, fmt, ##__VA_ARGS__)
#define LOG_RENDERER_WARN(fmt, ...)  log_component("renderer", LOG_LEVEL_WARN, fmt, ##__VA_ARGS__)
#define LOG_RENDERER_ERROR(fmt, ...) log_component("renderer", LOG_LEVEL_ERROR, fmt, ##__VA_ARGS__)
#define LOG_RENDERER_TRACE(fmt, ...) log_component("renderer", LOG_LEVEL_TRACE, fmt, ##__VA_ARGS__)

#define LOG_VRAM_DEBUG(fmt, ...) log_component("vram", LOG_LEVEL_DEBUG, fmt, ##__VA_ARGS__)
#define LOG_VRAM_INFO(fmt, ...)  log_component("vram", LOG_LEVEL_INFO, fmt, ##__VA_ARGS__)
#define LOG_VRAM_WARN(fmt, ...)  log_component("vram", LOG_LEVEL_WARN, fmt, ##__VA_ARGS__)
#define LOG_VRAM_ERROR(fmt, ...) log_component("vram", LOG_LEVEL_ERROR, fmt, ##__VA_ARGS__)
#define LOG_VRAM_TRACE(fmt, ...) log_component("vram", LOG_LEVEL_TRACE, fmt, ##__VA_ARGS__)

#define LOG_CDROM_DEBUG(fmt, ...) log_component("cdrom", LOG_LEVEL_DEBUG, fmt, ##__VA_ARGS__)
#define LOG_CDROM_INFO(fmt, ...)  log_component("cdrom", LOG_LEVEL_INFO, fmt, ##__VA_ARGS__)
#define LOG_CDROM_WARN(fmt, ...)  log_component("cdrom", LOG_LEVEL_WARN, fmt, ##__VA_ARGS__)
#define LOG_CDROM_ERROR(fmt, ...) log_component("cdrom", LOG_LEVEL_ERROR, fmt, ##__VA_ARGS__)
#define LOG_CDROM_TRACE(fmt, ...) log_component("cdrom", LOG_LEVEL_TRACE, fmt, ##__VA_ARGS__)
#define LOG_CDROM_IMPORTANT(...) log_component("cdrom", LOG_LEVEL_INFO, __VA_ARGS__)

#define LOG_DMA_DEBUG(fmt, ...) log_component("dma", LOG_LEVEL_DEBUG, fmt, ##__VA_ARGS__)
#define LOG_DMA_INFO(fmt, ...)  log_component("dma", LOG_LEVEL_INFO, fmt, ##__VA_ARGS__)
#define LOG_DMA_WARN(fmt, ...)  log_component("dma", LOG_LEVEL_WARN, fmt, ##__VA_ARGS__)
#define LOG_DMA_ERROR(fmt, ...) log_component("dma", LOG_LEVEL_ERROR, fmt, ##__VA_ARGS__)
#define LOG_DMA_TRACE(fmt, ...) log_component("dma", LOG_LEVEL_TRACE, fmt, ##__VA_ARGS__)

#define LOG_TIMERS_DEBUG(fmt, ...) log_component("timers", LOG_LEVEL_DEBUG, fmt, ##__VA_ARGS__)
#define LOG_TIMERS_INFO(fmt, ...)  log_component("timers", LOG_LEVEL_INFO, fmt, ##__VA_ARGS__)
#define LOG_TIMERS_WARN(fmt, ...)  log_component("timers", LOG_LEVEL_WARN, fmt, ##__VA_ARGS__)
#define LOG_TIMERS_ERROR(fmt, ...) log_component("timers", LOG_LEVEL_ERROR, fmt, ##__VA_ARGS__)
#define LOG_TIMERS_TRACE(fmt, ...) log_component("timers", LOG_LEVEL_TRACE, fmt, ##__VA_ARGS__)

#define LOG_GTE_DEBUG(fmt, ...) log_component("gte", LOG_LEVEL_DEBUG, fmt, ##__VA_ARGS__)
#define LOG_GTE_INFO(fmt, ...)  log_component("gte", LOG_LEVEL_INFO, fmt, ##__VA_ARGS__)
#define LOG_GTE_WARN(fmt, ...)  log_component("gte", LOG_LEVEL_WARN, fmt, ##__VA_ARGS__)
#define LOG_GTE_ERROR(fmt, ...) log_component("gte", LOG_LEVEL_ERROR, fmt, ##__VA_ARGS__)
#define LOG_GTE_TRACE(fmt, ...) log_component("gte", LOG_LEVEL_TRACE, fmt, ##__VA_ARGS__)

#define LOG_RAM_DEBUG(fmt, ...) log_component("ram", LOG_LEVEL_DEBUG, fmt, ##__VA_ARGS__)
#define LOG_RAM_INFO(fmt, ...)  log_component("ram", LOG_LEVEL_INFO, fmt, ##__VA_ARGS__)
#define LOG_RAM_WARN(fmt, ...)  log_component("ram", LOG_LEVEL_WARN, fmt, ##__VA_ARGS__)
#define LOG_RAM_ERROR(fmt, ...) log_component("ram", LOG_LEVEL_ERROR, fmt, ##__VA_ARGS__)
#define LOG_RAM_TRACE(fmt, ...) log_component("ram", LOG_LEVEL_TRACE, fmt, ##__VA_ARGS__)

#define LOG_DEBUGGER_DEBUG(fmt, ...) log_component("debugger", LOG_LEVEL_DEBUG, fmt, ##__VA_ARGS__)
#define LOG_DEBUGGER_INFO(fmt, ...)  log_component("debugger", LOG_LEVEL_INFO, fmt, ##__VA_ARGS__)
#define LOG_DEBUGGER_WARN(fmt, ...)  log_component("debugger", LOG_LEVEL_WARN, fmt, ##__VA_ARGS__)
#define LOG_DEBUGGER_ERROR(fmt, ...) log_component("debugger", LOG_LEVEL_ERROR, fmt, ##__VA_ARGS__)
#define LOG_DEBUGGER_TRACE(fmt, ...) log_component("debugger", LOG_LEVEL_TRACE, fmt, ##__VA_ARGS__)

// ... add more as needed

// Global log rate-limiting (for debug/trace):
extern int log_rate_limit_enabled;
extern int log_rate_limit_n;
void log_set_rate_limit(int enabled, int n);
// Call log_set_rate_limit(1, N) to enable, 0 to disable.

// Single file logging mode
void log_set_single_file(int enabled);

#endif // LOG_H 