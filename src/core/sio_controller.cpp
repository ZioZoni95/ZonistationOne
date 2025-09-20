/*
 * ZonistationOne - PlayStation 1 Emulator
 * SIO (Serial Interface) Controller Implementation
 * Based on PCSX-Redux architecture
 */

#include "core/sio_controller.h"
#include "core/logger.h"

namespace ZonistationOne {
    
    SIOController::SIOController() {
        reset();
    }
    
    SIOController::~SIOController() {
        // Nothing to do
    }
    
    void SIOController::reset() {
        // Initialize SIO registers with default values
        m_sioData = 0x00;
        m_sioStatus = 0x05;        // Ready to transmit/receive
        m_sioMode = 0x00;
        m_sioControl = 0x00;
        m_sioBaud = 0x00;
        
        // Initialize SIO port 2 registers
        m_sioData2 = 0x00;
        m_sioStatus2 = 0x05;       // Ready to transmit/receive
        m_sioMode2 = 0x00;
        m_sioControl2 = 0x00;
        m_sioBaud2 = 0x00;
        
        // Default controller state
        m_controllerConnected = true;  // Assume controller is connected
        m_controllerData = 0xFFFF;     // All buttons released
        
        ZONI_LOG_DEBUG(SYSTEM, "SIO controller reset");
    }
    
    uint32_t SIOController::readRegister(uint32_t address) {
        switch (address) {
            case SIO_DATA_ADDR:
                ZONI_LOG_DEBUG(SYSTEM, "SIO Data read: 0x%08x", m_sioData);
                return m_sioData;
                
            case SIO_STAT_ADDR:
                ZONI_LOG_DEBUG(SYSTEM, "SIO Status read: 0x%08x", m_sioStatus);
                return m_sioStatus;
                
            case SIO_MODE_ADDR:
                ZONI_LOG_DEBUG(SYSTEM, "SIO Mode read: 0x%08x", m_sioMode);
                return m_sioMode;
                
            case SIO_CTRL_ADDR:
                ZONI_LOG_DEBUG(SYSTEM, "SIO Control read: 0x%08x", m_sioControl);
                return m_sioControl;
                
            case SIO_BAUD_ADDR:
                ZONI_LOG_DEBUG(SYSTEM, "SIO Baud read: 0x%08x", m_sioBaud);
                return m_sioBaud;
                
            // SIO Port 2 registers
            case SIO_DATA2_ADDR:
                ZONI_LOG_DEBUG(SYSTEM, "SIO Data2 read: 0x%08x", m_sioData2);
                return m_sioData2;
                
            case SIO_STAT2_ADDR:
                ZONI_LOG_DEBUG(SYSTEM, "SIO Status2 read: 0x%08x", m_sioStatus2);
                return m_sioStatus2;
                
            case SIO_MODE2_ADDR:
                ZONI_LOG_DEBUG(SYSTEM, "SIO Mode2 read: 0x%08x", m_sioMode2);
                return m_sioMode2;
                
            case SIO_CTRL2_ADDR:
                ZONI_LOG_DEBUG(SYSTEM, "SIO Control2 read: 0x%08x", m_sioControl2);
                return m_sioControl2;
                
            case SIO_BAUD2_ADDR:
                ZONI_LOG_DEBUG(SYSTEM, "SIO Baud2 read: 0x%08x", m_sioBaud2);
                return m_sioBaud2;
                
            default:
                ZONI_LOG_WARN(SYSTEM, "Unknown SIO register read: 0x%08x", address);
                return 0;
        }
    }
    
    void SIOController::writeRegister(uint32_t address, uint32_t value) {
        switch (address) {
            case SIO_DATA_ADDR:
                m_sioData = value & 0xFF;  // 8-bit data register
                ZONI_LOG_INFO(SYSTEM, "SIO Data write: 0x%02x", m_sioData);
                
                // Simulate controller response
                if (m_controllerConnected) {
                    // Simple controller response simulation
                    if (m_sioData == 0x01) {
                        m_sioData = 0x41;  // Controller ID response
                    } else if (m_sioData == 0x42) {
                        m_sioData = 0x5A;  // Controller data ready
                    }
                }
                break;
                
            case SIO_STAT_ADDR:
                // Status register is mostly read-only, but some bits can be cleared
                m_sioStatus &= ~(value & 0x1F);  // Clear writable bits
                ZONI_LOG_INFO(SYSTEM, "SIO Status write: 0x%08x (result: 0x%08x)", value, m_sioStatus);
                break;
                
            case SIO_MODE_ADDR:
                m_sioMode = value & 0xFFFF;
                ZONI_LOG_INFO(SYSTEM, "SIO Mode write: 0x%04x", m_sioMode);
                break;
                
            case SIO_CTRL_ADDR:
                m_sioControl = value & 0xFFFF;
                ZONI_LOG_INFO(SYSTEM, "SIO Control write: 0x%04x", m_sioControl);
                
                // Handle control commands
                if (m_sioControl & 0x10) {  // Reset bit
                    ZONI_LOG_INFO(SYSTEM, "SIO Reset requested");
                    reset();
                }
                break;
                
            case SIO_BAUD_ADDR:
                m_sioBaud = value & 0xFFFF;
                ZONI_LOG_INFO(SYSTEM, "SIO Baud write: 0x%04x", m_sioBaud);
                break;
                
            // SIO Port 2 registers
            case SIO_DATA2_ADDR:
                m_sioData2 = value & 0xFF;
                ZONI_LOG_INFO(SYSTEM, "SIO Data2 write: 0x%02x", m_sioData2);
                break;
                
            case SIO_STAT2_ADDR:
                m_sioStatus2 &= ~(value & 0x1F);
                ZONI_LOG_INFO(SYSTEM, "SIO Status2 write: 0x%08x", value);
                break;
                
            case SIO_MODE2_ADDR:
                m_sioMode2 = value & 0xFFFF;
                ZONI_LOG_INFO(SYSTEM, "SIO Mode2 write: 0x%04x", m_sioMode2);
                break;
                
            case SIO_CTRL2_ADDR:
                m_sioControl2 = value & 0xFFFF;
                ZONI_LOG_INFO(SYSTEM, "SIO Control2 write: 0x%04x", m_sioControl2);
                break;
                
            case SIO_BAUD2_ADDR:
                m_sioBaud2 = value & 0xFFFF;
                ZONI_LOG_INFO(SYSTEM, "SIO Baud2 write: 0x%04x", m_sioBaud2);
                break;
                
            default:
                ZONI_LOG_WARN(SYSTEM, "Unknown SIO register write: 0x%08x = 0x%08x", address, value);
                break;
        }
    }
    
    void SIOController::updateControllerState() {
        // Update controller state - for now just maintain connected state
        // In a full implementation, this would poll actual input devices
        if (m_controllerConnected) {
            m_controllerData = 0xFFFF;  // All buttons released
        }
    }
    
    bool SIOController::isControllerConnected(int port) const {
        // For now, always report controller connected on port 0
        return (port == 0) ? m_controllerConnected : false;
    }
    
} // namespace ZonistationOne
