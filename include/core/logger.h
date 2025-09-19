/*
 * ZonistationOne - PlayStation 1 Emulator
 * Logging System Header
 * Based on PCSX-Redux logging architecture
 */

#pragma once

#include <string>
#include <iostream>
#include <fstream>
#include <memory>
#include <mutex>
#include <cstdint>
#include <chrono>
#include <iomanip>

namespace ZonistationOne {

// Log levels for filtering
enum class LogLevel {
    TRACE = 0,      // Extremely verbose tracing
    DEBUG_LEVEL = 1, // Debug information (renamed to avoid macro conflicts)
    INFO = 2,       // General information
    WARN = 3,       // Warnings
    ERROR = 4,      // Errors
    CRITICAL = 5    // Critical errors
};

// Log categories for different emulator components
enum class LogCategory {
    CORE,           // Core emulator functionality
    CPU,            // MIPS R3000A CPU
    MEMORY,         // Memory management
    GPU,            // Graphics processing
    SPU,            // Sound processing
    CDROM,          // CD-ROM drive
    INPUT,          // Input/controllers
    BIOS,           // BIOS operations
    GUI,            // User interface
    DEBUGGER,       // Debug/breakpoints (renamed to avoid conflicts)
    SYSTEM          // System/OS interface
};

// Forward declaration
class Emulator;

class Logger {
public:
    Logger();
    ~Logger();
    
    // Initialize logging system
    bool initialize();
    void shutdown();
    
    // Core logging functions
    void log(LogLevel level, LogCategory category, const std::string& message);
    void logf(LogLevel level, LogCategory category, const char* format, ...);
    
    // Convenience functions for different log levels
    void trace(LogCategory category, const std::string& message);
    void debug(LogCategory category, const std::string& message);
    void info(LogCategory category, const std::string& message);
    void warn(LogCategory category, const std::string& message);
    void error(LogCategory category, const std::string& message);
    void critical(LogCategory category, const std::string& message);
    
    // Settings
    void setLogLevel(LogLevel level) { m_minLogLevel = level; }
    void enableCategory(LogCategory category);
    void disableCategory(LogCategory category);
    void enableFileOutput(const std::string& filename);
    void disableFileOutput();
    
    // Compile-time enabled/disabled logging
    template<LogCategory category, LogLevel level, bool enabled = true>
    struct CategoryLogger {
        template<typename... Args>
        static void log(const char* format, Args&&... args) {
            if constexpr (enabled) {
                getInstance().logf(level, category, format, std::forward<Args>(args)...);
            }
        }
        
        static void log(const std::string& message) {
            if constexpr (enabled) {
                getInstance().log(level, category, message);
            }
        }
        
        static constexpr bool isEnabled() { return enabled; }
    };
    
    // Get singleton instance
    static Logger& getInstance();
    
private:
    std::string formatMessage(LogLevel level, LogCategory category, const std::string& message);
    std::string levelToString(LogLevel level);
    std::string categoryToString(LogCategory category);
    bool shouldLog(LogLevel level, LogCategory category);
    
    LogLevel m_minLogLevel = LogLevel::INFO;
    bool m_categoryEnabled[static_cast<int>(LogCategory::SYSTEM) + 1] = {true};
    
    std::mutex m_mutex;
    std::unique_ptr<std::ofstream> m_logFile;
    bool m_consoleOutput = true;
    bool m_fileOutput = false;
    
    // Timing
    std::chrono::high_resolution_clock::time_point m_startTime;
};

// Compile-time logger definitions
// These can be enabled/disabled per category to control verbosity

// Enable specialized logging by uncommenting these defines:
#define ENABLE_CPU_INSTRUCTION_LOGGING
// #define ENABLE_MEMORY_ACCESS_LOGGING

// High-frequency loggers (disabled by default to reduce noise)
using CPU_TRACE_LOG = Logger::CategoryLogger<LogCategory::CPU, LogLevel::TRACE, false>;
using MEMORY_TRACE_LOG = Logger::CategoryLogger<LogCategory::MEMORY, LogLevel::TRACE, false>;
using GPU_TRACE_LOG = Logger::CategoryLogger<LogCategory::GPU, LogLevel::TRACE, false>;
using SPU_TRACE_LOG = Logger::CategoryLogger<LogCategory::SPU, LogLevel::TRACE, false>;

// Debug loggers (mostly disabled by default, enable as needed)
using CPU_DEBUG_LOG = Logger::CategoryLogger<LogCategory::CPU, LogLevel::DEBUG_LEVEL, false>;
using MEMORY_DEBUG_LOG = Logger::CategoryLogger<LogCategory::MEMORY, LogLevel::DEBUG_LEVEL, false>;
using GPU_DEBUG_LOG = Logger::CategoryLogger<LogCategory::GPU, LogLevel::DEBUG_LEVEL, false>;
using SPU_DEBUG_LOG = Logger::CategoryLogger<LogCategory::SPU, LogLevel::DEBUG_LEVEL, false>;
using CDROM_DEBUG_LOG = Logger::CategoryLogger<LogCategory::CDROM, LogLevel::DEBUG_LEVEL, false>;

// Info loggers (always enabled)
using CORE_INFO_LOG = Logger::CategoryLogger<LogCategory::CORE, LogLevel::INFO, true>;
using BIOS_INFO_LOG = Logger::CategoryLogger<LogCategory::BIOS, LogLevel::INFO, true>;
using SYSTEM_INFO_LOG = Logger::CategoryLogger<LogCategory::SYSTEM, LogLevel::INFO, true>;

} // namespace ZonistationOne

// Convenience macros for logging with PC/cycle info when available
#define ZONI_LOG_TRACE(category, ...) \
    ZonistationOne::Logger::getInstance().logf(ZonistationOne::LogLevel::TRACE, \
                                               ZonistationOne::LogCategory::category, __VA_ARGS__)

#define ZONI_LOG_DEBUG(category, ...) \
    ZonistationOne::Logger::getInstance().logf(ZonistationOne::LogLevel::DEBUG_LEVEL, \
                                               ZonistationOne::LogCategory::category, __VA_ARGS__)

#define ZONI_LOG_INFO(category, ...) \
    ZonistationOne::Logger::getInstance().logf(ZonistationOne::LogLevel::INFO, \
                                               ZonistationOne::LogCategory::category, __VA_ARGS__)

#define ZONI_LOG_WARN(category, ...) \
    ZonistationOne::Logger::getInstance().logf(ZonistationOne::LogLevel::WARN, \
                                               ZonistationOne::LogCategory::category, __VA_ARGS__)

#define ZONI_LOG_ERROR(category, ...) \
    ZonistationOne::Logger::getInstance().logf(ZonistationOne::LogLevel::ERROR, \
                                               ZonistationOne::LogCategory::category, __VA_ARGS__)

#define ZONI_LOG_CRITICAL(category, ...) \
    ZonistationOne::Logger::getInstance().logf(ZonistationOne::LogLevel::CRITICAL, \
                                               ZonistationOne::LogCategory::category, __VA_ARGS__)

// Specialized CPU instruction logging macros (can be easily disabled)
#ifdef ENABLE_CPU_INSTRUCTION_LOGGING
#define ZONI_LOG_CPU_INSTRUCTION(...) ZONI_LOG_DEBUG(CPU, __VA_ARGS__)
#define ZONI_LOG_CPU_FETCH(...) ZONI_LOG_TRACE(CPU, __VA_ARGS__)
#define ZONI_LOG_CPU_UNKNOWN_INSTRUCTION(...) ZONI_LOG_WARN(CPU, __VA_ARGS__)
#else
#define ZONI_LOG_CPU_INSTRUCTION(...) do {} while(0)
#define ZONI_LOG_CPU_FETCH(...) do {} while(0)
#define ZONI_LOG_CPU_UNKNOWN_INSTRUCTION(...) ZONI_LOG_WARN(CPU, __VA_ARGS__)
#endif

// Memory access logging (very verbose, disabled by default)
#ifdef ENABLE_MEMORY_ACCESS_LOGGING
#define ZONI_LOG_MEMORY_ACCESS(...) ZONI_LOG_TRACE(MEMORY, __VA_ARGS__)
#else
#define ZONI_LOG_MEMORY_ACCESS(...) do {} while(0)
#endif
