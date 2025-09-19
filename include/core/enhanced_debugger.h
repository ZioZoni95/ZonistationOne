/*
 * ZonistationOne - Enhanced Debugger Interface (Redux-Style)
 * This adds more sophisticated debugging features similar to PCSX-Redux
 */

#pragma once

#include "core/debug_console.h"
#include <unordered_map>

namespace ZonistationOne {

class EnhancedDebugger {
public:
    EnhancedDebugger(Emulator* emulator);
    ~EnhancedDebugger();
    
    // Redux-style features
    void showAssemblyWindow(uint32_t startPC, int lines = 20);
    void showMemoryEditor(uint32_t address, size_t length = 256);
    void showRegistersPanel();
    void showBreakpointsPanel();
    
    // Enhanced disassembly (like Redux)
    struct DisassemblyLine {
        uint32_t address;
        uint32_t instruction;
        std::string mnemonic;
        std::string operands;
        std::string comment;
        bool hasBreakpoint = false;
        bool isCurrentPC = false;
    };
    
    std::vector<DisassemblyLine> getEnhancedDisassembly(uint32_t startAddr, int count);
    
    // Enhanced memory view (like Redux hex editor)  
    struct MemoryLine {
        uint32_t address;
        std::array<uint8_t, 16> bytes;
        std::string hexString;
        std::string asciiString;
        bool isModified = false;
    };
    
    std::vector<MemoryLine> getMemoryView(uint32_t startAddr, size_t length);
    
    // Interactive features
    void handleMouseClick(int x, int y);  // For future GUI
    void toggleBreakpoint(uint32_t address);
    void followPC();  // Keep assembly view centered on PC
    
    // Display formatting (Redux-style colors/layout)
    void printWithColors(const std::string& text, const std::string& color = "");
    void printAssemblyLine(const DisassemblyLine& line);
    void printMemoryLine(const MemoryLine& line);
    
private:
    Emulator* m_emulator;
    Debugger* m_debugger;
    
    // Display state
    uint32_t m_assemblyViewPC = 0xBFC00000;
    uint32_t m_memoryViewAddr = 0x00000000;
    bool m_followPC = true;
    
    // Enhanced instruction decoding
    std::string decodeInstruction(uint32_t instruction, uint32_t pc);
    std::string getInstructionComment(uint32_t instruction, uint32_t pc);
    std::string formatOperands(uint32_t instruction);
    
    // Color/formatting constants  
    static constexpr const char* COLOR_RESET = "\033[0m";
    static constexpr const char* COLOR_BREAKPOINT = "\033[31m";  // Red
    static constexpr const char* COLOR_PC = "\033[33m";          // Yellow  
    static constexpr const char* COLOR_ADDRESS = "\033[36m";     // Cyan
    static constexpr const char* COLOR_INSTRUCTION = "\033[32m"; // Green
    static constexpr const char* COLOR_COMMENT = "\033[90m";     // Dark gray
};

} // namespace ZonistationOne