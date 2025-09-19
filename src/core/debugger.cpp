/*
 * ZonistationOne - PlayStation 1 Emulator
 * Debug System Implementation
 */

#include "core/debugger.h"
#include "core/emulator.h"
#include "core/logger.h"
#include "cpu/r3000a.h"
#include "memory/memory_map.h"
#include <iostream>
#include <iomanip>
#include <sstream>

namespace ZonistationOne {

Debugger::Debugger(Emulator* emulator) 
    : m_emulator(emulator)
    , m_cpu(nullptr)
    , m_memory(nullptr) {
    
    ZONI_LOG_INFO(DEBUGGER, "Debugger initialized");
}

Debugger::~Debugger() {
    clearAllBreakpoints();
    ZONI_LOG_INFO(DEBUGGER, "Debugger shutdown");
}

void Debugger::connectComponents(CPU* cpu, Memory* memory) {
    m_cpu = cpu;
    m_memory = memory;
    ZONI_LOG_INFO(DEBUGGER, "Connected CPU and Memory to debugger");
}

uint32_t Debugger::addBreakpoint(uint32_t address, BreakpointType type, const std::string& label) {
    uint32_t id = m_nextBreakpointId++;
    address = normalizeAddress(address);
    
    m_breakpoints.emplace(id, Breakpoint(address, type, label));
    
    const char* typeStr = "";
    switch (type) {
        case BreakpointType::EXECUTION: typeStr = "EXEC"; break;
        case BreakpointType::READ: typeStr = "READ"; break;
        case BreakpointType::WRITE: typeStr = "WRITE"; break;
        case BreakpointType::ACCESS: typeStr = "ACCESS"; break;
    }
    
    ZONI_LOG_INFO(DEBUGGER, "Added breakpoint #%u: %s at 0x%08X (%s)", 
                  id, typeStr, address, label.empty() ? "unnamed" : label.c_str());
    
    return id;
}

bool Debugger::removeBreakpoint(uint32_t id) {
    auto it = m_breakpoints.find(id);
    if (it != m_breakpoints.end()) {
        ZONI_LOG_INFO(DEBUGGER, "Removed breakpoint #%u at 0x%08X", id, it->second.address);
        m_breakpoints.erase(it);
        return true;
    }
    return false;
}

bool Debugger::enableBreakpoint(uint32_t id, bool enabled) {
    auto it = m_breakpoints.find(id);
    if (it != m_breakpoints.end()) {
        it->second.enabled = enabled;
        ZONI_LOG_INFO(DEBUGGER, "Breakpoint #%u %s", id, enabled ? "enabled" : "disabled");
        return true;
    }
    return false;
}

const Debugger::Breakpoint* Debugger::getBreakpoint(uint32_t id) const {
    auto it = m_breakpoints.find(id);
    return (it != m_breakpoints.end()) ? &it->second : nullptr;
}

std::vector<const Debugger::Breakpoint*> Debugger::getAllBreakpoints() const {
    std::vector<const Breakpoint*> result;
    result.reserve(m_breakpoints.size());
    
    for (const auto& [id, bp] : m_breakpoints) {
        result.push_back(&bp);
    }
    
    return result;
}

void Debugger::clearAllBreakpoints() {
    size_t count = m_breakpoints.size();
    m_breakpoints.clear();
    if (count > 0) {
        ZONI_LOG_INFO(DEBUGGER, "Cleared %zu breakpoints", count);
    }
}

void Debugger::pause() {
    if (!m_paused) {
        m_paused = true;
        m_stepMode = StepMode::NONE;
        ZONI_LOG_INFO(DEBUGGER, "Execution paused");
    }
}

void Debugger::resume() {
    if (m_paused) {
        m_paused = false;
        m_stepMode = StepMode::NONE;
        ZONI_LOG_INFO(DEBUGGER, "Execution resumed");
    }
}

void Debugger::step() {
    m_stepMode = StepMode::STEP_INTO;
    m_paused = false;
    ZONI_LOG_DEBUG(DEBUGGER, "Step into");
}

void Debugger::stepOver() {
    if (m_cpu) {
        m_stepMode = StepMode::STEP_OVER;
        m_stepOverAddress = m_cpu->getPC() + 4; // Next instruction
        m_paused = false;
        ZONI_LOG_DEBUG(DEBUGGER, "Step over to 0x%08X", m_stepOverAddress);
    }
}

void Debugger::stepOut() {
    m_stepMode = StepMode::STEP_OUT;
    m_stepOutDepth = 0; // TODO: Implement call stack depth tracking
    m_paused = false;
    ZONI_LOG_DEBUG(DEBUGGER, "Step out");
}

void Debugger::runToCursor(uint32_t address) {
    m_stepMode = StepMode::RUN_TO_CURSOR;
    m_runToAddress = normalizeAddress(address);
    m_paused = false;
    ZONI_LOG_DEBUG(DEBUGGER, "Run to cursor: 0x%08X", m_runToAddress);
}

bool Debugger::checkExecutionBreakpoint(uint32_t address) {
    if (!m_emulator || !m_cpu) {
        m_cpu = m_emulator->getCPU();
        m_memory = m_emulator->getMemory();
    }
    
    address = normalizeAddress(address);
    
    // Check step modes first
    switch (m_stepMode) {
        case StepMode::STEP_INTO:
            m_stepMode = StepMode::NONE;
            m_paused = true;
            ZONI_LOG_DEBUG(DEBUGGER, "Step break at 0x%08X", address);
            return true;
            
        case StepMode::STEP_OVER:
            if (address == m_stepOverAddress) {
                m_stepMode = StepMode::NONE;
                m_paused = true;
                ZONI_LOG_DEBUG(DEBUGGER, "Step over break at 0x%08X", address);
                return true;
            }
            break;
            
        case StepMode::RUN_TO_CURSOR:
            if (address == m_runToAddress) {
                m_stepMode = StepMode::NONE;
                m_paused = true;
                ZONI_LOG_DEBUG(DEBUGGER, "Run to cursor break at 0x%08X", address);
                return true;
            }
            break;
            
        default:
            break;
    }
    
    // Check execution breakpoints
    for (auto& [id, bp] : m_breakpoints) {
        if (bp.enabled && bp.type == BreakpointType::EXECUTION && bp.address == address) {
            bp.hitCount++;
            onBreakpointHit(bp);
            return true;
        }
    }
    
    // Instruction tracing
    if (m_instructionTrace) {
        std::string disasm = disassembleInstruction(address);
        ZONI_LOG_TRACE(CPU, "PC: 0x%08X  %s", address, disasm.c_str());
    }
    
    return false;
}

bool Debugger::checkMemoryBreakpoint(uint32_t address, BreakpointType type) {
    address = normalizeAddress(address);
    
    for (auto& [id, bp] : m_breakpoints) {
        if (bp.enabled && bp.address == address && 
            (bp.type == type || bp.type == BreakpointType::ACCESS)) {
            bp.hitCount++;
            onBreakpointHit(bp);
            return true;
        }
    }
    
    return false;
}

void Debugger::dumpCPUState() const {
    if (!m_cpu) {
        ZONI_LOG_ERROR(DEBUGGER, "CPU not available for state dump");
        return;
    }
    
    ZONI_LOG_INFO(DEBUGGER, "=== CPU State ===");
    ZONI_LOG_INFO(DEBUGGER, "PC: 0x%08x", m_cpu->getPC());
    ZONI_LOG_INFO(DEBUGGER, "Cycle Count: %llu", m_cpu->getCycleCount());
    ZONI_LOG_INFO(DEBUGGER, "Halted: %s", m_cpu->isHalted() ? "Yes" : "No");
    
    // Register dump
    ZONI_LOG_INFO(DEBUGGER, "General Purpose Registers:");
    for (int i = 0; i < 32; i += 4) {
        ZONI_LOG_INFO(DEBUGGER, "R%02d-R%02d: 0x%08x 0x%08x 0x%08x 0x%08x", 
                     i, i+3,
                     m_cpu->getRegister(i), m_cpu->getRegister(i+1),
                     m_cpu->getRegister(i+2), m_cpu->getRegister(i+3));
    }
}

void Debugger::dumpMemoryRegion(uint32_t startAddress, uint32_t length) const {
    if (!m_memory) {
        ZONI_LOG_ERROR(DEBUGGER, "Memory not available for dump");
        return;
    }
    
    ZONI_LOG_INFO(DEBUGGER, "=== Memory Dump: 0x%08x - 0x%08x ===", 
                  startAddress, startAddress + length - 1);
    
    for (uint32_t addr = startAddress; addr < startAddress + length; addr += 16) {
        // Build hex dump string
        std::string hexDump;
        std::string asciiDump;
        
        for (int i = 0; i < 16 && (addr + i) < startAddress + length; ++i) {
            uint8_t byte = m_memory->read8(addr + i);
            char hexBuf[4];
            snprintf(hexBuf, sizeof(hexBuf), "%02x ", byte);
            hexDump += hexBuf;
            
            char c = (byte >= 32 && byte <= 126) ? (char)byte : '.';
            asciiDump += c;
        }
        
        ZONI_LOG_INFO(DEBUGGER, "0x%08x: %-48s %s", addr, hexDump.c_str(), asciiDump.c_str());
    }
}

std::string Debugger::disassembleInstruction(uint32_t address) const {
    if (!m_memory) {
        return "N/A";
    }
    
    uint32_t instruction = m_memory->read32(address);
    
    // Basic disassembly - just show the instruction value for now
    // TODO: Implement full MIPS disassembler
    std::ostringstream oss;
    oss << "0x" << std::hex << std::setfill('0') << std::setw(8) << instruction;
    
    // Add basic opcode identification
    uint32_t opcode = (instruction >> 26) & 0x3F;
    switch (opcode) {
        case 0x00: oss << " (SPECIAL)"; break;
        case 0x0F: oss << " (LUI)"; break;
        case 0x09: oss << " (ADDIU)"; break;
        case 0x0D: oss << " (ORI)"; break;
        case 0x02: oss << " (J)"; break;
        case 0x03: oss << " (JAL)"; break;
        case 0x2B: oss << " (SW)"; break;
        case 0x23: oss << " (LW)"; break;
        default: oss << " (OP:" << std::hex << opcode << ")"; break;
    }
    
    return oss.str();
}

std::vector<std::string> Debugger::disassembleRegion(uint32_t startAddress, int count) const {
    std::vector<std::string> result;
    result.reserve(count);
    
    for (int i = 0; i < count; ++i) {
        uint32_t addr = startAddress + (i * 4);
        std::ostringstream oss;
        oss << "0x" << std::hex << std::setfill('0') << std::setw(8) << addr 
            << ": " << disassembleInstruction(addr);
        result.push_back(oss.str());
    }
    
    return result;
}

void Debugger::addSymbol(uint32_t address, const std::string& name) {
    m_symbols[normalizeAddress(address)] = name;
    ZONI_LOG_DEBUG(DEBUGGER, "Added symbol '%s' at 0x%08X", name.c_str(), address);
}

void Debugger::removeSymbol(uint32_t address) {
    auto it = m_symbols.find(normalizeAddress(address));
    if (it != m_symbols.end()) {
        ZONI_LOG_DEBUG(DEBUGGER, "Removed symbol '%s' from 0x%08X", it->second.c_str(), address);
        m_symbols.erase(it);
    }
}

std::string Debugger::getSymbol(uint32_t address) const {
    auto it = m_symbols.find(normalizeAddress(address));
    return (it != m_symbols.end()) ? it->second : "";
}

void Debugger::onBreakpointHit(const Breakpoint& bp) {
    m_paused = true;
    m_stepMode = StepMode::NONE;
    
    const char* typeStr = "";
    switch (bp.type) {
        case BreakpointType::EXECUTION: typeStr = "EXEC"; break;
        case BreakpointType::READ: typeStr = "READ"; break;
        case BreakpointType::WRITE: typeStr = "WRITE"; break;
        case BreakpointType::ACCESS: typeStr = "ACCESS"; break;
    }
    
    ZONI_LOG_INFO(DEBUGGER, "Breakpoint hit: %s at 0x%08X (%s) - Hit count: %u", 
                  typeStr, bp.address, 
                  bp.label.empty() ? "unnamed" : bp.label.c_str(),
                  bp.hitCount);
    
    // Dump CPU state on breakpoint hit
    if (m_traceEnabled) {
        dumpCPUState();
    }
}

uint32_t Debugger::normalizeAddress(uint32_t address) const {
    // Remove cache control bits (same as memory system)
    return address & 0x1FFFFFFF;
}

} // namespace ZonistationOne