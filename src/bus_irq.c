#include "interconnect.h"
#include <stdio.h>
#include <stdbool.h>
#include "log.h"

// --- Peripheral Interrupt Request ---
/**
 * @brief Sets the state of an interrupt line (edge-triggered like real PSX).
 * Per PSX-SPX: "interrupt request bits in I_STAT are edge-triggered"
 * Only sets I_STAT bit on 0->1 transition of the line.
 * @param inter Pointer to the Interconnect instance.
 * @param irq_line The interrupt line number (0-10).
 * @param state true = line active, false = line inactive
 */
void interconnect_set_irq_line(Interconnect* inter, uint32_t irq_line, bool state) {
    const uint32_t bit = (1u << irq_line);
    const uint32_t prev_line_state = inter->irq_line_state;

    // Update line state
    if (state) {
        inter->irq_line_state |= bit;
    } else {
        inter->irq_line_state &= ~bit;
    }

    // Edge detection: only set I_STAT on rising edge (0->1 transition)
    if (state && !(prev_line_state & bit)) {
        inter->irq_status |= bit;
        static uint32_t irq_edge_count = 0;
        if (++irq_edge_count % 100 == 0) {
            LOG_IRQ_DEBUG("[IRQ] Rising edge #%u: line=%u I_STAT=0x%04x, I_MASK=0x%04x", irq_edge_count, irq_line, inter->irq_status, inter->irq_mask);
        }
    }
}

/**
 * @brief Allows peripherals to signal an interrupt request.
 * Sets the corresponding bit in the I_STAT register (irq_status).
 * Uses edge-triggered behavior per PSX-SPX.
 * @param inter Pointer to the Interconnect instance.
 * @param irq_line The interrupt line number (0-10).
 * @param source The source of the interrupt request.
 */
void interconnect_request_irq(Interconnect* inter, uint32_t irq_line, const char* source) {
    static uint32_t irq_req_count = 0;
    if (++irq_req_count % 100 == 0) {
        LOG_IRQ_DEBUG("[IRQ] Request #%u: line=%u by %s, I_STAT=0x%04x", irq_req_count, irq_line, source, inter->irq_status);
    }
    // Pulse the line (0->1->0) to trigger edge detection
    interconnect_set_irq_line(inter, irq_line, true);
    // Line stays high until acknowledged - don't pulse back to 0 here
}

// Helper to clear/deassert an IRQ line
void interconnect_clear_irq(Interconnect* inter, uint32_t irq_line, const char* source) {
    // Deassert the line state (for edge detection on next request)
    // Note: I_STAT bit is NOT cleared here - BIOS must write to I_STAT to clear it
    (void)source;
    interconnect_set_irq_line(inter, irq_line, false);
}

void interconnect_trigger_cdrom_irq(Interconnect* inter) {
    if (!inter) {
        LOG_CDROM_ERROR("[CDROM] trigger_cdrom_irq: inter is NULL!");
        return;
    }
    interconnect_request_irq(inter, IRQ_CDROM, "CDROM");
    static uint32_t cdrom_irq_count = 0;
    if (++cdrom_irq_count <= 10 || cdrom_irq_count % 50 == 0) {
        LOG_CDROM_DEBUG("[CDROM] IRQ #%u triggered, I_STAT=0x%04x", cdrom_irq_count, inter->irq_status);
    }
}

void interconnect_debug_check_irq_status(const Interconnect* inter, const char* context) {
    (void)inter;
    (void)context;
    // stub
}
