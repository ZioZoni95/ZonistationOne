#include "spu.h"
#include "interconnect.h"
#include "log.h"

/* =========================================================================
 * IRQ9 (SPU RAM IRQ)
 * ========================================================================= */

bool spu_check_irq(Spu* spu, uint32_t address) {
    if (!(spu->control & SPU_CTRL_IRQ9_ENABLE)) return false;
    if (spu->irq9_flag) return false;

    uint32_t irq_byte_addr = (uint32_t)spu->irq_addr * 8;
    if (irq_byte_addr == address) {
        spu->irq9_flag = true;
        spu->status |= SPU_STATUS_IRQ9_FLAG;
        LOG_SPU_DEBUG("[SPU] IRQ9 triggered at address 0x%06X", address);
        return true;
    }

    return false;
}

void spu_update_irq_addr(Spu* spu) {
    /* Late IRQ check: when IRQ address changes, check all voice addresses
       and transfer address against the new IRQ boundary */
    uint32_t irq_byte_addr = (uint32_t)spu->irq_addr * 8;

    if (!(spu->control & SPU_CTRL_IRQ9_ENABLE)) {
        spu->irq9_flag = false;
        spu->status &= ~SPU_STATUS_IRQ9_FLAG;
        return;
    }

    for (int v = 0; v < NUM_VOICES; v++) {
        uint32_t voice_addr = (uint32_t)spu->voices[v].current_address * 8;
        if (voice_addr == irq_byte_addr) {
            spu->irq9_flag = true;
            spu->status |= SPU_STATUS_IRQ9_FLAG;
        }
    }

    if (spu->transfer_addr == irq_byte_addr) {
        spu->irq9_flag = true;
        spu->status |= SPU_STATUS_IRQ9_FLAG;
    }
}
