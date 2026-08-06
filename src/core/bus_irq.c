/* SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 ZioZoni95
 *
 * Part of ZoniStation One, a PlayStation 1 emulator.
 * See LICENSE for the full licence text and THIRD-PARTY.md for the
 * components of this project that have other authors.
 */
#include "interconnect.h"
#include <stdio.h>
#include <stdbool.h>
#include "log.h"

static const char* const s_irq_names[] = {
    "VBLANK", "GPU", "CDROM", "DMA", "TMR0", "TMR1", "TMR2", "PAD", "SIO", "SPU", "IRQ10"
};

void interconnect_set_irq_line(Interconnect* inter, uint32_t irq_line, bool state) {
    const uint32_t bit = (1u << irq_line);
    const uint32_t prev_line_state = inter->irq_line_state;
    inter->irq_line_state = (inter->irq_line_state & ~bit) | (state ? bit : 0u);
    if (inter->irq_line_state == prev_line_state)
        return;

    const char* name = (irq_line < 11) ? s_irq_names[irq_line] : "UNKNOWN";
    if (!(prev_line_state & bit) && state) {
        inter->irq_status |= bit;
        LOG_IRQ_DEBUG("%s IRQ triggered", name);
    } else if ((prev_line_state & bit) && !state) {
        LOG_IRQ_DEBUG("%s IRQ line inactive", name);
    }
}

void interconnect_request_irq(Interconnect* inter, uint32_t irq_line, const char* source) {
    (void)source;
    interconnect_set_irq_line(inter, irq_line, true);
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
        LOG_CDROM_ERROR("[INTERCONNECT] trigger_cdrom_irq: inter is NULL!");
        return;
    }
    // cdrom.c only calls this after its own interrupt_flag==0 gate confirms a fresh,
    // distinct interrupt cause (cdrom_command_event_tick/cdrom_drive_event_tick/
    // cdrom_second_response_event_tick all reschedule-and-wait otherwise) — so this is
    // always a genuine new edge at the device level, regardless of whether the CPU has
    // separately gotten around to acking the previous one in I_STAT yet. Force the line
    // low-then-high so the generic edge-detector in interconnect_set_irq_line (which
    // otherwise requires I_STAT to have been acked first) can't swallow it — matching
    // real hardware, where the IRQ2 line is driven directly by the device's own
    // interrupt-cause register, not a separate CPU-side latch.
    interconnect_clear_irq(inter, IRQ_CDROM, "CDROM");
    interconnect_request_irq(inter, IRQ_CDROM, "CDROM");
}
