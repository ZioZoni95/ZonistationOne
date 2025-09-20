/*
 * ZonistationOne - PlayStation 1 Emulator
 * SIO (Serial Interface) Controller Header
 * Based on PCSX-Redux architecture
 */

#pragma once

#include <cstdint>

namespace ZonistationOne {
    
    class SIOController {
    public:
        SIOController();
        ~SIOController();
        
        void reset();
        
        // Hardware register access
        uint32_t readRegister(uint32_t address);
        void writeRegister(uint32_t address, uint32_t value);
        
        // Controller state management
        void updateControllerState();
        bool isControllerConnected(int port = 0) const;
        
        // SIO register addresses (following Redux patterns)
        static constexpr uint32_t SIO_DATA_ADDR     = 0x1f801040;  // SIO Data Register
        static constexpr uint32_t SIO_STAT_ADDR     = 0x1f801044;  // SIO Status Register
        static constexpr uint32_t SIO_MODE_ADDR     = 0x1f801048;  // SIO Mode Register  
        static constexpr uint32_t SIO_CTRL_ADDR     = 0x1f80104a;  // SIO Control Register
        static constexpr uint32_t SIO_BAUD_ADDR     = 0x1f80104e;  // SIO Baud Rate Register
        
        // Additional SIO registers
        static constexpr uint32_t SIO_DATA2_ADDR    = 0x1f801050;  // SIO Data Register 2
        static constexpr uint32_t SIO_STAT2_ADDR    = 0x1f801054;  // SIO Status Register 2
        static constexpr uint32_t SIO_MODE2_ADDR    = 0x1f801058;  // SIO Mode Register 2
        static constexpr uint32_t SIO_CTRL2_ADDR    = 0x1f80105a;  // SIO Control Register 2
        static constexpr uint32_t SIO_BAUD2_ADDR    = 0x1f80105e;  // SIO Baud Rate Register 2
        
    private:
        // SIO port 1 registers
        uint32_t m_sioData;      // SIO data register
        uint32_t m_sioStatus;    // SIO status register
        uint32_t m_sioMode;      // SIO mode register
        uint32_t m_sioControl;   // SIO control register
        uint32_t m_sioBaud;      // SIO baud rate register
        
        // SIO port 2 registers  
        uint32_t m_sioData2;     // SIO data register 2
        uint32_t m_sioStatus2;   // SIO status register 2
        uint32_t m_sioMode2;     // SIO mode register 2
        uint32_t m_sioControl2;  // SIO control register 2
        uint32_t m_sioBaud2;     // SIO baud rate register 2
        
        // Controller state
        bool m_controllerConnected;
        uint32_t m_controllerData;
    };
}
