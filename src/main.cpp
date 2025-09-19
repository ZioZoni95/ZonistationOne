/*
 * ZonistationOne - PlayStation 1 Emulator
 * Copyright (C) 2025
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include "core/emulator.h"
#include "core/logger.h"
#include "core/debug_console.h"
#include "core/debugger.h"

void printUsage(const char* programName) {
    std::cout << "ZonistationOne PS1 Emulator\n";
    std::cout << "Usage: " << programName << " [OPTIONS] [FILE]\n\n";
    std::cout << "Options:\n";
    std::cout << "  -h, --help           Show this help message\n";
    std::cout << "  -v, --verbose        Enable verbose (DEBUG) logging\n";
    std::cout << "  -q, --quiet          Enable quiet (WARN+) logging\n";
    std::cout << "  -t, --trace          Enable trace logging (very verbose)\n";
    std::cout << "  --log-file FILE      Enable file logging to FILE\n";
    std::cout << "  --log-level LEVEL    Set log level (TRACE|DEBUG|INFO|WARN|ERROR|CRITICAL)\n";
    std::cout << "  --debug              Enable interactive debugger mode\n";
    std::cout << "  --debug-console      Start with debug console interface\n";
    std::cout << "  --break-on-start     Set breakpoint at BIOS entry (0xBFC00000)\n";
    std::cout << "  --step-mode          Start emulation in step-by-step mode\n\n";
    std::cout << "FILE can be a BIOS file (.bin) or ISO image (.iso/.cue)\n\n";
    std::cout << "Debug Commands (when --debug-console is active):\n";
    std::cout << "  help                 Show debug commands\n";
    std::cout << "  run                  Start/resume emulation\n";
    std::cout << "  pause                Pause emulation\n";
    std::cout << "  step                 Execute one instruction\n";
    std::cout << "  bp <addr>            Set breakpoint at address (hex)\n";
    std::cout << "  info cpu             Show CPU state\n";
    std::cout << "  info mem <addr>      Show memory at address\n";
    std::cout << "  disasm <addr> <cnt>  Disassemble instructions\n";
    std::cout << "  quit                 Exit emulator\n";
}

ZonistationOne::LogLevel parseLogLevel(const std::string& level) {
    if (level == "TRACE") return ZonistationOne::LogLevel::TRACE;
    if (level == "DEBUG") return ZonistationOne::LogLevel::DEBUG_LEVEL;
    if (level == "INFO") return ZonistationOne::LogLevel::INFO;
    if (level == "WARN") return ZonistationOne::LogLevel::WARN;
    if (level == "ERROR") return ZonistationOne::LogLevel::ERROR;
    if (level == "CRITICAL") return ZonistationOne::LogLevel::CRITICAL;
    
    std::cerr << "Invalid log level: " << level << std::endl;
    std::cerr << "Valid levels: TRACE, DEBUG, INFO, WARN, ERROR, CRITICAL" << std::endl;
    exit(1);
}

int main(int argc, char* argv[]) {
    // Initialize logging first
    auto& logger = ZonistationOne::Logger::getInstance();
    
    // Default log configuration
    ZonistationOne::LogLevel logLevel = ZonistationOne::LogLevel::INFO;
    std::string logFile;
    std::string inputFile;
    bool enableFileLogging = false;
    bool debugMode = false;
    bool debugConsole = false;
    bool breakOnStart = false;
    bool stepMode = false;
    
    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        
        if (arg == "-h" || arg == "--help") {
            printUsage(argv[0]);
            return 0;
        } else if (arg == "-v" || arg == "--verbose") {
            logLevel = ZonistationOne::LogLevel::DEBUG_LEVEL;
        } else if (arg == "-q" || arg == "--quiet") {
            logLevel = ZonistationOne::LogLevel::WARN;
        } else if (arg == "-t" || arg == "--trace") {
            logLevel = ZonistationOne::LogLevel::TRACE;
        } else if (arg == "--log-file") {
            if (i + 1 < argc) {
                logFile = argv[++i];
                enableFileLogging = true;
            } else {
                std::cerr << "Error: --log-file requires a filename" << std::endl;
                return 1;
            }
        } else if (arg == "--log-level") {
            if (i + 1 < argc) {
                logLevel = parseLogLevel(argv[++i]);
            } else {
                std::cerr << "Error: --log-level requires a level" << std::endl;
                return 1;
            }
        } else if (arg == "--debug") {
            debugMode = true;
        } else if (arg == "--debug-console") {
            debugConsole = true;
            debugMode = true;
        } else if (arg == "--break-on-start") {
            breakOnStart = true;
            debugMode = true;
        } else if (arg == "--step-mode") {
            stepMode = true;
            debugMode = true;
        } else if (arg.substr(0, 1) == "-") {
            std::cerr << "Unknown option: " << arg << std::endl;
            printUsage(argv[0]);
            return 1;
        } else {
            // This should be the input file
            if (inputFile.empty()) {
                inputFile = arg;
            } else {
                std::cerr << "Error: Multiple input files specified" << std::endl;
                return 1;
            }
        }
    }
    
    // Configure logging
    logger.setLogLevel(logLevel);
    
    // Enable file logging if requested or debug build
    #ifdef DEBUG
    if (!enableFileLogging) {
        logFile = "zonistation-debug.log";
        enableFileLogging = true;
    }
    #endif
    
    if (enableFileLogging) {
        logger.enableFileOutput(logFile.empty() ? "zonistation.log" : logFile);
    }
    
    ZONI_LOG_INFO(SYSTEM, "ZonistationOne PS1 Emulator - Starting...");
    ZONI_LOG_INFO(SYSTEM, "Log level set to: %s", 
                  (logLevel == ZonistationOne::LogLevel::TRACE) ? "TRACE" :
                  (logLevel == ZonistationOne::LogLevel::DEBUG_LEVEL) ? "DEBUG" :
                  (logLevel == ZonistationOne::LogLevel::INFO) ? "INFO" :
                  (logLevel == ZonistationOne::LogLevel::WARN) ? "WARN" :
                  (logLevel == ZonistationOne::LogLevel::ERROR) ? "ERROR" : "CRITICAL");
    
    try {
        auto emulator = std::make_unique<ZonistationOne::Emulator>();
        
        // Initialize emulator first
        if (!emulator->initialize()) {
            ZONI_LOG_CRITICAL(SYSTEM, "Failed to initialize emulator");
            return 1;
        }
        
        if (!inputFile.empty()) {
            // Load BIOS or game file if provided
            ZONI_LOG_INFO(SYSTEM, "Loading file: %s", inputFile.c_str());
            
            if (!emulator->loadFile(inputFile)) {
                ZONI_LOG_ERROR(SYSTEM, "Failed to load file: %s", inputFile.c_str());
                return 1;
            }
        }
        
        ZONI_LOG_INFO(SYSTEM, "Emulator initialized successfully");
        
        // Set up debugging if requested
        std::unique_ptr<ZonistationOne::DebugConsole> console;
        
        if (debugMode) {
            // Enable debug features
            auto debugger = emulator->getDebugger();
            if (debugger && !debugConsole) {
                // Enable tracing for non-console debug mode
                debugger->setInstructionTrace(true);
                ZONI_LOG_INFO(SYSTEM, "Debug mode enabled with instruction tracing");
            }
        }
        
        if (debugConsole) {
            // Interactive debug console mode
            console = std::make_unique<ZonistationOne::DebugConsole>(emulator.get());
            console->setBreakOnStart(breakOnStart);
            console->setStepMode(stepMode);
            
            ZONI_LOG_INFO(SYSTEM, "Starting debug console interface...");
            console->start();
            
            // Debug console will handle emulation control
            // Keep the main thread alive until console quits
            while (console->isRunning()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            
        } else {
            // Normal emulation mode
            emulator->run();
        }
        
    } catch (const std::exception& e) {
        ZONI_LOG_CRITICAL(SYSTEM, "Fatal error: %s", e.what());
        return 1;
    }
    
    return 0;
}