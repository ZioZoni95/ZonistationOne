/*
 * ZonistationOne - PlayStation 1 Emulator
 * Memory Controller Header
 * Based on PCSX-Redux architecture
 */

#pragma once

#include <cstdint>

namespace ZonistationOne {
    
    class MemoryController {
    public:
        MemoryController();
        ~MemoryController();
        
        void reset();
        
        // Hardware register access
        uint32_t readRegister(uint32_t address);
        void writeRegister(uint32_t address, uint32_t value);
        
        // Register addresses (following Redux patterns)
        static constexpr uint32_t MEM_CONTROL_ADDR = 0x1f801010;  // Memory Control Register
        static constexpr uint32_t RAM_SIZE_ADDR    = 0x1f801060;  // RAM Size Register
        
        // Additional memory controller registers that might be accessed
        static constexpr uint32_t MEM_CONTROL_2_ADDR = 0x1f801014; // Memory Control Register 2
        static constexpr uint32_t MEM_CONTROL_3_ADDR = 0x1f801018; // Memory Control Register 3
        static constexpr uint32_t MEM_CONTROL_4_ADDR = 0x1f80101c; // Memory Control Register 4
        
    private:
        uint32_t m_memControl;    // Memory control register (0x1f801010)
        uint32_t m_ramSize;       // RAM size register (0x1f801060)
        uint32_t m_memControl2;   // Additional control registers
        uint32_t m_memControl3;
        uint32_t m_memControl4;
    };
}
