/*
 * ZonistationOne - PlayStation 1 Emulator
 * Interrupt Controller Header
 * Based on PCSX-Redux architecture
 */

#pragma once

#include <cstdint>

namespace ZonistationOne {
    
    class InterruptController {
    public:
        InterruptController();
        ~InterruptController();
        
        void reset();
        
        // Hardware register access
        uint32_t readRegister(uint32_t address);
        void writeRegister(uint32_t address, uint32_t value);
        
        // Interrupt management
        void triggerInterrupt(uint32_t mask);
        void clearInterrupt(uint32_t mask);
        bool isPending() const;
        
        // Register addresses (following Redux patterns)
        static constexpr uint32_t IREG_ADDR = 0x1f801070;  // Interrupt status register
        static constexpr uint32_t IMASK_ADDR = 0x1f801074; // Interrupt mask register
        
    private:
        uint32_t m_status;    // IREG - Interrupt status register
        uint32_t m_mask;      // IMASK - Interrupt mask register
    };
}
