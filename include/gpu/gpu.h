/*
 * ZonistationOne - PlayStation 1 Emulator
 * Graphics Processing Unit Header
 */

#pragma once

#include <cstdint>

namespace ZonistationOne {
    
    class Memory;
    
    class GPU {
    public:
        GPU(Memory* memory);
        ~GPU();
        
        bool initialize();
        void shutdown();
        void reset();
        
        // Rendering
        void endFrame();
        void executeCommand(uint32_t command);
        
        // VRAM access
        uint16_t readVRAM(uint32_t address);
        void writeVRAM(uint32_t address, uint16_t value);
        
        // Status
        uint32_t getStatus() const { return m_status; }
        
    private:
        Memory* m_memory;
        uint32_t m_status;
        
        // Display parameters
        int m_displayWidth = 320;
        int m_displayHeight = 240;
    };
}