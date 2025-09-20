/*
 * ZonistationOne - PlayStation 1 Emulator
 * Interrupt Controller Implementation
 * Based on PCSX-Redux architecture
 */

#include "core/interrupt.h"
#include "core/logger.h"

namespace ZonistationOne {
    
    InterruptController::InterruptController() {
        reset();
    }
    
    InterruptController::~InterruptController() {
        // Nothing to do
    }
    
    void InterruptController::reset() {
        m_status = 0;
        m_mask = 0;
        ZONI_LOG_DEBUG(SYSTEM, "Interrupt controller reset");
    }
    
    uint32_t InterruptController::readRegister(uint32_t address) {
        switch (address) {
            case IREG_ADDR:
                ZONI_LOG_DEBUG(SYSTEM, "IREG read: 0x%08x", m_status);
                return m_status;
            case IMASK_ADDR:
                ZONI_LOG_DEBUG(SYSTEM, "IMASK read: 0x%08x", m_mask);
                return m_mask;
            default:
                ZONI_LOG_WARN(SYSTEM, "Unknown interrupt register read: 0x%08x", address);
                return 0;
        }
    }
    
    void InterruptController::writeRegister(uint32_t address, uint32_t value) {
        switch (address) {
            case IREG_ADDR:
                // Writing to IREG acknowledges/clears interrupts (Redux pattern)
                m_status &= ~value;
                ZONI_LOG_INFO(SYSTEM, "IREG write: 0x%08x (status now: 0x%08x)", value, m_status);
                break;
            case IMASK_ADDR:
                m_mask = value;
                ZONI_LOG_INFO(SYSTEM, "IMASK write: 0x%08x", value);
                break;
            default:
                ZONI_LOG_WARN(SYSTEM, "Unknown interrupt register write: 0x%08x = 0x%08x", address, value);
                break;
        }
    }
    
    void InterruptController::triggerInterrupt(uint32_t mask) {
        m_status |= mask;
        ZONI_LOG_DEBUG(SYSTEM, "Interrupt triggered: 0x%08x (status: 0x%08x)", mask, m_status);
    }
    
    void InterruptController::clearInterrupt(uint32_t mask) {
        m_status &= ~mask;
        ZONI_LOG_DEBUG(SYSTEM, "Interrupt cleared: 0x%08x (status: 0x%08x)", mask, m_status);
    }
    
    bool InterruptController::isPending() const {
        return (m_status & m_mask) != 0;
    }
}
