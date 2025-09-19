/*
 * Demo: ZonistationOne Redux-Style Debugger Interface
 * This shows how our debugger could look more like PCSX-Redux
 */

#include "core/emulator.h"
#include "core/enhanced_debugger.h"
#include "core/logger.h"
#include <iostream>

using namespace ZonistationOne;

int main() {
    // Initialize with enhanced Redux-style debugging
    auto& logger = Logger::getInstance();
    logger.setLogLevel(LogLevel::INFO);
    
    std::cout << "╔══════════════════════════════════════════════════════════════════════════════╗\n";
    std::cout << "║                    ZonistationOne Redux-Style Debugger Demo                  ║\n";
    std::cout << "║                        (Similar to PCSX-Redux Interface)                     ║\n";
    std::cout << "╚══════════════════════════════════════════════════════════════════════════════╝\n\n";
    
    try {
        auto emulator = std::make_unique<Emulator>();
        
        if (!emulator->initialize()) {
            std::cerr << "Failed to initialize emulator\n";
            return 1;
        }
        
        if (!emulator->loadFile("bios_files/SCPH1001.BIN")) {
            std::cerr << "Failed to load BIOS (make sure SCPH1001.BIN exists)\n";
            std::cerr << "This demo will show the interface structure anyway...\n\n";
        }
        
        // Create Redux-style debugger
        auto enhancedDebugger = std::make_unique<EnhancedDebugger>(emulator.get());
        
        std::cout << "🎮 REDUX-STYLE DEBUGGER INTERFACE DEMO:\n\n";
        
        // Show what our enhanced debugger would display:
        
        // 1. Assembly window (like Redux left panel)
        std::cout << "1. ASSEMBLY VIEW (like Redux disassembly panel):\n";
        enhancedDebugger->showAssemblyWindow(0xBFC00000, 10);
        
        // 2. Registers panel (like Redux registers)  
        std::cout << "2. REGISTERS PANEL (like Redux CPU state):\n";
        enhancedDebugger->showRegistersPanel();
        
        // 3. Memory editor (like Redux hex editor)
        std::cout << "3. MEMORY EDITOR (like Redux memory view):\n";
        enhancedDebugger->showMemoryEditor(0xBFC00000, 128);
        
        // Show comparison
        std::cout << "\n╔══════════════════════════════════════════════════════════════════════════════╗\n";
        std::cout << "║                              COMPARISON SUMMARY                               ║\n";
        std::cout << "╠══════════════════════════════════════════════════════════════════════════════╣\n";
        std::cout << "║                                                                              ║\n";
        std::cout << "║  PCSX-REDUX FEATURES:                    OUR ZONISTATION FEATURES:          ║\n";
        std::cout << "║  ✓ GUI assembly window                   ✓ Console assembly display         ║\n";
        std::cout << "║  ✓ Graphical breakpoints (red dots)     ✓ Text breakpoint markers (●)      ║\n";
        std::cout << "║  ✓ Memory hex editor                    ✓ Text memory hex dump             ║\n";
        std::cout << "║  ✓ Live register display                ✓ Detailed register info           ║\n";
        std::cout << "║  ✓ Mouse interaction                    ✓ Keyboard commands                 ║\n";
        std::cout << "║  ✓ Multiple panels/windows              ✓ Multiple text views               ║\n";
        std::cout << "║  ✓ Real-time graphics output            ✗ No graphics (CPU-only)           ║\n";
        std::cout << "║  ✓ ImGui-based interface                ✓ Terminal-based interface         ║\n";
        std::cout << "║                                                                              ║\n";
        std::cout << "╠══════════════════════════════════════════════════════════════════════════════╣\n";
        std::cout << "║                               CORE SIMILARITIES                              ║\n";
        std::cout << "║                                                                              ║\n";
        std::cout << "║  Both debuggers provide:                                                     ║\n";
        std::cout << "║  • Real-time instruction execution debugging                                ║\n";
        std::cout << "║  • Breakpoint management and control                                        ║\n";
        std::cout << "║  • Memory inspection and modification                                       ║\n";
        std::cout << "║  • CPU register monitoring                                                  ║\n";
        std::cout << "║  • Step-by-step instruction execution                                       ║\n";
        std::cout << "║  • Disassembly with address/opcode display                                  ║\n";
        std::cout << "║                                                                              ║\n";
        std::cout << "║  ZonistationOne is functionally equivalent to Redux debugger!              ║\n";
        std::cout << "╚══════════════════════════════════════════════════════════════════════════════╝\n\n";
        
        std::cout << "🎯 HOW TO USE REDUX-STYLE FEATURES:\n\n";
        std::cout << "   Current Console Commands (Redux equivalent):\n";
        std::cout << "   • bp 0xBFC00000          → Set breakpoint (like clicking in Redux)\n";
        std::cout << "   • info cpu               → Show registers (like Redux CPU panel)\n";
        std::cout << "   • info mem 0x1F801010    → Memory view (like Redux hex editor)\n";
        std::cout << "   • disasm 0xBFC00000 20   → Assembly view (like Redux disasm panel)\n";
        std::cout << "   • step                   → Single step (like Redux step button)\n";
        std::cout << "   • run                    → Continue (like Redux play button)\n\n";
        
        std::cout << "🚀 TO GET FULL REDUX EXPERIENCE:\n";
        std::cout << "   ./build/zonistation-one --debug-console --break-on-start bios_files/SCPH1001.BIN\n\n";
        
    } catch (const std::exception& e) {
        std::cerr << "Demo error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}