/*
 * ZonistationOne - PlayStation 1 Emulator
 * Memory Management Unit Implementation (Stub)
 */

#include "memory/memory_map.h"
#include "core/logger.h"
#include <iostream>
#include <cstring>

namespace ZonistationOne {

Memory::Memory() {
    // Initialize all memory to zero
    m_ram.fill(0);
    m_vram.fill(0);
    m_bios.fill(0);
    m_scratchpad.fill(0);
}

Memory::~Memory() {
    shutdown();
}

bool Memory::initialize() {
    ZONI_LOG_INFO(MEMORY, "Initializing memory subsystem");
    ZONI_LOG_INFO(MEMORY, "RAM size: %dKB", RAM_SIZE / 1024);
    ZONI_LOG_INFO(MEMORY, "VRAM size: %dKB", VRAM_SIZE / 1024);
    ZONI_LOG_INFO(MEMORY, "BIOS size: %dKB", BIOS_SIZE / 1024);
    return true;
}

void Memory::shutdown() {
    // Nothing to cleanup
}

void Memory::reset() {
    ZONI_LOG_INFO(MEMORY, "Resetting memory state");
    
    // Clear RAM and VRAM, but preserve BIOS
    m_ram.fill(0);
    m_vram.fill(0);
    m_scratchpad.fill(0);
    
    // Don't reset BIOS as it should persist
}

uint32_t Memory::translateAddress(uint32_t address) {
    // Basic address translation - remove upper bits for cached/uncached regions
    // PS1 memory map has mirrored regions
    
    // Remove cache control bits (bits 31-29)
    address &= 0x1FFFFFFF;
    
    return address;
}

bool Memory::isValidAddress(uint32_t address) {
    address = translateAddress(address);
    
    // Check if address falls within any valid region
    if (address >= RAM_BASE && address < RAM_BASE + RAM_SIZE) return true;
    if (address >= BIOS_BASE && address < BIOS_BASE + BIOS_SIZE) return true;
    if (address >= SCRATCH_BASE && address < SCRATCH_BASE + SCRATCH_SIZE) return true;
    if (address >= IO_BASE && address < IO_BASE + IO_SIZE) return true;
    
    return false;
}

uint8_t Memory::read8(uint32_t address) {
    uint32_t translatedAddr = translateAddress(address);
    
    // RAM region
    if (translatedAddr >= RAM_BASE && translatedAddr < RAM_BASE + RAM_SIZE) {
        return m_ram[translatedAddr - RAM_BASE];
    }
    
    // BIOS region
    if (translatedAddr >= BIOS_BASE && translatedAddr < BIOS_BASE + BIOS_SIZE) {
        if (!m_biosLoaded) {
            ZONI_LOG_ERROR(MEMORY, "Attempted to read from unloaded BIOS at 0x%08x", address);
            return 0xFF;
        }
        return m_bios[translatedAddr - BIOS_BASE];
    }
    
    // Scratchpad region
    if (translatedAddr >= SCRATCH_BASE && translatedAddr < SCRATCH_BASE + SCRATCH_SIZE) {
        return m_scratchpad[translatedAddr - SCRATCH_BASE];
    }
    
    // I/O region - stub for now
    if (translatedAddr >= IO_BASE && translatedAddr < IO_BASE + IO_SIZE) {
        // TODO: Implement I/O register reads
        return 0;
    }
    
    ZONI_LOG_ERROR(MEMORY, "Invalid read8 from address 0x%08x", address);
    return 0;
}

uint16_t Memory::read16(uint32_t address) {
    // Simple implementation - read two bytes
    uint16_t low = read8(address);
    uint16_t high = read8(address + 1);
    return low | (high << 8);
}

uint32_t Memory::read32(uint32_t address) {
    // Simple implementation - read four bytes
    uint32_t result = 0;
    result |= read8(address);
    result |= read8(address + 1) << 8;
    result |= read8(address + 2) << 16;
    result |= read8(address + 3) << 24;
    return result;
}

void Memory::write8(uint32_t address, uint8_t value) {
    uint32_t translatedAddr = translateAddress(address);
    
    // RAM region
    if (translatedAddr >= RAM_BASE && translatedAddr < RAM_BASE + RAM_SIZE) {
        m_ram[translatedAddr - RAM_BASE] = value;
        return;
    }
    
    // BIOS region (read-only)
    if (translatedAddr >= BIOS_BASE && translatedAddr < BIOS_BASE + BIOS_SIZE) {
        ZONI_LOG_WARN(MEMORY, "Attempted write to read-only BIOS at 0x%08x", address);
        return;
    }
    
    // Scratchpad region
    if (translatedAddr >= SCRATCH_BASE && translatedAddr < SCRATCH_BASE + SCRATCH_SIZE) {
        m_scratchpad[translatedAddr - SCRATCH_BASE] = value;
        return;
    }
    
    // I/O region - stub for now
    if (translatedAddr >= IO_BASE && translatedAddr < IO_BASE + IO_SIZE) {
        // TODO: Implement I/O register writes
        return;
    }
    
    ZONI_LOG_ERROR(MEMORY, "Invalid write8 to address 0x%08x", address);
}

void Memory::write16(uint32_t address, uint16_t value) {
    // Simple implementation - write two bytes
    write8(address, value & 0xFF);
    write8(address + 1, (value >> 8) & 0xFF);
}

void Memory::write32(uint32_t address, uint32_t value) {
    // Simple implementation - write four bytes
    write8(address, value & 0xFF);
    write8(address + 1, (value >> 8) & 0xFF);
    write8(address + 2, (value >> 16) & 0xFF);
    write8(address + 3, (value >> 24) & 0xFF);
}

bool Memory::loadBIOS(const std::vector<uint8_t>& biosData) {
    if (biosData.size() != BIOS_SIZE) {
        ZONI_LOG_ERROR(MEMORY, "Invalid BIOS size: %zu bytes (expected %d)", 
                       biosData.size(), BIOS_SIZE);
        return false;
    }
    
    ZONI_LOG_INFO(MEMORY, "Loading BIOS into memory...");
    std::memcpy(m_bios.data(), biosData.data(), BIOS_SIZE);
    m_biosLoaded = true;
    
    ZONI_LOG_INFO(MEMORY, "BIOS loaded successfully");
    return true;
}

} // namespace ZonistationOne