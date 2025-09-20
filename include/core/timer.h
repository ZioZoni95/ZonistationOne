/*
 * ZonistationOne - PlayStation 1 Emulator
 * Timer/Counter Module Header
 * Based on PCSX-Redux architecture
 */

#ifndef ZONISTATION_TIMER_H
#define ZONISTATION_TIMER_H

#include <cstdint>

namespace ZonistationOne {
    
    // Timer register addresses
    static constexpr uint32_t TIMER0_COUNT = 0x1f801100;
    static constexpr uint32_t TIMER0_MODE = 0x1f801104;
    static constexpr uint32_t TIMER0_TARGET = 0x1f801108;
    
    static constexpr uint32_t TIMER1_COUNT = 0x1f801110;
    static constexpr uint32_t TIMER1_MODE = 0x1f801114;
    static constexpr uint32_t TIMER1_TARGET = 0x1f801118;
    
    static constexpr uint32_t TIMER2_COUNT = 0x1f801120;
    static constexpr uint32_t TIMER2_MODE = 0x1f801124;
    static constexpr uint32_t TIMER2_TARGET = 0x1f801128;
    
    // Timer mode register bits
    static constexpr uint32_t TIMER_MODE_GATE = (1 << 0);
    static constexpr uint32_t TIMER_MODE_RESET_AFTER_TARGET = (1 << 3);
    static constexpr uint32_t TIMER_MODE_TARGET_INTERRUPT = (1 << 4);
    static constexpr uint32_t TIMER_MODE_OVERFLOW_INTERRUPT = (1 << 5);
    static constexpr uint32_t TIMER_MODE_REPEAT_INTERRUPT = (1 << 6);
    static constexpr uint32_t TIMER_MODE_TOGGLE_BIT = (1 << 7);
    static constexpr uint32_t TIMER_MODE_CLOCK_SELECT = (3 << 8);
    static constexpr uint32_t TIMER_MODE_INTERRUPT_REQUEST = (1 << 10);
    static constexpr uint32_t TIMER_MODE_TARGET_REACHED = (1 << 11);
    static constexpr uint32_t TIMER_MODE_OVERFLOW_REACHED = (1 << 12);
    
    struct Timer {
        uint32_t count;
        uint32_t mode;
        uint32_t target;
        uint32_t lastUpdate;
        
        Timer() : count(0), mode(0), target(0), lastUpdate(0) {}
        
        void reset() {
            count = 0;
            mode = 0;
            target = 0;
            lastUpdate = 0;
        }
    };
    
    class TimerModule {
    public:
        TimerModule();
        ~TimerModule();
        
        void reset();
        void update(uint32_t cycles);
        
        uint32_t readRegister(uint32_t address);
        void writeRegister(uint32_t address, uint32_t value);
        
        // Check if any timer has a pending interrupt
        bool hasPendingInterrupts() const;
        
    private:
        Timer m_timers[3];
        
        void updateTimer(int timerIndex, uint32_t cycles);
        int getTimerIndex(uint32_t address) const;
        uint32_t getRegisterOffset(uint32_t address) const;
    };
}

#endif // ZONISTATION_TIMER_H
