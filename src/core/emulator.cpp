/*
 * ZonistationOne - PlayStation 1 Emulator  
 * Core Emulator Class Implementation
 */

#include "core/emulator.h"
#include "core/logger.h"
#include "core/debugger.h"
#include "cpu/r3000a.h"
#include "memory/memory_map.h"
#include "gpu/gpu.h" 
#include "spu/spu.h"
#include "cdrom/cdrom.h"

#include <iostream>
#include <fstream>
#include <chrono>
#include <thread>

namespace ZonistationOne {

Emulator::Emulator() {
    ZONI_LOG_INFO(CORE, "Creating ZonistationOne emulator instance");
}

Emulator::~Emulator() {
    shutdown();
}

bool Emulator::initialize() {
    ZONI_LOG_INFO(CORE, "Initializing emulator components...");
    
    // Initialize logging system first
    if (!Logger::getInstance().initialize()) {
        std::cerr << "Failed to initialize logging system" << std::endl;
        return false;
    }
    
    try {
        // Initialize memory system first
        m_memory = std::make_unique<Memory>();
        if (!m_memory->initialize()) {
            ZONI_LOG_ERROR(CORE, "Failed to initialize memory system");
            return false;
        }
        
        // Initialize CPU
        m_cpu = std::make_unique<CPU>(m_memory.get());
        if (!m_cpu->initialize()) {
            ZONI_LOG_ERROR(CORE, "Failed to initialize CPU");
            return false;
        }
        
        // Connect CPU to Memory for cache operations
        m_memory->setCPU(m_cpu.get());
        
        // Initialize debugger
        m_debugger = std::make_unique<Debugger>(this);
        
        // Connect debugger to components
        m_debugger->connectComponents(m_cpu.get(), m_memory.get());
        
        // Initialize GPU
        m_gpu = std::make_unique<GPU>(m_memory.get());
        if (!m_gpu->initialize()) {
            ZONI_LOG_ERROR(CORE, "Failed to initialize GPU");
            return false;
        }
        
        // Initialize SPU
        m_spu = std::make_unique<SPU>(m_memory.get());
        if (!m_spu->initialize()) {
            ZONI_LOG_ERROR(CORE, "Failed to initialize SPU");
            return false;
        }
        
        // Initialize CD-ROM
        m_cdrom = std::make_unique<CDROM>(m_memory.get());
        if (!m_cdrom->initialize()) {
            ZONI_LOG_ERROR(CORE, "Failed to initialize CD-ROM");
            return false;
        }
        
        ZONI_LOG_INFO(CORE, "All components initialized successfully");
        return true;
        
    } catch (const std::exception& e) {
        ZONI_LOG_ERROR(CORE, "Exception during initialization: %s", e.what());
        return false;
    }
}

void Emulator::run() {
    if (!m_cpu || !m_memory) {
        ZONI_LOG_ERROR(CORE, "Emulator not properly initialized");
        return;
    }
    
    ZONI_LOG_INFO(CORE, "Starting emulation loop...");
    m_running = true;
    
    // Basic timing for 60 FPS
    auto lastTime = std::chrono::high_resolution_clock::now();
    const auto frameDuration = std::chrono::microseconds(16667); // ~60 FPS
    
    while (m_running) {
        if (!m_paused) {
            executeFrame();
        }
        
        // Handle debugger pause state
        if (m_debugger && m_debugger->isPaused()) {
            m_paused = true;
        }
        
        // Basic timing control
        auto currentTime = std::chrono::high_resolution_clock::now();
        auto elapsed = currentTime - lastTime;
        
        if (elapsed < frameDuration) {
            std::this_thread::sleep_for(frameDuration - elapsed);
        }
        lastTime = std::chrono::high_resolution_clock::now();
    }
    
    ZONI_LOG_INFO(CORE, "Emulation stopped");
}

void Emulator::stop() {
    ZONI_LOG_INFO(CORE, "Stopping emulation...");
    m_running = false;
}

void Emulator::executeFrame() {
    // Execute one frame worth of CPU cycles
    uint32_t cyclesToExecute = CYCLES_PER_FRAME;
    
    while (cyclesToExecute > 0 && m_running) {
        // Check if CPU is halted before executing
        if (m_cpu->isHalted()) {
            ZONI_LOG_WARN(CORE, "CPU is halted, stopping emulation");
            m_running = false;
            break;
        }
        
        uint32_t executed = m_cpu->step();
        cyclesToExecute -= executed;
        m_cycleCount += executed;
        
        // Update other components periodically
        if (m_cycleCount % 1000 == 0) {
            updateTimers();
        }
    }
    
    // End of frame updates
    if (m_running) {
        m_gpu->endFrame();
    }
}

void Emulator::updateTimers() {
    // TODO: Implement timer updates for GPU, SPU, etc.
}

bool Emulator::loadFile(const std::string& filename) {
    ZONI_LOG_INFO(CORE, "Attempting to load file: %s", filename.c_str());
    
    // Simple file type detection based on extension
    if (filename.ends_with(".bin") || filename.ends_with(".BIN")) {
        return loadBIOS(filename);
    } else if (filename.ends_with(".iso") || filename.ends_with(".ISO") ||
               filename.ends_with(".cue") || filename.ends_with(".CUE")) {
        return loadISO(filename);
    }
    
    ZONI_LOG_ERROR(CORE, "Unsupported file type: %s", filename.c_str());
    return false;
}

bool Emulator::loadBIOS(const std::string& biosPath) {
    ZONI_LOG_INFO(BIOS, "Loading BIOS: %s", biosPath.c_str());
    
    std::ifstream file(biosPath, std::ios::binary);
    if (!file.is_open()) {
        ZONI_LOG_ERROR(BIOS, "Failed to open BIOS file: %s", biosPath.c_str());
        return false;
    }
    
    // PS1 BIOS is 512KB
    constexpr size_t BIOS_SIZE = 512 * 1024;
    std::vector<uint8_t> biosData(BIOS_SIZE);
    
    file.read(reinterpret_cast<char*>(biosData.data()), BIOS_SIZE);
    if (file.gcount() != BIOS_SIZE) {
        ZONI_LOG_ERROR(BIOS, "BIOS file size mismatch. Expected 512KB, got %ld bytes", file.gcount());
        return false;
    }
    
    // Load BIOS into memory
    if (!m_memory->loadBIOS(biosData)) {
        ZONI_LOG_ERROR(BIOS, "Failed to load BIOS into memory");
        return false;
    }
    
    ZONI_LOG_INFO(BIOS, "BIOS loaded successfully");
    return true;
}

bool Emulator::loadISO(const std::string& isoPath) {
    ZONI_LOG_INFO(CDROM, "Loading ISO: %s", isoPath.c_str());
    
    if (!m_cdrom) {
        ZONI_LOG_ERROR(CDROM, "CD-ROM not initialized");
        return false;
    }
    
    return m_cdrom->loadISO(isoPath);
}

void Emulator::pause() {
    m_paused = true;
    ZONI_LOG_INFO(CORE, "Emulator paused");
}

void Emulator::resume() {
    m_paused = false;
    ZONI_LOG_INFO(CORE, "Emulator resumed");
}

void Emulator::reset() {
    ZONI_LOG_INFO(CORE, "Resetting emulator...");
    
    if (m_cpu) m_cpu->reset();
    if (m_memory) m_memory->reset();
    if (m_gpu) m_gpu->reset();
    if (m_spu) m_spu->reset();
    if (m_cdrom) m_cdrom->reset();
    
    m_cycleCount = 0;
    ZONI_LOG_INFO(CORE, "Reset complete");
}

void Emulator::shutdown() {
    m_running = false;
    
    // Clean shutdown of components
    if (m_cdrom) m_cdrom->shutdown();
    if (m_spu) m_spu->shutdown();
    if (m_gpu) m_gpu->shutdown();
    if (m_cpu) m_cpu->shutdown();
    if (m_memory) m_memory->shutdown();
    
    // Shutdown debugger and logger last
    m_debugger.reset();
    Logger::getInstance().shutdown();
    
    ZONI_LOG_INFO(CORE, "Emulator shutdown complete");
}

} // namespace ZonistationOne