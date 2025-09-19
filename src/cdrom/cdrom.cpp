/*
 * ZonistationOne - PlayStation 1 Emulator
 * CD-ROM Implementation (Stub)
 */

#include "cdrom/cdrom.h"
#include "memory/memory_map.h"
#include "core/logger.h"
#include <iostream>

namespace ZonistationOne {

CDROM::CDROM(Memory* memory) : m_memory(memory) {
}

CDROM::~CDROM() {
    shutdown();
}

bool CDROM::initialize() {
    ZONI_LOG_INFO(CDROM, "Initializing CD-ROM subsystem");
    return true;
}

void CDROM::shutdown() {
    unloadISO();
    ZONI_LOG_INFO(CDROM, "Shutting down");
}

void CDROM::reset() {
    ZONI_LOG_INFO(CDROM, "Resetting CD-ROM state");
}

bool CDROM::loadISO(const std::string& isoPath) {
    ZONI_LOG_INFO(CDROM, "Loading ISO: %s", isoPath.c_str());
    
    // TODO: Implement actual ISO loading
    m_currentISO = isoPath;
    m_isoLoaded = true;
    
    ZONI_LOG_INFO(CDROM, "ISO loaded successfully (stub)");
    return true;
}

void CDROM::unloadISO() {
    if (m_isoLoaded) {
        ZONI_LOG_INFO(CDROM, "Unloading ISO: %s", m_currentISO.c_str());
        m_isoLoaded = false;
        m_currentISO.clear();
    }
}

bool CDROM::seekToSector(uint32_t sector) {
    // TODO: Implement sector seeking
    return m_isoLoaded;
}

bool CDROM::readSector(uint8_t* buffer) {
    // TODO: Implement sector reading
    return m_isoLoaded;
}

} // namespace ZonistationOne