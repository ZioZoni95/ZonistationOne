/*
 * ZonistationOne - Redux-Style Debugger Demo
 * Shows how our debugger could look more like PCSX-Redux
 */

#include "core/enhanced_debugger.h"
#include "core/emulator.h"
#include "core/debugger.h"
#include "core/logger.h"
#include <iostream>
#include <iomanip>
#include <sstream>

namespace ZonistationOne {

EnhancedDebugger::EnhancedDebugger(Emulator* emulator) 
    : m_emulator(emulator)
    , m_debugger(nullptr) {
    
    if (m_emulator) {
        m_debugger = m_emulator->getDebugger();
    }
}

EnhancedDebugger::~EnhancedDebugger() {
}

void EnhancedDebugger::showAssemblyWindow(uint32_t startPC, int lines) {
    if (!m_debugger) return;
    
    // Redux-style assembly window header
    std::cout << "\n╔══════════════════════════════════════════════════════════════════════════════╗\n";
    std::cout << "║                           ASSEMBLY VIEW (Redux Style)                        ║\n";  
    std::cout << "╠══════════════════════════════════════════════════════════════════════════════╣\n";
    
    auto disasm = getEnhancedDisassembly(startPC, lines);
    
    for (const auto& line : disasm) {
        printAssemblyLine(line);
    }
    
    std::cout << "╚══════════════════════════════════════════════════════════════════════════════╝\n\n";
}

void EnhancedDebugger::showMemoryEditor(uint32_t address, size_t length) {
    if (!m_debugger) return;
    
    // Redux-style memory editor header  
    std::cout << "\n╔══════════════════════════════════════════════════════════════════════════════╗\n";
    std::cout << "║                        MEMORY EDITOR (Redux Style)                           ║\n";
    std::cout << "╠══════════════════════════════════════════════════════════════════════════════╣\n";
    std::cout << "║ Address    00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F    ASCII        ║\n";
    std::cout << "╠══════════════════════════════════════════════════════════════════════════════╣\n";
    
    auto memView = getMemoryView(address, length);
    
    for (const auto& line : memView) {
        printMemoryLine(line);
    }
    
    std::cout << "╚══════════════════════════════════════════════════════════════════════════════╝\n\n";
}

void EnhancedDebugger::showRegistersPanel() {
    if (!m_debugger) return;
    
    auto cpu = m_emulator->getCPU();
    if (!cpu) return;
    
    // Redux-style registers panel
    std::cout << "\n╔══════════════════════════════╗  ╔══════════════════════════════╗\n";
    std::cout << "║        REGISTERS (R0-R15)     ║  ║       REGISTERS (R16-R31)    ║\n";
    std::cout << "╠══════════════════════════════╣  ╠══════════════════════════════╣\n";
    
    for (int i = 0; i < 16; i++) {
        std::cout << "║ R" << std::setfill('0') << std::setw(2) << i 
                  << ": " << COLOR_INSTRUCTION << "0x" << std::hex << std::setfill('0') << std::setw(8) 
                  << cpu->getRegister(i) << COLOR_RESET << " ║  ";
        
        std::cout << "║ R" << std::setfill('0') << std::setw(2) << (i+16) 
                  << ": " << COLOR_INSTRUCTION << "0x" << std::hex << std::setfill('0') << std::setw(8) 
                  << cpu->getRegister(i+16) << COLOR_RESET << " ║\n";
    }
    
    std::cout << "╠══════════════════════════════╩══╩══════════════════════════════╣\n";
    std::cout << "║ PC: " << COLOR_PC << "0x" << std::hex << std::setfill('0') << std::setw(8) 
              << cpu->getPC() << COLOR_RESET << "  Cycles: " << COLOR_ADDRESS << std::dec 
              << cpu->getCycleCount() << COLOR_RESET << "  Status: " 
              << (cpu->isHalted() ? COLOR_BREAKPOINT "HALTED" : COLOR_INSTRUCTION "RUNNING") 
              << COLOR_RESET << " ║\n";
    std::cout << "╚══════════════════════════════════════════════════════════════╝\n\n";
}

std::vector<EnhancedDebugger::DisassemblyLine> EnhancedDebugger::getEnhancedDisassembly(uint32_t startAddr, int count) {
    std::vector<DisassemblyLine> lines;
    
    if (!m_debugger) return lines;
    
    auto cpu = m_emulator->getCPU();
    uint32_t currentPC = cpu ? cpu->getPC() : 0;
    
    for (int i = 0; i < count; i++) {
        uint32_t addr = startAddr + (i * 4);
        DisassemblyLine line;
        
        line.address = addr;
        line.instruction = 0; // Would read from memory
        line.isCurrentPC = (addr == currentPC);
        
        // Check if there's a breakpoint here
        auto breakpoints = m_debugger->getAllBreakpoints();
        for (const auto* bp : breakpoints) {
            if (bp->address == addr && bp->type == Debugger::BreakpointType::EXECUTION) {
                line.hasBreakpoint = true;
                break;
            }
        }
        
        // Enhanced instruction decoding
        line.mnemonic = decodeInstruction(line.instruction, addr);
        line.operands = formatOperands(line.instruction);
        line.comment = getInstructionComment(line.instruction, addr);
        
        lines.push_back(line);
    }
    
    return lines;
}

std::vector<EnhancedDebugger::MemoryLine> EnhancedDebugger::getMemoryView(uint32_t startAddr, size_t length) {
    std::vector<MemoryLine> lines;
    
    auto memory = m_emulator->getMemory();
    if (!memory) return lines;
    
    for (size_t offset = 0; offset < length; offset += 16) {
        MemoryLine line;
        line.address = startAddr + offset;
        
        // Build hex and ASCII strings (like Redux)
        std::ostringstream hexStream, asciiStream;
        
        for (int i = 0; i < 16; i++) {
            if (offset + i < length) {
                uint8_t byte = memory->read8(line.address + i);
                line.bytes[i] = byte;
                
                hexStream << std::hex << std::setfill('0') << std::setw(2) << (int)byte << " ";
                asciiStream << (byte >= 32 && byte <= 126 ? (char)byte : '.');
            } else {
                hexStream << "   ";
                asciiStream << " ";
            }
        }
        
        line.hexString = hexStream.str();
        line.asciiString = asciiStream.str();
        
        lines.push_back(line);
    }
    
    return lines;
}

void EnhancedDebugger::printAssemblyLine(const DisassemblyLine& line) {
    std::cout << "║ ";
    
    // Breakpoint indicator (like Redux red circle)
    if (line.hasBreakpoint) {
        std::cout << COLOR_BREAKPOINT << "●" << COLOR_RESET << " ";
    } else {
        std::cout << "  ";
    }
    
    // PC indicator (like Redux yellow arrow)
    if (line.isCurrentPC) {
        std::cout << COLOR_PC << "► " << COLOR_RESET;
    } else {
        std::cout << "  ";
    }
    
    // Address (like Redux)
    std::cout << COLOR_ADDRESS << "0x" << std::hex << std::setfill('0') << std::setw(8) 
              << line.address << COLOR_RESET << ": ";
    
    // Instruction hex
    std::cout << std::hex << std::setfill('0') << std::setw(8) << line.instruction << " ";
    
    // Mnemonic and operands (like Redux)
    std::cout << COLOR_INSTRUCTION << std::setfill(' ') << std::setw(8) << line.mnemonic 
              << COLOR_RESET << " " << line.operands;
    
    // Comment (like Redux)
    if (!line.comment.empty()) {
        std::cout << COLOR_COMMENT << "  ; " << line.comment << COLOR_RESET;
    }
    
    std::cout << " ║\n";
}

void EnhancedDebugger::printMemoryLine(const MemoryLine& line) {
    std::cout << "║ " << COLOR_ADDRESS << "0x" << std::hex << std::setfill('0') << std::setw(8) 
              << line.address << COLOR_RESET << " ";
    
    // Hex bytes (like Redux)
    std::cout << line.hexString;
    
    // ASCII (like Redux)  
    std::cout << " " << COLOR_COMMENT << line.asciiString << COLOR_RESET << " ║\n";
}

std::string EnhancedDebugger::decodeInstruction(uint32_t instruction, uint32_t pc) {
    // Enhanced instruction decoding (better than our current basic version)
    uint32_t opcode = (instruction >> 26) & 0x3F;
    
    switch (opcode) {
        case 0x00: {
            uint32_t funct = instruction & 0x3F;
            switch (funct) {
                case 0x00: return "SLL";
                case 0x02: return "SRL"; 
                case 0x21: return "ADDU";
                case 0x25: return "OR";
                default: return "SPECIAL";
            }
        }
        case 0x02: return "J";
        case 0x03: return "JAL";
        case 0x09: return "ADDIU";
        case 0x0D: return "ORI";
        case 0x0F: return "LUI";
        case 0x23: return "LW";
        case 0x2B: return "SW";
        default: return "UNKNOWN";
    }
}

std::string EnhancedDebugger::formatOperands(uint32_t instruction) {
    // Format operands like Redux: "R8, R0, 0x1234"
    uint32_t opcode = (instruction >> 26) & 0x3F;
    uint32_t rs = (instruction >> 21) & 0x1F;
    uint32_t rt = (instruction >> 16) & 0x1F; 
    uint32_t rd = (instruction >> 11) & 0x1F;
    int16_t imm = instruction & 0xFFFF;
    
    std::ostringstream oss;
    
    switch (opcode) {
        case 0x0F: // LUI
            oss << "R" << rt << ", 0x" << std::hex << (imm & 0xFFFF);
            break;
        case 0x0D: // ORI
        case 0x09: // ADDIU
            oss << "R" << rt << ", R" << rs << ", 0x" << std::hex << (imm & 0xFFFF);
            break;
        case 0x23: // LW  
        case 0x2B: // SW
            oss << "R" << rt << ", " << std::dec << imm << "(R" << rs << ")";
            break;
        case 0x00: // SPECIAL
            oss << "R" << rd << ", R" << rs << ", R" << rt;
            break;
        default:
            oss << "...";
    }
    
    return oss.str();
}

std::string EnhancedDebugger::getInstructionComment(uint32_t instruction, uint32_t pc) {
    // Add helpful comments like Redux
    uint32_t opcode = (instruction >> 26) & 0x3F;
    
    switch (opcode) {
        case 0x0F: return "Load upper immediate";
        case 0x0D: return "Bitwise OR with immediate";  
        case 0x09: return "Add immediate unsigned";
        case 0x23: return "Load word from memory";
        case 0x2B: return "Store word to memory";
        case 0x02: return "Jump to address";
        case 0x03: return "Jump and link";
        default: return "";
    }
}

} // namespace ZonistationOne