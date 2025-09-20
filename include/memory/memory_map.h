/*
 * ZonistationOne - PlayStation 1 Emulator
 * Memory Management Unit Header
 */

#pragma once

#include <cstdint>
#include <vector>
#include <array>
#include <memory>

// Forward declarations for modular hardware components
namespace ZonistationOne {
    class InterruptController;
    class TimerModule;
    class MemoryController;
    class SIOController;
    class CPU;  // Forward declaration for cache operations
}

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
        
        // CPU reference for cache operations
        void setCPU(CPU* cpu) { m_cpu = cpu; }
        
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
        
        // Hardware registers (following Redux patterns)
        uint32_t m_biuConfig = 0;                         // BIU_CONFIG register at 0xfffe0130
        std::array<uint8_t, 0x2000> m_hardwareRegs;      // Hardware registers 0x1f801000-0x1f802fff
        std::array<uint8_t, 0x800000> m_exp1;            // EXP1 region 0x1f000000-0x1f7fffff
        
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
        static constexpr uint32_t IO_SIZE = 0x00002000;       // Extended to 8KB for hardware registers
        
        // EXP1 region (expansion memory)
        static constexpr uint32_t EXP1_BASE = 0x1F000000;
        static constexpr uint32_t EXP1_SIZE = 0x00800000;     // 8MB
        
        // Special addresses
        static constexpr uint32_t BIU_CONFIG = 0xfffe0130;
        
        // Hardware register offsets (Redux patterns)
        static constexpr uint32_t MEMCTRL_BASE = 0x1f801000;  // Memory control registers
        static constexpr uint32_t RAM_SIZE_REG = 0x1f801060;  // RAM size register
        static constexpr uint32_t SIO_BASE = 0x1f801040;      // SIO Controller registers
        static constexpr uint32_t IRQ_CTRL_BASE = 0x1f801070; // Interrupt control
        static constexpr uint32_t DMA_BASE = 0x1f801080;      // DMA registers
        static constexpr uint32_t TIMER_BASE = 0x1f801100;    // Timer/Counter registers
        
        // Specific hardware registers (following Redux)
        static constexpr uint32_t IREG = 0x1f801070;          // Interrupt Status Register
        static constexpr uint32_t IMASK = 0x1f801074;         // Interrupt Mask Register
        static constexpr uint32_t TIMER0_COUNT = 0x1f801100;  // Timer 0 Counter Value
        static constexpr uint32_t TIMER0_MODE = 0x1f801104;   // Timer 0 Mode Register
        static constexpr uint32_t TIMER0_TARGET = 0x1f801108; // Timer 0 Target Value
        static constexpr uint32_t TIMER1_COUNT = 0x1f801110;  // Timer 1 Counter Value
        static constexpr uint32_t TIMER1_MODE = 0x1f801114;   // Timer 1 Mode Register
        static constexpr uint32_t TIMER1_TARGET = 0x1f801118; // Timer 1 Target Value
        static constexpr uint32_t TIMER2_COUNT = 0x1f801120;  // Timer 2 Counter Value
        static constexpr uint32_t TIMER2_MODE = 0x1f801124;   // Timer 2 Mode Register
        static constexpr uint32_t TIMER2_TARGET = 0x1f801128; // Timer 2 Target Value
        
        // Cache control (BIU - Bus Interface Unit)
        uint32_t m_BIU = 0;
        bool isCacheEnabled() const { return m_BIU == 0x1e988; }
        
        // Modular hardware components (Redux architecture)
        std::unique_ptr<InterruptController> m_interruptController;
        std::unique_ptr<TimerModule> m_timerModule;
        std::unique_ptr<MemoryController> m_memoryController;
        std::unique_ptr<SIOController> m_sioController;
        
        // CPU reference for cache operations
        CPU* m_cpu = nullptr;
        
        bool m_biosLoaded = false;
    };
}