/*
 * ZonistationOne - PlayStation 1 Emulator
 * Core Emulator Class Header
 */

#pragma once

#include <string>
#include <memory>
#include <cstdint>

namespace ZonistationOne {
    
    // Forward declarations
    class CPU;
    class Memory;
    class GPU;
    class SPU;
    class CDROM;
    class Debugger;
    
    class Emulator {
    public:
        Emulator();
        ~Emulator();
        
        // Main emulation control
        bool initialize();
        void run();
        void shutdown();
        void reset();
        
        // File loading
        bool loadFile(const std::string& filename);
        bool loadBIOS(const std::string& biosPath);
        bool loadISO(const std::string& isoPath);
        
        // Emulation state
        void pause();
        void resume();
        bool isPaused() const { return m_paused; }
        
        // Component access
        CPU* getCPU() const { return m_cpu.get(); }
        Memory* getMemory() const { return m_memory.get(); }
        GPU* getGPU() const { return m_gpu.get(); }
        SPU* getSPU() const { return m_spu.get(); }
        CDROM* getCDROM() const { return m_cdrom.get(); }
        Debugger* getDebugger() const { return m_debugger.get(); }
        
    private:
        void executeFrame();
        void updateTimers();
        
        // Core components
        std::unique_ptr<CPU> m_cpu;
        std::unique_ptr<Memory> m_memory;
        std::unique_ptr<GPU> m_gpu;
        std::unique_ptr<SPU> m_spu;
        std::unique_ptr<CDROM> m_cdrom;
        std::unique_ptr<Debugger> m_debugger;
        
        // State
        bool m_running = false;
        bool m_paused = false;
        uint64_t m_cycleCount = 0;
        
        // Timing
        static constexpr uint32_t CPU_CLOCK_SPEED = 33868800;  // ~33.87 MHz
        static constexpr uint32_t CYCLES_PER_FRAME = CPU_CLOCK_SPEED / 60;
    };
}