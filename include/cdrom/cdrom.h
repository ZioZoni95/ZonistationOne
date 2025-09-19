/*
 * ZonistationOne - PlayStation 1 Emulator
 * CD-ROM Drive Header
 */

#pragma once

#include <cstdint>
#include <string>

namespace ZonistationOne {
    
    class Memory;
    
    class CDROM {
    public:
        CDROM(Memory* memory);
        ~CDROM();
        
        bool initialize();
        void shutdown();
        void reset();
        
        // ISO management
        bool loadISO(const std::string& isoPath);
        void unloadISO();
        
        // Data access
        bool seekToSector(uint32_t sector);
        bool readSector(uint8_t* buffer);
        
        // Status
        bool hasISO() const { return m_isoLoaded; }
        
    private:
        Memory* m_memory;
        bool m_isoLoaded = false;
        std::string m_currentISO;
    };
}