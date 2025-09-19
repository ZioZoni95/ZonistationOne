/*
 * ZonistationOne - PlayStation 1 Emulator
 * MIPS R3000A CPU Core Header
 */

#pragma once

#include <cstdint>
#include <memory>

namespace ZonistationOne {
    
    class Memory;
    
    class CPU {
    public:
        CPU(Memory* memory);
        ~CPU();
        
        bool initialize();
        void shutdown();
        void reset();
        
        // Execution
        uint32_t step();  // Execute one instruction, return cycles used
        void runFor(uint32_t cycles);
        
        // State
        bool isHalted() const { return m_halted; }
        uint64_t getCycleCount() const { return m_cycleCount; }
        
        // Registers
        uint32_t getPC() const { return m_pc; }
        uint32_t getRegister(int reg) const;
        void setRegister(int reg, uint32_t value);
        
        // Debug
        void dumpState() const;
        
    private:
        void executeInstruction(uint32_t instruction);
        uint32_t fetchInstruction();
        
        // Register file (32 general purpose + special registers)
        uint32_t m_registers[32];
        uint32_t m_pc;           // Program counter
        uint32_t m_nextPC;       // Next PC (for branch delay slot)
        uint32_t m_hi, m_lo;     // Multiplication/division registers
        
        // Coprocessor 0 (System control)
        uint32_t m_cop0_registers[32];
        
        // State
        Memory* m_memory;
        bool m_halted;
        uint64_t m_cycleCount;
        bool m_delaySlot;
        
        // Constants
        static constexpr uint32_t RESET_VECTOR = 0xBFC00000;
    };
}