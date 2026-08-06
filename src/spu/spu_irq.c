/* SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 ZioZoni95
 *
 * Part of ZoniStation One, a PlayStation 1 emulator.
 * See LICENSE for the full licence text and THIRD-PARTY.md for the
 * components of this project that have other authors.
 */
#include "spu.h"
#include "interconnect.h"
#include "log.h"

/* =========================================================================
 * IRQ9 (SPU RAM IRQ)
 * ========================================================================= */

bool spu_check_irq(Spu* spu, struct Interconnect* inter, uint32_t address) {
    if (!(spu->control & SPU_CTRL_IRQ9_ENABLE)) return false;
    if (spu->irq9_flag) return false;

    uint32_t irq_byte_addr = (uint32_t)spu->irq_addr * 8;
    if (irq_byte_addr == address) {
        spu->irq9_flag = true;
        spu->status |= SPU_STATUS_IRQ9_FLAG;
        LOG_SPU_DEBUG("[SPU] IRQ9 triggered at address 0x%06X", address);
        if (inter) interconnect_set_irq_line(inter, IRQ_SPU, true);
        return true;
    }

    return false;
}

void spu_update_irq_addr(Spu* spu, struct Interconnect* inter) {
    uint32_t irq_byte_addr = (uint32_t)spu->irq_addr * 8;

    if (!(spu->control & SPU_CTRL_IRQ9_ENABLE)) {
        spu->irq9_flag = false;
        spu->status &= ~SPU_STATUS_IRQ9_FLAG;
        return;
    }

    for (int v = 0; v < NUM_VOICES; v++) {
        uint32_t voice_addr = spu->voices[v].curr_addr;
        if (voice_addr == irq_byte_addr && !spu->irq9_flag) {
            spu->irq9_flag = true;
            spu->status |= SPU_STATUS_IRQ9_FLAG;
            LOG_SPU_DEBUG("[SPU] IRQ9 triggered (late check voice %d) addr 0x%06X", v, irq_byte_addr);
            if (inter) interconnect_set_irq_line(inter, IRQ_SPU, true);
        }
    }

    if (spu->transfer_addr == irq_byte_addr && !spu->irq9_flag) {
        spu->irq9_flag = true;
        spu->status |= SPU_STATUS_IRQ9_FLAG;
        LOG_SPU_DEBUG("[SPU] IRQ9 triggered (late check transfer) addr 0x%06X", irq_byte_addr);
        if (inter) interconnect_set_irq_line(inter, IRQ_SPU, true);
    }
}
