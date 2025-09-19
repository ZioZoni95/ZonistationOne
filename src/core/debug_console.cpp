/*
 * ZonistationOne - PlayStation 1 Emulator
 * Debug Console Interface Implementation
 */

#include "core/debug_console.h"
#include "core/emulator.h"
#include "core/debugger.h"
#include "core/logger.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cstring>

namespace ZonistationOne {

DebugConsole::DebugConsole(Emulator* emulator) 
    : m_emulator(emulator)
    , m_debugger(nullptr)
    , m_running(false)
    , m_quit(false) {
    
    if (m_emulator) {
        m_debugger = m_emulator->getDebugger();
    }
}

DebugConsole::~DebugConsole() {
    stop();
}

void DebugConsole::start() {
    if (m_running) return;
    
    m_running = true;
    m_quit = false;
    
    std::cout << "\n=== ZonistationOne Debug Console ===\n";
    std::cout << "Type 'help' for available commands\n";
    std::cout << "Type 'run' to start emulation\n\n";
    
    // Set up initial debugging state
    if (m_debugger) {
        if (m_breakOnStart) {
            uint32_t bp = m_debugger->addBreakpoint(0xBFC00000, Debugger::BreakpointType::EXECUTION, "BIOS Entry");
            std::cout << "Set breakpoint #" << bp << " at BIOS entry (0xBFC00000)\n";
        }
        
        if (m_stepMode) {
            m_debugger->pause();
            std::cout << "Started in step mode - emulation paused\n";
        }
    }
    
    // Start console input thread
    m_consoleThread = std::make_unique<std::thread>(&DebugConsole::consoleThread, this);
}

void DebugConsole::stop() {
    if (!m_running) return;
    
    m_running = false;
    m_quit = true;
    
    if (m_consoleThread && m_consoleThread->joinable()) {
        m_consoleThread->join();
    }
}

void DebugConsole::consoleThread() {
    std::string line;
    
    while (m_running && !m_quit) {
        printPrompt();
        
        if (!std::getline(std::cin, line)) {
            break; // EOF
        }
        
        // Skip empty lines
        if (line.empty()) continue;
        
        if (!executeCommand(line)) {
            break; // Quit command
        }
    }
}

void DebugConsole::printPrompt() {
    if (m_debugger && m_debugger->isPaused()) {
        std::cout << "(paused) ";
    }
    std::cout << "debug> " << std::flush;
}

bool DebugConsole::executeCommand(const std::string& command) {
    auto tokens = tokenize(command);
    if (tokens.empty()) return true;
    
    std::string cmd = tokens[0];
    std::transform(cmd.begin(), cmd.end(), cmd.begin(), ::tolower);
    
    // Command dispatch
    if (cmd == "help" || cmd == "h") {
        return handleHelp(tokens);
    } else if (cmd == "run" || cmd == "r") {
        return handleRun(tokens);
    } else if (cmd == "pause" || cmd == "p") {
        return handlePause(tokens);
    } else if (cmd == "step" || cmd == "s") {
        return handleStep(tokens);
    } else if (cmd == "continue" || cmd == "c") {
        return handleContinue(tokens);
    } else if (cmd == "bp" || cmd == "break") {
        return handleBreakpoint(tokens);
    } else if (cmd == "info" || cmd == "i") {
        return handleInfo(tokens);
    } else if (cmd == "disasm" || cmd == "d") {
        return handleDisasm(tokens);
    } else if (cmd == "list" || cmd == "l") {
        return handleList(tokens);
    } else if (cmd == "quit" || cmd == "q" || cmd == "exit") {
        return handleQuit(tokens);
    } else {
        std::cout << "Unknown command: " << cmd << "\n";
        std::cout << "Type 'help' for available commands\n";
    }
    
    return true;
}

bool DebugConsole::handleHelp(const std::vector<std::string>& args) {
    (void)args; // Unused parameter
    
    std::cout << "\nAvailable Debug Commands:\n";
    std::cout << "  help, h              - Show this help\n";
    std::cout << "  run, r               - Start/resume emulation\n";
    std::cout << "  pause, p             - Pause emulation\n";
    std::cout << "  step, s              - Execute one instruction (when paused)\n";
    std::cout << "  continue, c          - Resume from breakpoint\n";
    std::cout << "  bp <addr>            - Set execution breakpoint at address (hex)\n";
    std::cout << "  list, l              - List all breakpoints\n";
    std::cout << "  info cpu             - Show CPU registers and status\n";
    std::cout << "  info mem <addr>      - Show memory at address (64 bytes)\n";
    std::cout << "  disasm <addr> [cnt]  - Disassemble instructions (default 10)\n";
    std::cout << "  quit, q, exit        - Exit debugger and emulator\n";
    std::cout << "\nAddress format: 0x1234ABCD or 1234ABCD (hex)\n";
    std::cout << "Examples:\n";
    std::cout << "  bp 0xBFC00000        - Break at BIOS entry\n";
    std::cout << "  info mem 0x1F801010  - Show I/O register area\n";
    std::cout << "  disasm 0xBFC00000 5  - Disassemble 5 instructions from BIOS\n\n";
    
    return true;
}

bool DebugConsole::handleRun(const std::vector<std::string>& args) {
    (void)args; // Unused parameter
    
    if (!m_emulator) {
        std::cout << "No emulator instance available\n";
        return true;
    }
    
    if (!m_emulationStarted) {
        std::cout << "Starting emulation...\n";
        m_emulationStarted = true;
        
        // Start emulation in a separate thread so we can continue accepting commands
        std::thread emulationThread([this]() {
            m_emulator->run();
        });
        emulationThread.detach();
        
    } else {
        std::cout << "Resuming emulation...\n";
        m_emulator->resume();
    }
    
    return true;
}

bool DebugConsole::handlePause(const std::vector<std::string>& args) {
    (void)args; // Unused parameter
    
    if (!m_emulator) {
        std::cout << "No emulator instance available\n";
        return true;
    }
    
    std::cout << "Pausing emulation...\n";
    m_emulator->pause();
    
    // Show current state when paused
    if (m_debugger) {
        printCPUState();
    }
    
    return true;
}

bool DebugConsole::handleStep(const std::vector<std::string>& args) {
    (void)args; // Unused parameter
    
    if (!m_debugger) {
        std::cout << "No debugger available\n";
        return true;
    }
    
    if (!m_debugger->isPaused()) {
        std::cout << "Emulation is not paused. Use 'pause' first.\n";
        return true;
    }
    
    std::cout << "Stepping one instruction...\n";
    m_debugger->step();
    
    // Show state after step
    printCPUState();
    
    return true;
}

bool DebugConsole::handleContinue(const std::vector<std::string>& args) {
    return handleRun(args); // Same as run
}

bool DebugConsole::handleBreakpoint(const std::vector<std::string>& args) {
    if (!m_debugger) {
        std::cout << "No debugger available\n";
        return true;
    }
    
    if (args.size() < 2) {
        std::cout << "Usage: bp <address>\n";
        std::cout << "Example: bp 0xBFC00000\n";
        return true;
    }
    
    uint32_t address = parseAddress(args[1]);
    if (address == 0 && args[1] != "0" && args[1] != "0x0") {
        std::cout << "Invalid address: " << args[1] << "\n";
        return true;
    }
    
    uint32_t bp = m_debugger->addBreakpoint(address, Debugger::BreakpointType::EXECUTION, "User");
    std::cout << "Set breakpoint #" << bp << " at 0x" << std::hex << std::setfill('0') << std::setw(8) << address << std::dec << "\n";
    
    return true;
}

bool DebugConsole::handleInfo(const std::vector<std::string>& args) {
    if (args.size() < 2) {
        std::cout << "Usage: info <cpu|mem> [address]\n";
        return true;
    }
    
    std::string subcmd = args[1];
    std::transform(subcmd.begin(), subcmd.end(), subcmd.begin(), ::tolower);
    
    if (subcmd == "cpu") {
        printCPUState();
    } else if (subcmd == "mem") {
        if (args.size() < 3) {
            std::cout << "Usage: info mem <address>\n";
            return true;
        }
        
        uint32_t address = parseAddress(args[2]);
        printMemory(address);
    } else {
        std::cout << "Unknown info command: " << subcmd << "\n";
        std::cout << "Available: cpu, mem\n";
    }
    
    return true;
}

bool DebugConsole::handleDisasm(const std::vector<std::string>& args) {
    if (args.size() < 2) {
        std::cout << "Usage: disasm <address> [count]\n";
        return true;
    }
    
    uint32_t address = parseAddress(args[1]);
    int count = 10; // default
    
    if (args.size() >= 3) {
        try {
            count = std::stoi(args[2]);
        } catch (const std::exception&) {
            std::cout << "Invalid count: " << args[2] << "\n";
            return true;
        }
    }
    
    printDisassembly(address, count);
    return true;
}

bool DebugConsole::handleList(const std::vector<std::string>& args) {
    (void)args; // Unused parameter
    
    if (!m_debugger) {
        std::cout << "No debugger available\n";
        return true;
    }
    
    auto breakpoints = m_debugger->getAllBreakpoints();
    if (breakpoints.empty()) {
        std::cout << "No breakpoints set\n";
        return true;
    }
    
    std::cout << "Breakpoints:\n";
    for (const auto* bp : breakpoints) {
        const char* typeStr = "";
        switch (bp->type) {
            case Debugger::BreakpointType::EXECUTION: typeStr = "EXEC"; break;
            case Debugger::BreakpointType::READ: typeStr = "READ"; break;
            case Debugger::BreakpointType::WRITE: typeStr = "WRITE"; break;
            case Debugger::BreakpointType::ACCESS: typeStr = "ACCESS"; break;
        }
        
        std::cout << "  " << typeStr << " at 0x" << std::hex << std::setfill('0') << std::setw(8) << bp->address 
                  << " (" << bp->label << ") - " << (bp->enabled ? "enabled" : "disabled")
                  << " - hit " << std::dec << bp->hitCount << " times\n";
    }
    
    return true;
}

bool DebugConsole::handleQuit(const std::vector<std::string>& args) {
    (void)args; // Unused parameter
    
    std::cout << "Exiting debugger...\n";
    
    if (m_emulator) {
        m_emulator->stop();
    }
    
    return false; // Signal to exit
}

std::vector<std::string> DebugConsole::tokenize(const std::string& str) {
    std::vector<std::string> tokens;
    std::istringstream iss(str);
    std::string token;
    
    while (iss >> token) {
        tokens.push_back(token);
    }
    
    return tokens;
}

uint32_t DebugConsole::parseAddress(const std::string& addr) {
    try {
        if (addr.substr(0, 2) == "0x" || addr.substr(0, 2) == "0X") {
            return std::stoul(addr, nullptr, 16);
        } else {
            return std::stoul(addr, nullptr, 16); // Assume hex even without 0x
        }
    } catch (const std::exception&) {
        return 0;
    }
}

void DebugConsole::printCPUState() {
    if (!m_debugger) {
        std::cout << "No debugger available\n";
        return;
    }
    
    std::cout << "\n=== CPU State ===\n";
    m_debugger->dumpCPUState();
    std::cout << "\n";
}

void DebugConsole::printMemory(uint32_t address, uint32_t length) {
    if (!m_debugger) {
        std::cout << "No debugger available\n";
        return;
    }
    
    std::cout << "\n=== Memory at 0x" << std::hex << std::setfill('0') << std::setw(8) << address << " ===\n" << std::dec;
    m_debugger->dumpMemoryRegion(address, length);
    std::cout << "\n";
}

void DebugConsole::printDisassembly(uint32_t address, int count) {
    if (!m_debugger) {
        std::cout << "No debugger available\n";
        return;
    }
    
    std::cout << "\n=== Disassembly at 0x" << std::hex << std::setfill('0') << std::setw(8) << address << " ===\n" << std::dec;
    auto disasm = m_debugger->disassembleRegion(address, count);
    for (size_t i = 0; i < disasm.size(); ++i) {
        uint32_t addr = address + (i * 4);
        std::cout << "0x" << std::hex << std::setfill('0') << std::setw(8) << addr << ": " << disasm[i] << std::dec << "\n";
    }
    std::cout << "\n";
}

} // namespace ZonistationOne