/*
 * ZonistationOne - PlayStation 1 Emulator
 * Debug System Header
 */

#pragma once

#include <cstdint>
#include <vector>
#include <unordered_map>
#include <functional>
#include <string>

namespace ZonistationOne {

class CPU;
class Memory;
class Emulator;

class Debugger {
public:
    enum class BreakpointType {
        EXECUTION,  // Break on instruction execution
        READ,       // Break on memory read
        WRITE,      // Break on memory write
        ACCESS      // Break on read or write
    };
    
    struct Breakpoint {
        uint32_t address;
        BreakpointType type;
        bool enabled = true;
        uint32_t hitCount = 0;
        std::string condition; // Future: conditional breakpoints
        std::string label;     // User-defined label
        
        Breakpoint(uint32_t addr, BreakpointType t, const std::string& lbl = "")
            : address(addr), type(t), label(lbl) {}
    };
    
    // Step mode for debugging
    enum class StepMode {
        NONE,           // Normal execution
        STEP_INTO,      // Step one instruction
        STEP_OVER,      // Step over function calls  
        STEP_OUT,       // Step out of current function
        RUN_TO_CURSOR   // Run until specific address
    };

public:
    Debugger(Emulator* emulator);
    ~Debugger();
    
    // Breakpoint management
    uint32_t addBreakpoint(uint32_t address, BreakpointType type, const std::string& label = "");
    bool removeBreakpoint(uint32_t id);
    bool enableBreakpoint(uint32_t id, bool enabled);
    const Breakpoint* getBreakpoint(uint32_t id) const;
    std::vector<const Breakpoint*> getAllBreakpoints() const;
    void clearAllBreakpoints();
    
    // Execution control
    void pause();
    void resume();
    void step();
    void stepOver();
    void stepOut();
    void runToCursor(uint32_t address);
    
    bool isPaused() const { return m_paused; }
    StepMode getStepMode() const { return m_stepMode; }
    
    // Breakpoint checking (called by CPU/Memory)
    bool checkExecutionBreakpoint(uint32_t address);
    bool checkMemoryBreakpoint(uint32_t address, BreakpointType type);
    
    // CPU state inspection
    void dumpCPUState() const;
    void dumpMemoryRegion(uint32_t startAddress, uint32_t length) const;
    void dumpStack(uint32_t stackPointer, int depth = 16) const;
    
    // Disassembly
    std::string disassembleInstruction(uint32_t address) const;
    std::vector<std::string> disassembleRegion(uint32_t startAddress, int count) const;
    
    // Symbol management (for future use)
    void addSymbol(uint32_t address, const std::string& name);
    void removeSymbol(uint32_t address);
    std::string getSymbol(uint32_t address) const;
    
    // Watchpoints for specific values
    void addWatchpoint(uint32_t address, const std::string& label = "");
    void removeWatchpoint(uint32_t address);
    void updateWatchpoints();
    
    // Debug output control
    void setTraceEnabled(bool enabled) { m_traceEnabled = enabled; }
    void setInstructionTrace(bool enabled) { m_instructionTrace = enabled; }
    bool isTraceEnabled() const { return m_traceEnabled; }
    
private:
    void onBreakpointHit(const Breakpoint& bp);
    void handleStep();
    uint32_t normalizeAddress(uint32_t address) const;
    
    Emulator* m_emulator;
    CPU* m_cpu;
    Memory* m_memory;
    
    // Breakpoints
    std::unordered_map<uint32_t, Breakpoint> m_breakpoints;
    uint32_t m_nextBreakpointId = 1;
    
    // Execution state
    bool m_paused = false;
    StepMode m_stepMode = StepMode::NONE;
    uint32_t m_stepOverAddress = 0;
    uint32_t m_runToAddress = 0;
    int m_stepOutDepth = 0;
    
    // Symbols and watchpoints
    std::unordered_map<uint32_t, std::string> m_symbols;
    std::unordered_map<uint32_t, std::pair<uint32_t, std::string>> m_watchpoints; // address -> (last_value, label)
    
    // Debug settings
    bool m_traceEnabled = false;
    bool m_instructionTrace = false;
};

} // namespace ZonistationOne