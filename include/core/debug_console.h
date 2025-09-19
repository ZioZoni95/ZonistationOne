/*
 * ZonistationOne - PlayStation 1 Emulator
 * Debug Console Interface Header
 */

#pragma once

#include <string>
#include <vector>
#include <memory>
#include <thread>
#include <atomic>

namespace ZonistationOne {

class Emulator;
class Debugger;

class DebugConsole {
public:
    DebugConsole(Emulator* emulator);
    ~DebugConsole();
    
    // Console control
    void start();
    void stop();
    bool isRunning() const { return m_running; }
    
    // Setup debugging features
    void setBreakOnStart(bool enabled) { m_breakOnStart = enabled; }
    void setStepMode(bool enabled) { m_stepMode = enabled; }
    
    // Execute a debug command
    bool executeCommand(const std::string& command);
    
private:
    void consoleThread();
    void printPrompt();
    void printHelp();
    void showStatus();
    
    // Command handlers
    bool handleRun(const std::vector<std::string>& args);
    bool handlePause(const std::vector<std::string>& args);
    bool handleStep(const std::vector<std::string>& args);
    bool handleBreakpoint(const std::vector<std::string>& args);
    bool handleInfo(const std::vector<std::string>& args);
    bool handleDisasm(const std::vector<std::string>& args);
    bool handleQuit(const std::vector<std::string>& args);
    bool handleHelp(const std::vector<std::string>& args);
    bool handleContinue(const std::vector<std::string>& args);
    bool handleList(const std::vector<std::string>& args);
    
    // Utilities
    std::vector<std::string> tokenize(const std::string& str);
    uint32_t parseAddress(const std::string& addr);
    void printCPUState();
    void printMemory(uint32_t address, uint32_t length = 64);
    void printDisassembly(uint32_t address, int count = 10);
    
    Emulator* m_emulator;
    Debugger* m_debugger;
    
    std::atomic<bool> m_running;
    std::atomic<bool> m_quit;
    std::unique_ptr<std::thread> m_consoleThread;
    
    // Debug settings
    bool m_breakOnStart = false;
    bool m_stepMode = false;
    bool m_emulationStarted = false;
};

} // namespace ZonistationOne