/*
 * ZonistationOne - PlayStation 1 Emulator
 * Debugger Test Program
 */

#include "core/emulator.h"
#include "core/debugger.h"
#include "core/logger.h"
#include <iostream>
#include <memory>

using namespace ZonistationOne;

int main() {
    // Initialize logging
    auto& logger = Logger::getInstance();
    logger.setLogLevel(LogLevel::DEBUG_LEVEL);
    
    ZONI_LOG_INFO(SYSTEM, "ZonistationOne Debugger Test - Starting...");
    
    try {
        // Create and initialize emulator
        auto emulator = std::make_unique<Emulator>();
        
        if (!emulator->initialize()) {
            ZONI_LOG_ERROR(SYSTEM, "Failed to initialize emulator");
            return 1;
        }
        
        // Get debugger instance
        auto debugger = emulator->getDebugger();
        if (!debugger) {
            ZONI_LOG_ERROR(SYSTEM, "No debugger available");
            return 1;
        }
        
        ZONI_LOG_INFO(SYSTEM, "Testing debugger functionality...");
        
        // Test 1: Add execution breakpoint at BIOS entry
        uint32_t bp1 = debugger->addBreakpoint(0xBFC00000, Debugger::BreakpointType::EXECUTION, "BIOS Entry");
        ZONI_LOG_INFO(SYSTEM, "Added breakpoint #%u at BIOS entry", bp1);
        
        // Test 2: Add memory read breakpoint
        uint32_t bp2 = debugger->addBreakpoint(0x00000000, Debugger::BreakpointType::READ, "RAM Read");
        ZONI_LOG_INFO(SYSTEM, "Added breakpoint #%u for RAM read", bp2);
        
        // Test 3: List all breakpoints
        auto breakpoints = debugger->getAllBreakpoints();
        ZONI_LOG_INFO(SYSTEM, "Total breakpoints: %zu", breakpoints.size());
        
        for (const auto* bp : breakpoints) {
            const char* typeStr = "";
            switch (bp->type) {
                case Debugger::BreakpointType::EXECUTION: typeStr = "EXEC"; break;
                case Debugger::BreakpointType::READ: typeStr = "READ"; break;
                case Debugger::BreakpointType::WRITE: typeStr = "WRITE"; break;
                case Debugger::BreakpointType::ACCESS: typeStr = "ACCESS"; break;
            }
            ZONI_LOG_INFO(SYSTEM, "  BP: %s at 0x%08X (%s) - %s", 
                         typeStr, bp->address, bp->label.c_str(), 
                         bp->enabled ? "enabled" : "disabled");
        }
        
        // Test 4: CPU state dump
        ZONI_LOG_INFO(SYSTEM, "Testing CPU state dump:");
        debugger->dumpCPUState();
        
        // Test 5: Memory dump
        ZONI_LOG_INFO(SYSTEM, "Testing memory dump (BIOS area):");
        debugger->dumpMemoryRegion(0xBFC00000, 64);
        
        // Test 6: Disassembly
        ZONI_LOG_INFO(SYSTEM, "Testing disassembly:");
        auto disasm = debugger->disassembleRegion(0xBFC00000, 5);
        for (size_t i = 0; i < disasm.size(); ++i) {
            ZONI_LOG_INFO(SYSTEM, "  0x%08X: %s", 0xBFC00000 + (i * 4), disasm[i].c_str());
        }
        
        // Test 7: Enable/disable breakpoints
        debugger->enableBreakpoint(bp1, false);
        debugger->enableBreakpoint(bp2, false);
        ZONI_LOG_INFO(SYSTEM, "Disabled breakpoints");
        
        // Test 8: Remove breakpoints
        debugger->removeBreakpoint(bp1);
        debugger->removeBreakpoint(bp2);
        ZONI_LOG_INFO(SYSTEM, "Removed breakpoints");
        
        // Test 9: Clear all breakpoints
        debugger->clearAllBreakpoints();
        ZONI_LOG_INFO(SYSTEM, "Cleared all breakpoints");
        
        ZONI_LOG_INFO(SYSTEM, "Debugger test completed successfully!");
        
    } catch (const std::exception& e) {
        ZONI_LOG_ERROR(SYSTEM, "Test failed: %s", e.what());
        return 1;
    }
    
    return 0;
}