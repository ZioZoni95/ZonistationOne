/*
 * ZonistationOne - PlayStation 1 Emulator
 * Sound Processing Unit Header
 */

#pragma once

#include <cstdint>

namespace ZonistationOne {
    
    class Memory;
    
    class SPU {
    public:
        SPU(Memory* memory);
        ~SPU();
        
        bool initialize();
        void shutdown();
        void reset();
        
        // Audio processing
        void update();
        
        // Register access
        uint16_t readRegister(uint32_t address);
        void writeRegister(uint32_t address, uint16_t value);
        
    private:
        Memory* m_memory;
        
        // SPU state
        bool m_enabled = false;
    };
}