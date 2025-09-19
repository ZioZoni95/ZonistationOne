/*
 * ZonistationOne - PlayStation 1 Emulator
 * Enhanced Debugger Test with Real BIOS
 */

#include "core/emulator.h"
#include "core/debugger.h"
#include "core/logger.h"
#include <iostream>
#include <memory>
#include <thread>
#include <chrono>

using namespace ZonistationOne;

int main() {
    // Initialize logging for maximum detail
    auto& logger = Logger::getInstance();
    logger.setLogLevel(LogLevel::DEBUG_LEVEL);
    logger.enableFileOutput("debugger-bios-test.log");
    
    ZONI_LOG_INFO(SYSTEM, "ZonistationOne Enhanced Debugger Test - BIOS Edition");
    ZONI_LOG_INFO(SYSTEM, "This test will load real PlayStation BIOS and demonstrate debugging");
    
    try {
        // Create and initialize emulator
        auto emulator = std::make_unique<Emulator>();
        
        if (!emulator->initialize()) {
            ZONI_LOG_ERROR(SYSTEM, "Failed to initialize emulator");
            return 1;
        }
        
        // Load BIOS file
        ZONI_LOG_INFO(SYSTEM, "Loading PlayStation BIOS...");
        if (!emulator->loadFile("bios_files/SCPH1001.BIN")) {
            ZONI_LOG_ERROR(SYSTEM, "Failed to load BIOS file");
            ZONI_LOG_ERROR(SYSTEM, "Make sure SCPH1001.BIN is in bios_files/ directory");
            return 1;
        }
        
        // Get debugger instance
        auto debugger = emulator->getDebugger();
        if (!debugger) {
            ZONI_LOG_ERROR(SYSTEM, "No debugger available");
            return 1;
        }
        
        ZONI_LOG_INFO(SYSTEM, "=== ENHANCED DEBUGGER TEST WITH REAL BIOS ===");
        
        // Test 1: Set breakpoint at BIOS entry point (first instruction)
        ZONI_LOG_INFO(SYSTEM, "Setting breakpoint at BIOS entry point...");
        uint32_t bp_entry = debugger->addBreakpoint(0xBFC00000, Debugger::BreakpointType::EXECUTION, "BIOS Entry Point");
        
        // Test 2: Set breakpoint after a few instructions 
        uint32_t bp_later = debugger->addBreakpoint(0xBFC00010, Debugger::BreakpointType::EXECUTION, "After First Instructions");
        
        // Test 3: Set memory write breakpoint on I/O register
        uint32_t bp_io = debugger->addBreakpoint(0x1F801010, Debugger::BreakpointType::WRITE, "I/O Register Write");
        
        // Test 4: Enable instruction tracing
        ZONI_LOG_INFO(SYSTEM, "Enabling instruction tracing...");
        debugger->setInstructionTrace(true);
        
        // Test 5: Show initial CPU state
        ZONI_LOG_INFO(SYSTEM, "Initial CPU state:");
        debugger->dumpCPUState();
        
        // Test 6: Show BIOS memory content (first 128 bytes)
        ZONI_LOG_INFO(SYSTEM, "BIOS memory content (first 128 bytes):");
        debugger->dumpMemoryRegion(0xBFC00000, 128);
        
        // Test 7: Disassemble first 10 BIOS instructions
        ZONI_LOG_INFO(SYSTEM, "First 10 BIOS instructions:");
        auto disasm = debugger->disassembleRegion(0xBFC00000, 10);
        for (size_t i = 0; i < disasm.size(); ++i) {
            ZONI_LOG_INFO(SYSTEM, "  [%zu] %s", i, disasm[i].c_str());
        }
        
        // Test 8: Start emulation with debugging
        ZONI_LOG_INFO(SYSTEM, "Starting emulation with active breakpoints...");
        ZONI_LOG_INFO(SYSTEM, "Note: Emulation will pause at breakpoints and show debugging info");
        
        // Create a separate thread to run emulation for a short time
        std::thread emulation_thread([&emulator]() {
            // Run for a very short time to hit a few breakpoints
            emulator->run();
        });
        
        // Let it run for a short time
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        // Pause emulation
        emulator->pause();
        
        // Wait for thread to finish
        if (emulation_thread.joinable()) {
            emulation_thread.join();
        }
        
        // Test 9: Show final CPU state after some execution
        ZONI_LOG_INFO(SYSTEM, "CPU state after brief execution:");
        debugger->dumpCPUState();
        
        // Test 10: Check breakpoint hit counts
        ZONI_LOG_INFO(SYSTEM, "Breakpoint statistics:");
        auto breakpoints = debugger->getAllBreakpoints();
        for (const auto* bp : breakpoints) {
            const char* typeStr = "";
            switch (bp->type) {
                case Debugger::BreakpointType::EXECUTION: typeStr = "EXEC"; break;
                case Debugger::BreakpointType::READ: typeStr = "READ"; break;
                case Debugger::BreakpointType::WRITE: typeStr = "WRITE"; break;
                case Debugger::BreakpointType::ACCESS: typeStr = "ACCESS"; break;
            }
            ZONI_LOG_INFO(SYSTEM, "  %s at 0x%08X (%s): Hit %u times", 
                         typeStr, bp->address, bp->label.c_str(), bp->hitCount);
        }
        
        // Test 11: Memory region after execution (check if anything changed)
        ZONI_LOG_INFO(SYSTEM, "I/O register area after execution:");
        debugger->dumpMemoryRegion(0x1F801000, 64);
        
        // Cleanup
        debugger->clearAllBreakpoints();
        
        ZONI_LOG_INFO(SYSTEM, "Enhanced debugger test with BIOS completed successfully!");
        ZONI_LOG_INFO(SYSTEM, "Check 'debugger-bios-test.log' for detailed execution log");
        
    } catch (const std::exception& e) {
        ZONI_LOG_ERROR(SYSTEM, "Test failed: %s", e.what());
        return 1;
    }
    
    return 0;
}