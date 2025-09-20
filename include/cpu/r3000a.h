/*
 * ZonistationOne - PlayStation 1 Emulator
 * MIPS R3000A CPU Core Header
 */

#pragma once

#include <cstdint>
#include <memory>
#include "cpu/instruction.h"

namespace ZonistationOne {

class Memory;    class CPU {
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
        
        // Instruction handlers (following PCSX-Redux pattern)
        typedef void (CPU::*InstructionHandler)(const InstructionInfo& info);
        
        // Primary instruction handlers
        void handleSPECIAL(const InstructionInfo& info);
        void handleREGIMM(const InstructionInfo& info);
        void handleJ(const InstructionInfo& info);
        void handleJAL(const InstructionInfo& info);
        void handleBEQ(const InstructionInfo& info);
        void handleBNE(const InstructionInfo& info);
        void handleBLEZ(const InstructionInfo& info);
        void handleBGTZ(const InstructionInfo& info);
        void handleADDI(const InstructionInfo& info);
        void handleADDIU(const InstructionInfo& info);
        void handleSLTI(const InstructionInfo& info);
        void handleSLTIU(const InstructionInfo& info);
        void handleANDI(const InstructionInfo& info);
        void handleORI(const InstructionInfo& info);
        void handleXORI(const InstructionInfo& info);
        void handleLUI(const InstructionInfo& info);
        void handleCOP0(const InstructionInfo& info);  // Coprocessor 0 (System Control)
        void handleLB(const InstructionInfo& info);
        void handleLH(const InstructionInfo& info);
        void handleLW(const InstructionInfo& info);
        void handleLBU(const InstructionInfo& info);
        void handleLHU(const InstructionInfo& info);
        void handleSB(const InstructionInfo& info);
        void handleSH(const InstructionInfo& info);
        void handleSW(const InstructionInfo& info);
        
        // SPECIAL function handlers
        void handleSLL(const InstructionInfo& info);
        void handleSRL(const InstructionInfo& info);
        void handleSRA(const InstructionInfo& info);
        void handleJR(const InstructionInfo& info);
        void handleJALR(const InstructionInfo& info);
        void handleSYSCALL(const InstructionInfo& info);
        void handleBREAK(const InstructionInfo& info);
        void handleMFHI(const InstructionInfo& info);
        void handleMTHI(const InstructionInfo& info);
        void handleMFLO(const InstructionInfo& info);
        void handleMTLO(const InstructionInfo& info);
        void handleMULT(const InstructionInfo& info);
        void handleMULTU(const InstructionInfo& info);
        void handleDIV(const InstructionInfo& info);
        void handleDIVU(const InstructionInfo& info);
        void handleADD(const InstructionInfo& info);
        void handleADDU(const InstructionInfo& info);
        void handleSUB(const InstructionInfo& info);
        void handleSUBU(const InstructionInfo& info);
        void handleAND(const InstructionInfo& info);
        void handleOR(const InstructionInfo& info);
        void handleXOR(const InstructionInfo& info);
        void handleNOR(const InstructionInfo& info);
        void handleSLT(const InstructionInfo& info);
        void handleSLTU(const InstructionInfo& info);
        
        // Helper methods
        void handleUnknownInstruction(uint32_t instruction, uint32_t opcode);
        
        // Dispatch tables (following PCSX-Redux pattern)
        static const InstructionHandler s_primaryHandlers[64];
        static const InstructionHandler s_specialHandlers[64];
        
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