/*
 * ZonistationOne - PlayStation 1 Emulator
 * Memory Management Unit Header
 */

#pragma once

#include <cstdint>
#include <vector>
#include <array>

namespace ZonistationOne {
    
    class Memory {
    public:
        Memory();
        ~Memory();
        
        bool initialize();
        void shutdown();
        void reset();
        
        // Memory access
        uint8_t read8(uint32_t address);
        uint16_t read16(uint32_t address);  
        uint32_t read32(uint32_t address);
        
        void write8(uint32_t address, uint8_t value);
        void write16(uint32_t address, uint16_t value);
        void write32(uint32_t address, uint32_t value);
        
        // BIOS loading
        bool loadBIOS(const std::vector<uint8_t>& biosData);
        
        // Direct memory access for components
        uint8_t* getRAMPtr() { return m_ram.data(); }
        uint8_t* getVRAMPtr() { return m_vram.data(); }
        uint8_t* getBIOSPtr() { return m_bios.data(); }
        
    private:
        uint32_t translateAddress(uint32_t address);
        bool isValidAddress(uint32_t address);
        
        // Memory regions
        std::array<uint8_t, 2 * 1024 * 1024> m_ram;      // 2MB main RAM
        std::array<uint8_t, 1024 * 1024> m_vram;         // 1MB video RAM
        std::array<uint8_t, 512 * 1024> m_bios;          // 512KB BIOS ROM
        std::array<uint8_t, 8 * 1024> m_scratchpad;      // 1KB scratchpad
        
        // Memory map constants
        static constexpr uint32_t RAM_BASE    = 0x00000000;
        static constexpr uint32_t RAM_SIZE    = 0x00200000;  // 2MB
        static constexpr uint32_t BIOS_BASE   = 0x1FC00000;
        static constexpr uint32_t BIOS_SIZE   = 0x00080000;  // 512KB
        static constexpr uint32_t VRAM_BASE   = 0x1F800000;  
        static constexpr uint32_t VRAM_SIZE   = 0x00100000;  // 1MB
        static constexpr uint32_t SCRATCH_BASE = 0x1F800000;
        static constexpr uint32_t SCRATCH_SIZE = 0x00000400;  // 1KB
        
        // I/O region
        static constexpr uint32_t IO_BASE = 0x1F801000;
        static constexpr uint32_t IO_SIZE = 0x00001000;
        
        bool m_biosLoaded = false;
    };
}