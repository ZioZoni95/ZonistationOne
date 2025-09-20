/*
 * ZonistationOne - PlayStation 1 Emulator
 * Memory Controller Implementation
 * Based on PCSX-Redux architecture
 */

#include "memory/memory_controller.h"
#include "core/logger.h"

namespace ZonistationOne {
    
    MemoryController::MemoryController() {
        reset();
    }
    
    MemoryController::~MemoryController() {
        // Nothing to do
    }
    
    void MemoryController::reset() {
        // Initialize with realistic default values based on PlayStation hardware
        m_memControl = 0x0013243f;  // Default memory control configuration
        m_ramSize = 0x00000b88;     // 2MB main RAM configuration
        m_memControl2 = 0x00000000;
        m_memControl3 = 0x00000000;
        m_memControl4 = 0x00000000;
        
        ZONI_LOG_DEBUG(SYSTEM, "Memory controller reset - RAM size: 2MB");
    }
    
    uint32_t MemoryController::readRegister(uint32_t address) {
        switch (address) {
            case MEM_CONTROL_ADDR:
                ZONI_LOG_DEBUG(SYSTEM, "Memory Control read: 0x%08x", m_memControl);
                return m_memControl;
                
            case RAM_SIZE_ADDR:
                ZONI_LOG_INFO(SYSTEM, "RAM Size read: 0x%08x (2MB)", m_ramSize);
                return m_ramSize;
                
            case MEM_CONTROL_2_ADDR:
                ZONI_LOG_DEBUG(SYSTEM, "Memory Control 2 read: 0x%08x", m_memControl2);
                return m_memControl2;
                
            case MEM_CONTROL_3_ADDR:
                ZONI_LOG_DEBUG(SYSTEM, "Memory Control 3 read: 0x%08x", m_memControl3);
                return m_memControl3;
                
            case MEM_CONTROL_4_ADDR:
                ZONI_LOG_DEBUG(SYSTEM, "Memory Control 4 read: 0x%08x", m_memControl4);
                return m_memControl4;
                
            default:
                ZONI_LOG_WARN(SYSTEM, "Unknown memory controller register read: 0x%08x", address);
                return 0;
        }
    }
    
    void MemoryController::writeRegister(uint32_t address, uint32_t value) {
        switch (address) {
            case MEM_CONTROL_ADDR:
                m_memControl = value;
                ZONI_LOG_INFO(SYSTEM, "Memory Control write: 0x%08x", value);
                break;
                
            case RAM_SIZE_ADDR:
                // RAM size is typically read-only, but some BIOS might try to write
                ZONI_LOG_INFO(SYSTEM, "RAM Size write attempt: 0x%08x (ignored - read-only)", value);
                break;
                
            case MEM_CONTROL_2_ADDR:
                m_memControl2 = value;
                ZONI_LOG_INFO(SYSTEM, "Memory Control 2 write: 0x%08x", value);
                break;
                
            case MEM_CONTROL_3_ADDR:
                m_memControl3 = value;
                ZONI_LOG_INFO(SYSTEM, "Memory Control 3 write: 0x%08x", value);
                break;
                
            case MEM_CONTROL_4_ADDR:
                m_memControl4 = value;
                ZONI_LOG_INFO(SYSTEM, "Memory Control 4 write: 0x%08x", value);
                break;
                
            default:
                ZONI_LOG_WARN(SYSTEM, "Unknown memory controller register write: 0x%08x = 0x%08x", 
                             address, value);
                break;
        }
    }
    
} // namespace ZonistationOne
