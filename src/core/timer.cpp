/*
 * ZonistationOne - PlayStation 1 Emulator
 * Timer/Counter Module Implementation
 * Based on PCSX-Redux architecture
 */

#include "core/timer.h"
#include "core/logger.h"

namespace ZonistationOne {
    
    TimerModule::TimerModule() {
        reset();
    }
    
    TimerModule::~TimerModule() {
        // Nothing to do
    }
    
    void TimerModule::reset() {
        for (int i = 0; i < 3; i++) {
            m_timers[i].reset();
        }
        ZONI_LOG_DEBUG(SYSTEM, "Timer module reset");
    }
    
    void TimerModule::update(uint32_t cycles) {
        for (int i = 0; i < 3; i++) {
            updateTimer(i, cycles);
        }
    }
    
    void TimerModule::updateTimer(int timerIndex, uint32_t cycles) {
        Timer& timer = m_timers[timerIndex];
        
        // Simple increment for now - BIOS mainly needs basic timer access
        if (timer.mode & TIMER_MODE_GATE) {
            // Gate mode - not implemented yet
            return;
        }
        
        uint32_t increment = cycles; // Simplified timing
        timer.count += increment;
        
        // Check for target reached
        if (timer.count >= timer.target && timer.target > 0) {
            if (timer.mode & TIMER_MODE_TARGET_INTERRUPT) {
                timer.mode |= TIMER_MODE_INTERRUPT_REQUEST;
                timer.mode |= TIMER_MODE_TARGET_REACHED;
            }
            
            if (timer.mode & TIMER_MODE_RESET_AFTER_TARGET) {
                timer.count = 0;
            }
        }
        
        // Check for overflow
        if (timer.count > 0xFFFF) {
            if (timer.mode & TIMER_MODE_OVERFLOW_INTERRUPT) {
                timer.mode |= TIMER_MODE_INTERRUPT_REQUEST;
                timer.mode |= TIMER_MODE_OVERFLOW_REACHED;
            }
            timer.count &= 0xFFFF;
        }
    }
    
    uint32_t TimerModule::readRegister(uint32_t address) {
        int timerIndex = getTimerIndex(address);
        if (timerIndex < 0) {
            ZONI_LOG_WARN(SYSTEM, "Invalid timer register read: 0x%08x", address);
            return 0;
        }
        
        uint32_t offset = getRegisterOffset(address);
        Timer& timer = m_timers[timerIndex];
        
        switch (offset) {
            case 0x00: // COUNT
                ZONI_LOG_DEBUG(SYSTEM, "Timer%d COUNT read: 0x%04x", timerIndex, timer.count & 0xFFFF);
                return timer.count & 0xFFFF;
            case 0x04: // MODE
                ZONI_LOG_DEBUG(SYSTEM, "Timer%d MODE read: 0x%08x", timerIndex, timer.mode);
                return timer.mode;
            case 0x08: // TARGET
                ZONI_LOG_DEBUG(SYSTEM, "Timer%d TARGET read: 0x%04x", timerIndex, timer.target & 0xFFFF);
                return timer.target & 0xFFFF;
            default:
                ZONI_LOG_WARN(SYSTEM, "Unknown timer register read: 0x%08x", address);
                return 0;
        }
    }
    
    void TimerModule::writeRegister(uint32_t address, uint32_t value) {
        int timerIndex = getTimerIndex(address);
        if (timerIndex < 0) {
            ZONI_LOG_WARN(SYSTEM, "Invalid timer register write: 0x%08x = 0x%08x", address, value);
            return;
        }
        
        uint32_t offset = getRegisterOffset(address);
        Timer& timer = m_timers[timerIndex];
        
        switch (offset) {
            case 0x00: // COUNT
                timer.count = value & 0xFFFF;
                ZONI_LOG_INFO(SYSTEM, "Timer%d COUNT write: 0x%04x", timerIndex, timer.count);
                break;
            case 0x04: // MODE
                timer.mode = value;
                // Clear interrupt flags on write (Redux pattern)
                timer.mode &= ~(TIMER_MODE_INTERRUPT_REQUEST | TIMER_MODE_TARGET_REACHED | TIMER_MODE_OVERFLOW_REACHED);
                ZONI_LOG_INFO(SYSTEM, "Timer%d MODE write: 0x%08x", timerIndex, timer.mode);
                break;
            case 0x08: // TARGET
                timer.target = value & 0xFFFF;
                ZONI_LOG_INFO(SYSTEM, "Timer%d TARGET write: 0x%04x", timerIndex, timer.target);
                break;
            default:
                ZONI_LOG_WARN(SYSTEM, "Unknown timer register write: 0x%08x = 0x%08x", address, value);
                break;
        }
    }
    
    bool TimerModule::hasPendingInterrupts() const {
        for (int i = 0; i < 3; i++) {
            if (m_timers[i].mode & TIMER_MODE_INTERRUPT_REQUEST) {
                return true;
            }
        }
        return false;
    }
    
    int TimerModule::getTimerIndex(uint32_t address) const {
        if (address >= TIMER0_COUNT && address <= TIMER0_TARGET) {
            return 0;
        }
        if (address >= TIMER1_COUNT && address <= TIMER1_TARGET) {
            return 1;
        }
        if (address >= TIMER2_COUNT && address <= TIMER2_TARGET) {
            return 2;
        }
        return -1;
    }
    
    uint32_t TimerModule::getRegisterOffset(uint32_t address) const {
        // Calculate offset within timer block (each timer is 0x10 bytes apart)
        if (address >= TIMER0_COUNT && address <= TIMER0_TARGET) {
            return address - TIMER0_COUNT;
        }
        if (address >= TIMER1_COUNT && address <= TIMER1_TARGET) {
            return address - TIMER1_COUNT;
        }
        if (address >= TIMER2_COUNT && address <= TIMER2_TARGET) {
            return address - TIMER2_COUNT;
        }
        return 0;
    }
}
