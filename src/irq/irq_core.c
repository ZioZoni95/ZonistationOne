/**
 * @file irq_core.c
 * @brief Interrupt Controller Core Implementation
 * 
 * Thread-safe interrupt controller for PlayStation 1 emulator.
 * Implements PSX I_STAT/I_MASK registers with edge-triggered behavior.
 * 
 * Architecture:
 * - All operations protected by recursive mutex
 * - Edge detection for IRQ line state changes (0->1 sets I_STAT bit)
 * - Write-0-to-clear behavior for I_STAT acknowledgment
 * - Statistics tracking for debugging/profiling
 * 
 * References:
 * - PSX-SPX: "I/O Ports - Interrupt Control"
 * - DuckStation: src/core/interrupt_controller.cpp
 */

#include "irq/irq_core.h"
#include "cpu/cpu_core.h"
#include "log.h"
#include <string.h>

// ============================================================================
// IRQ Line Names
// ============================================================================

const char* IRQ_LINE_NAMES[IRQ_LINE_COUNT] = {
    "VBlank",       // IRQ0
    "GPU",          // IRQ1
    "CDROM",        // IRQ2
    "DMA",          // IRQ3
    "Timer0",       // IRQ4
    "Timer1",       // IRQ5
    "Timer2",       // IRQ6
    "CtrlMemCard",  // IRQ7
    "SIO",          // IRQ8
    "SPU",          // IRQ9
    "PIO"           // IRQ10
};

// ============================================================================
// Initialization and Cleanup
// ============================================================================

void irq_init(IrqState* irq) {
    memset(irq, 0, sizeof(IrqState));
    
    // Initialize mutex for thread safety
    mutex_init(&irq->lock);
    
    // Clear hardware registers
    irq->i_stat = 0;
    irq->i_mask = 0;
    irq->line_state = 0;
    
    // Enable logging by default (will be rate-limited)
    irq->log_enabled = true;
    
    LOG_SYSTEM_INFO("[IRQ] Interrupt controller initialized (thread-safe)");
}

void irq_shutdown(IrqState* irq) {
    // Log final statistics
    if (irq->log_enabled) {
        LOG_SYSTEM_INFO("[IRQ] Shutdown statistics:");
        LOG_SYSTEM_INFO("[IRQ]   Total requests: %u", irq->total_requests);
        for (int i = 0; i < IRQ_LINE_COUNT; i++) {
            if (irq->request_count[i] > 0) {
                LOG_SYSTEM_INFO("[IRQ]   %s: %u requests, %u edges",
                              IRQ_LINE_NAMES[i],
                              irq->request_count[i],
                              irq->edge_count[i]);
            }
        }
    }
    
    // Destroy mutex
    mutex_destroy(&irq->lock);
    
    LOG_SYSTEM_INFO("[IRQ] Interrupt controller shut down");
}

// ============================================================================
// Internal Helper: Edge Detection
// ============================================================================

/**
 * @brief Internal helper for edge detection (must be called with lock held)
 */
static void irq_set_line_internal(IrqState* irq, IrqLine line, bool state) {
    const uint32_t bit = (1u << line);
    const uint16_t prev_line_state = irq->line_state;
    
    // Update line state
    if (state) {
        irq->line_state |= bit;
    } else {
        irq->line_state &= ~bit;
    }
    
    // Edge detection: only set I_STAT on rising edge (0->1 transition)
    if (state && !(prev_line_state & bit)) {
        irq->i_stat |= bit;
        irq->edge_count[line]++;
        
        // Rate-limited logging
        if (irq->log_enabled && irq->edge_count[line] <= 10) {
            LOG_IRQ_DEBUG("[IRQ] Rising edge #%u: %s (line=%u) I_STAT=0x%04x, I_MASK=0x%04x",
                         irq->edge_count[line], IRQ_LINE_NAMES[line], line,
                         irq->i_stat, irq->i_mask);
        }
    }
}

// ============================================================================
// Interrupt Request API
// ============================================================================

void irq_request(IrqState* irq, IrqLine line, const char* source) {
    mutex_lock(&irq->lock);
    
    // Update statistics
    irq->request_count[line]++;
    irq->total_requests++;
    
    // Rate-limited logging
    if (irq->log_enabled && irq->request_count[line] <= 5) {
        LOG_IRQ_DEBUG("[IRQ] Request #%u: %s from %s, I_STAT=0x%04x",
                     irq->request_count[line], IRQ_LINE_NAMES[line],
                     source, irq->i_stat);
    }
    
    // Pulse the line: rising edge (0->1) triggers I_STAT bit, then falling edge (1->0) resets for next pulse
    irq_set_line_internal(irq, line, true);   // Rising edge: sets I_STAT bit
    irq_set_line_internal(irq, line, false);  // Falling edge: prepares for next interrupt
    
    mutex_unlock(&irq->lock);
}

void irq_clear(IrqState* irq, IrqLine line, const char* source) {
    mutex_lock(&irq->lock);
    
    // Deassert the line state (for edge detection on next request)
    // Note: I_STAT bit is NOT cleared here - software must write to I_STAT
    (void)source; // May be used for logging in the future
    
    irq_set_line_internal(irq, line, false);
    
    mutex_unlock(&irq->lock);
}

void irq_set_line(IrqState* irq, IrqLine line, bool state) {
    mutex_lock(&irq->lock);
    irq_set_line_internal(irq, line, state);
    mutex_unlock(&irq->lock);
}

// ============================================================================
// Interrupt Check API
// ============================================================================

bool irq_check_pending(const IrqState* irq) {
    mutex_lock((Mutex*)&irq->lock); // Cast away const for mutex
    
    uint16_t pending = irq->i_stat & irq->i_mask;
    bool has_pending = (pending != 0);
    
    mutex_unlock((Mutex*)&irq->lock);
    
    return has_pending;
}

uint16_t irq_get_pending_mask(const IrqState* irq) {
    mutex_lock((Mutex*)&irq->lock); // Cast away const for mutex
    
    uint16_t pending = irq->i_stat & irq->i_mask;
    
    mutex_unlock((Mutex*)&irq->lock);
    
    return pending;
}

// ============================================================================
// Hardware Register Access API
// ============================================================================

uint32_t irq_read_i_stat(const IrqState* irq) {
    mutex_lock((Mutex*)&irq->lock); // Cast away const for mutex
    
    uint32_t value = irq->i_stat;
    
    mutex_unlock((Mutex*)&irq->lock);
    
    return value;
}

void irq_write_i_stat(IrqState* irq, uint32_t value) {
    mutex_lock(&irq->lock);
    
    // PSX behavior: Writing 0 to a bit clears (acknowledges) that IRQ
    // Writing 1 to a bit is ignored (bit stays as-is)
    // Example: writing 0xFFFE clears bit 0, writing 0x0001 is ignored
    uint16_t old_i_stat = irq->i_stat;
    irq->i_stat &= (uint16_t)value;  // Clear bits where value has 0
    
    // Log acknowledgments (rate-limited)
    static uint32_t ack_count = 0;
    if (irq->log_enabled && ++ack_count <= 20) {
        uint16_t acked_bits = old_i_stat & ~irq->i_stat;
        if (acked_bits) {
            LOG_IRQ_DEBUG("[IRQ] ACK #%u: I_STAT 0x%04x -> 0x%04x (cleared: 0x%04x)",
                         ack_count, old_i_stat, irq->i_stat, acked_bits);
        }
    }
    
    mutex_unlock(&irq->lock);
}

uint32_t irq_read_i_mask(const IrqState* irq) {
    mutex_lock((Mutex*)&irq->lock); // Cast away const for mutex
    
    uint32_t value = irq->i_mask;
    
    mutex_unlock((Mutex*)&irq->lock);
    
    return value;
}

void irq_write_i_mask(IrqState* irq, uint32_t value) {
    mutex_lock(&irq->lock);
    
    uint16_t old_mask = irq->i_mask;
    irq->i_mask = (uint16_t)value & 0x7FF; // Only 11 IRQ lines (bits 0-10)
    
    // Log mask changes
    static uint32_t mask_change_count = 0;
    if (irq->log_enabled && ++mask_change_count <= 20) {
        LOG_IRQ_DEBUG("[IRQ] I_MASK change #%u: 0x%04x -> 0x%04x",
                     mask_change_count, old_mask, irq->i_mask);
    }
    
    mutex_unlock(&irq->lock);
}

// ============================================================================
// Utility and Debug API
// ============================================================================

const char* irq_get_line_name(IrqLine line) {
    if (line < IRQ_LINE_COUNT) {
        return IRQ_LINE_NAMES[line];
    }
    return "INVALID";
}

void irq_get_stats(const IrqState* irq, IrqLine line,
                   uint32_t* out_request_count, uint32_t* out_edge_count) {
    mutex_lock((Mutex*)&irq->lock); // Cast away const for mutex
    
    if (line < IRQ_LINE_COUNT) {
        if (out_request_count) *out_request_count = irq->request_count[line];
        if (out_edge_count) *out_edge_count = irq->edge_count[line];
    } else {
        if (out_request_count) *out_request_count = 0;
        if (out_edge_count) *out_edge_count = 0;
    }
    
    mutex_unlock((Mutex*)&irq->lock);
}

void irq_set_logging(IrqState* irq, bool enabled) {
    mutex_lock(&irq->lock);
    irq->log_enabled = enabled;
    mutex_unlock(&irq->lock);
}

void irq_reset_stats(IrqState* irq) {
    mutex_lock(&irq->lock);
    
    memset(irq->request_count, 0, sizeof(irq->request_count));
    memset(irq->edge_count, 0, sizeof(irq->edge_count));
    irq->total_requests = 0;
    
    mutex_unlock(&irq->lock);
}

// ============================================================================
// State Management (DuckStation-style)
// ============================================================================

void irq_reset(IrqState* irq) {
    mutex_lock(&irq->lock);
    
    // Clear hardware registers (DuckStation-style)
    irq->i_stat = 0;
    irq->i_mask = 0;
    irq->line_state = 0;
    
    // Clear statistics
    memset(irq->request_count, 0, sizeof(irq->request_count));
    memset(irq->edge_count, 0, sizeof(irq->edge_count));
    irq->total_requests = 0;
    
    mutex_unlock(&irq->lock);
    
    LOG_IRQ_DEBUG("[IRQ] Controller reset to initial state");
}

int irq_serialize(const IrqState* irq, void* data, size_t size) {
    if (size < sizeof(IrqState)) {
        return -1; // Buffer too small
    }
    
    mutex_lock((Mutex*)&irq->lock); // Cast away const for mutex
    
    memcpy(data, irq, sizeof(IrqState));
    
    mutex_unlock((Mutex*)&irq->lock);
    
    return sizeof(IrqState);
}

int irq_deserialize(IrqState* irq, const void* data, size_t size) {
    if (size < sizeof(IrqState)) {
        return -1; // Data too small
    }
    
    mutex_lock(&irq->lock);
    
    memcpy(irq, data, sizeof(IrqState));
    
    mutex_unlock(&irq->lock);
    
    return sizeof(IrqState);
}
