/*
 * ZonistationOne - PlayStation 1 Emulator
 * Logging System Implementation
 */

#include "core/logger.h"
#include <cstdarg>
#include <cstdio>
#include <sstream>
#include <iomanip>

namespace ZonistationOne {

Logger::Logger() {
    m_startTime = std::chrono::high_resolution_clock::now();
    
    // Enable all categories by default
    for (int i = 0; i <= static_cast<int>(LogCategory::SYSTEM); ++i) {
        m_categoryEnabled[i] = true;
    }
}

Logger::~Logger() {
    shutdown();
}

Logger& Logger::getInstance() {
    static Logger instance;
    return instance;
}

bool Logger::initialize() {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    // Log without recursive mutex lock
    std::string msg = "ZonistationOne Logger initialized";
    std::string formatted = formatMessage(LogLevel::INFO, LogCategory::SYSTEM, msg);
    std::cout << formatted << std::endl;
    if (m_fileOutput && m_logFile) {
        *m_logFile << formatted << std::endl;
        m_logFile->flush();
    }
    
    return true;
}

void Logger::shutdown() {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (m_logFile) {
        m_logFile->flush();
        m_logFile.reset();
        m_fileOutput = false;
    }
}

void Logger::log(LogLevel level, LogCategory category, const std::string& message) {
    if (!shouldLog(level, category)) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(m_mutex);
    
    std::string formatted = formatMessage(level, category, message);
    
    // Console output
    if (m_consoleOutput) {
        if (level >= LogLevel::ERROR) {
            std::cerr << formatted << std::endl;
        } else {
            std::cout << formatted << std::endl;
        }
    }
    
    // File output
    if (m_fileOutput && m_logFile) {
        *m_logFile << formatted << std::endl;
        m_logFile->flush();
    }
}

void Logger::logf(LogLevel level, LogCategory category, const char* format, ...) {
    if (!shouldLog(level, category)) {
        return;
    }
    
    va_list args;
    va_start(args, format);
    
    // Calculate required buffer size
    va_list args_copy;
    va_copy(args_copy, args);
    int size = std::vsnprintf(nullptr, 0, format, args_copy) + 1;
    va_end(args_copy);
    
    if (size <= 0) {
        va_end(args);
        return;
    }
    
    // Format the string
    std::string buffer(size, '\0');
    std::vsnprintf(&buffer[0], size, format, args);
    va_end(args);
    
    // Remove the null terminator
    buffer.pop_back();
    
    log(level, category, buffer);
}

void Logger::trace(LogCategory category, const std::string& message) {
    log(LogLevel::TRACE, category, message);
}

void Logger::debug(LogCategory category, const std::string& message) {
    log(LogLevel::DEBUG_LEVEL, category, message);
}

void Logger::info(LogCategory category, const std::string& message) {
    log(LogLevel::INFO, category, message);
}

void Logger::warn(LogCategory category, const std::string& message) {
    log(LogLevel::WARN, category, message);
}

void Logger::error(LogCategory category, const std::string& message) {
    log(LogLevel::ERROR, category, message);
}

void Logger::critical(LogCategory category, const std::string& message) {
    log(LogLevel::CRITICAL, category, message);
}

void Logger::enableCategory(LogCategory category) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_categoryEnabled[static_cast<int>(category)] = true;
}

void Logger::disableCategory(LogCategory category) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_categoryEnabled[static_cast<int>(category)] = false;
}

void Logger::enableFileOutput(const std::string& filename) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    m_logFile = std::make_unique<std::ofstream>(filename, std::ios::out | std::ios::app);
    if (m_logFile->is_open()) {
        m_fileOutput = true;
        // Log message without recursive mutex lock
        std::string msg = "File logging enabled: " + filename;
        std::string formatted = formatMessage(LogLevel::INFO, LogCategory::SYSTEM, msg);
        std::cout << formatted << std::endl;
        if (m_fileOutput && m_logFile) {
            *m_logFile << formatted << std::endl;
            m_logFile->flush();
        }
    } else {
        // Log error message without recursive mutex lock
        std::string msg = "Failed to open log file: " + filename;
        std::string formatted = formatMessage(LogLevel::ERROR, LogCategory::SYSTEM, msg);
        std::cerr << formatted << std::endl;
        m_logFile.reset();
    }
}

void Logger::disableFileOutput() {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (m_logFile) {
        info(LogCategory::SYSTEM, "File logging disabled");
        m_logFile->flush();
        m_logFile.reset();
        m_fileOutput = false;
    }
}

std::string Logger::formatMessage(LogLevel level, LogCategory category, const std::string& message) {
    std::ostringstream oss;
    
    // Timestamp
    auto now = std::chrono::high_resolution_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(now - m_startTime);
    double seconds = elapsed.count() / 1000000.0;
    
    oss << "[" << std::fixed << std::setprecision(6) << seconds << "] ";
    
    // Log level
    oss << "[" << levelToString(level) << "] ";
    
    // Category
    oss << "[" << categoryToString(category) << "] ";
    
    // Message
    oss << message;
    
    return oss.str();
}

std::string Logger::levelToString(LogLevel level) {
    switch (level) {
        case LogLevel::TRACE: return "TRACE";
        case LogLevel::DEBUG_LEVEL: return "DEBUG";
        case LogLevel::INFO:  return "INFO ";
        case LogLevel::WARN:  return "WARN ";
        case LogLevel::ERROR: return "ERROR";
        case LogLevel::CRITICAL: return "CRIT ";
        default: return "UNKN ";
    }
}

std::string Logger::categoryToString(LogCategory category) {
    switch (category) {
        case LogCategory::CORE:   return "CORE  ";
        case LogCategory::CPU:    return "CPU   ";
        case LogCategory::MEMORY: return "MEMORY";
        case LogCategory::GPU:    return "GPU   ";
        case LogCategory::SPU:    return "SPU   ";
        case LogCategory::CDROM:  return "CDROM ";
        case LogCategory::INPUT:  return "INPUT ";
        case LogCategory::BIOS:   return "BIOS  ";
        case LogCategory::GUI:    return "GUI   ";
        case LogCategory::DEBUGGER: return "DEBUG ";
        case LogCategory::SYSTEM: return "SYSTEM";
        default: return "UNKN  ";
    }
}

bool Logger::shouldLog(LogLevel level, LogCategory category) {
    return level >= m_minLogLevel && 
           m_categoryEnabled[static_cast<int>(category)];
}

} // namespace ZonistationOne