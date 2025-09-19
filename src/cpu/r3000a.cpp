/*
 * ZonistationOne - PlayStation 1 Emulator
 * MIPS R3000A CPU Core Implementation (Stub)
 */

#include "cpu/r3000a.h"
#include "memory/memory_map.h"
#include "core/logger.h"
#include <iostream>
#include <cstring>

namespace ZonistationOne {

CPU::CPU(Memory* memory) 
    : m_memory(memory)
    , m_halted(false)
    , m_cycleCount(0)
    , m_delaySlot(false) {
    
    // Initialize registers to zero
    std::memset(m_registers, 0, sizeof(m_registers));
    std::memset(m_cop0_registers, 0, sizeof(m_cop0_registers));
    
    m_pc = RESET_VECTOR;
    m_nextPC = m_pc + 4;
    m_hi = m_lo = 0;
}

CPU::~CPU() {
    shutdown();
}

bool CPU::initialize() {
    ZONI_LOG_INFO(CPU, "Initializing MIPS R3000A core");
    
    // Reset to initial state
    reset();
    
    ZONI_LOG_INFO(CPU, "Initialization complete");
    return true;
}

void CPU::shutdown() {
    // Nothing to cleanup for now
}

void CPU::reset() {
    ZONI_LOG_INFO(CPU, "Resetting to initial state");
    
    // Clear all registers
    std::memset(m_registers, 0, sizeof(m_registers));
    std::memset(m_cop0_registers, 0, sizeof(m_cop0_registers));
    
    // Reset PC to BIOS entry point
    m_pc = RESET_VECTOR;
    m_nextPC = m_pc + 4;
    m_hi = m_lo = 0;
    
    m_halted = false;
    m_cycleCount = 0;
    m_delaySlot = false;
}

uint32_t CPU::step() {
    if (m_halted) {
        // CPU is halted, don't execute any instructions
        ZONI_LOG_TRACE(CPU, "CPU step called while halted");
        return 0; // Don't consume cycles when halted
    }
    
    // Fetch instruction
    uint32_t instruction = fetchInstruction();
    
    // Update PC (handle delay slot)
    uint32_t currentPC = m_pc;
    m_pc = m_nextPC;
    m_nextPC = m_pc + 4;
    
    // Execute instruction
    executeInstruction(instruction);
    
    m_cycleCount++;
    return 1; // Most instructions take 1 cycle
}

void CPU::runFor(uint32_t cycles) {
    for (uint32_t i = 0; i < cycles && !m_halted; ++i) {
        step();
    }
}

uint32_t CPU::fetchInstruction() {
    if (!m_memory) {
        ZONI_LOG_ERROR(CPU, "No memory interface available");
        m_halted = true;
        return 0;
    }
    
    return m_memory->read32(m_pc);
}

void CPU::executeInstruction(uint32_t instruction) {
    // TODO: Implement full MIPS instruction decoding and execution
    // For now, just implement a basic NOP and halt on unknown instructions
    
    if (instruction == 0) {
        // NOP instruction
        return;
    }
    
    // Extract opcode (bits 31-26)
    uint32_t opcode = (instruction >> 26) & 0x3F;
    
    // Very basic stub - just log unknown instructions for now
    static int unknownCount = 0;
    if (unknownCount < 10) { // Don't spam too much
        ZONI_LOG_CPU_UNKNOWN_INSTRUCTION("Unknown instruction 0x%08x (opcode 0x%02x) at PC 0x%08x", 
                                          instruction, opcode, m_pc);
        unknownCount++;
    }
    
    // For now, halt on unknown instructions to prevent infinite loops
    if (unknownCount >= 10) {
        ZONI_LOG_WARN(CPU, "Too many unknown instructions, halting");
        m_halted = true;
    }
}

uint32_t CPU::getRegister(int reg) const {
    if (reg < 0 || reg >= 32) return 0;
    return m_registers[reg];
}

void CPU::setRegister(int reg, uint32_t value) {
    if (reg <= 0 || reg >= 32) return; // R0 is always zero
    m_registers[reg] = value;
}

void CPU::dumpState() const {
    ZONI_LOG_INFO(CPU, "=== CPU State ===");
    ZONI_LOG_INFO(CPU, "  PC: 0x%08x", m_pc);
    ZONI_LOG_INFO(CPU, "  Next PC: 0x%08x", m_nextPC);
    ZONI_LOG_INFO(CPU, "  Cycles: %llu", m_cycleCount);
    ZONI_LOG_INFO(CPU, "  Halted: %s", m_halted ? "Yes" : "No");
    
    // Print some key registers
    ZONI_LOG_INFO(CPU, "  Registers:");
    for (int i = 0; i < 32; i += 4) {
        ZONI_LOG_INFO(CPU, "    R%d-R%d: 0x%08x 0x%08x 0x%08x 0x%08x", 
                     i, (i+3),
                     m_registers[i], m_registers[i+1], 
                     m_registers[i+2], m_registers[i+3]);
    }
}

} // namespace ZonistationOne