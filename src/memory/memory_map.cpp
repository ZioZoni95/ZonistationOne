/*
 * ZonistationOne - PlayStation 1 Emulator
 * Memory Management Unit Implementation (Stub)
 */

#include "memory/memory_map.h"
#include "core/logger.h"
#include "core/interrupt.h"
#include "core/timer.h"
#include <iostream>
#include <cstring>

namespace ZonistationOne {

Memory::Memory() {
    // Initialize all memory to zero
    m_ram.fill(0);
    m_vram.fill(0);
    m_bios.fill(0);
    m_scratchpad.fill(0);
    
    // Initialize hardware registers and expansion memory (Redux patterns)
    m_hardwareRegs.fill(0);
    m_exp1.fill(0xFF);  // EXP1 typically defaults to 0xFF like Redux
    
    // Initialize cache control (BIU - Bus Interface Unit)
    m_BIU = 0;
    
    // Create modular hardware components (Redux architecture)
    m_interruptController = std::make_unique<InterruptController>();
    m_timerModule = std::make_unique<TimerModule>();
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
    
    // Reset hardware registers and expansion memory to defaults
    m_hardwareRegs.fill(0);
    m_exp1.fill(0xFF);  // EXP1 defaults to 0xFF
    
    // Reset modular hardware components (Redux architecture)
    m_interruptController->reset();
    m_timerModule->reset();
    
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
    if (address >= RAM_BASE && address < 0x00800000) return true;  // 8MB mirrored RAM
    if (address >= BIOS_BASE && address < BIOS_BASE + BIOS_SIZE) return true;
    if (address >= SCRATCH_BASE && address < SCRATCH_BASE + SCRATCH_SIZE) return true;
    if (address >= IO_BASE && address < IO_BASE + IO_SIZE) return true;
    if (address >= EXP1_BASE && address < EXP1_BASE + EXP1_SIZE) return true; // EXP1 region
    
    return false;
}

uint8_t Memory::read8(uint32_t address) {
    // Special case: Cache control register (BIU - Bus Interface Unit)
    if (address == BIU_CONFIG) {
        // BIU is 32-bit register, return appropriate byte
        uint8_t byteIndex = address & 0x3;
        return (m_BIU >> (byteIndex * 8)) & 0xFF;
    }
    
    uint32_t translatedAddr = translateAddress(address);
    
    // RAM region with mirroring - PS1 2MB RAM is mirrored every 2MB up to 8MB
    if (translatedAddr >= RAM_BASE && translatedAddr < 0x00800000) {  // 8MB range
        uint32_t ramOffset = (translatedAddr - RAM_BASE) % RAM_SIZE;  // Mirror every 2MB
        return m_ram[ramOffset];
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
    
    // I/O region - hardware registers (Redux patterns)
    if (translatedAddr >= IO_BASE && translatedAddr < IO_BASE + IO_SIZE) {
        uint32_t offset = translatedAddr - IO_BASE;
        if (offset < 0x2000) {  // Within our hardware register range
            return m_hardwareRegs[offset];
        }
        return 0;
    }
    
    // EXP1 region (expansion memory)
    if (translatedAddr >= EXP1_BASE && translatedAddr < EXP1_BASE + EXP1_SIZE) {
        uint32_t offset = translatedAddr - EXP1_BASE;
        return m_exp1[offset];
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
    // Special case: Cache control register (BIU)
    if (address == BIU_CONFIG) {
        return m_BIU;
    }
    
    // Hardware registers - delegate to modular components (Redux architecture)
    // Interrupt Controller (0x1f801070-0x1f801074)
    if (address == IREG || address == IMASK) {
        return m_interruptController->readRegister(address);
    }
    
    // Timer/Counter registers (0x1f801100-0x1f801128)
    if (address >= TIMER0_COUNT && address <= TIMER2_TARGET) {
        return m_timerModule->readRegister(address);
    }
    
    // Simple implementation - read four bytes
    uint32_t result = 0;
    result |= read8(address);
    result |= read8(address + 1) << 8;
    result |= read8(address + 2) << 16;
    result |= read8(address + 3) << 24;
    return result;
}

void Memory::write8(uint32_t address, uint8_t value) {
    // Special case: Cache control register (BIU - Bus Interface Unit)  
    if (address == BIU_CONFIG) {
        // BIU is typically written as 32-bit, but handle 8-bit writes
        uint8_t byteIndex = address & 0x3;
        uint32_t mask = 0xFF << (byteIndex * 8);
        m_BIU = (m_BIU & ~mask) | ((uint32_t)value << (byteIndex * 8));
        ZONI_LOG_WARN(MEMORY, "8-bit write to BIU cache control: 0x%08x = 0x%02x (BIU now: 0x%08x)", address, value, m_BIU);
        return;
    }
    
    uint32_t translatedAddr = translateAddress(address);
    
    // RAM region with mirroring - PS1 2MB RAM is mirrored every 2MB up to 8MB
    if (translatedAddr >= RAM_BASE && translatedAddr < 0x00800000) {  // 8MB range
        uint32_t ramOffset = (translatedAddr - RAM_BASE) % RAM_SIZE;  // Mirror every 2MB
        m_ram[ramOffset] = value;
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
    
    // I/O region - hardware registers (Redux patterns)
    if (translatedAddr >= IO_BASE && translatedAddr < IO_BASE + IO_SIZE) {
        uint32_t offset = translatedAddr - IO_BASE;
        if (offset < 0x2000) {  // Within our hardware register range
            m_hardwareRegs[offset] = value;
            ZONI_LOG_DEBUG(MEMORY, "Hardware register write8 at 0x%08x = 0x%02x", address, value);
            return;
        }
        return;
    }
    
    // EXP1 region (expansion memory)
    if (translatedAddr >= EXP1_BASE && translatedAddr < EXP1_BASE + EXP1_SIZE) {
        uint32_t offset = translatedAddr - EXP1_BASE;
        m_exp1[offset] = value;
        if (address == 0x1f000084) {  // Log the specific address BIOS is accessing
            ZONI_LOG_INFO(MEMORY, "BIOS wrote to expansion config register 0x1f000084 = 0x%02x", value);
        }
        return;
    }
    
    // Unknown address - log but don't crash
    ZONI_LOG_WARN(MEMORY, "8-bit write to unknown address 0x%08x = 0x%02x (ignored)", address, value);
}

void Memory::write16(uint32_t address, uint16_t value) {
    // Default implementation - write two bytes (BIU handling is in write8)
    write8(address, value & 0xFF);
    write8(address + 1, (value >> 8) & 0xFF);
}

void Memory::write32(uint32_t address, uint32_t value) {
    // Special case: Cache control register (BIU - Bus Interface Unit)
    if (address == BIU_CONFIG) {
        ZONI_LOG_INFO(MEMORY, "BIU cache control write: 0x%08x = 0x%08x", address, value);
        m_BIU = value;
        
        // Handle cache control operations based on Redux patterns
        switch (value) {
            case 0x00000800:
            case 0x00000804:
            case 0x0001e90c:  // TOCA World Touring Cars, FlushCache operation
                ZONI_LOG_INFO(MEMORY, "BIU: Cache invalidation requested (0x%08x)", value);
                // TODO: CPU cache invalidation when CPU interface is available
                break;
            case 0x0001e988:
                ZONI_LOG_INFO(MEMORY, "BIU: Cache configuration set (0x%08x)", value);
                // TODO: Set memory lookup tables when needed
                break;
            default:
                ZONI_LOG_DEBUG(MEMORY, "BIU: Unknown cache control value 0x%08x", value);
                break;
        }
        return;
    }
    
    // Hardware registers - delegate to modular components (Redux architecture)
    // Interrupt Controller (0x1f801070-0x1f801074)
    if (address == IREG || address == IMASK) {
        m_interruptController->writeRegister(address, value);
        return;
    }
    
    // Timer/Counter registers (0x1f801100-0x1f801128)
    if (address >= TIMER0_COUNT && address <= TIMER2_TARGET) {
        m_timerModule->writeRegister(address, value);
        return;
    }
    
    // Default implementation - write four bytes
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
    // Copy BIOS data
    for (size_t i = 0; i < BIOS_SIZE; ++i) {
        m_bios[i] = biosData[i];
    }
    m_biosLoaded = true;
    
    ZONI_LOG_INFO(MEMORY, "BIOS loaded successfully");
    return true;
}

} // namespace ZonistationOne