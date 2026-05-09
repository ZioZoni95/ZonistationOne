#include "spu.h"
#include "log.h"
#include <string.h>

/* =========================================================================
 * FIFO helpers
 * ========================================================================= */

static void fifo_reset(Spu* spu) {
    spu->fifo_read_pos = 0;
    spu->fifo_write_pos = 0;
    spu->fifo_count = 0;
}

static bool fifo_is_full(Spu* spu) {
    return spu->fifo_count >= FIFO_SIZE_HALFWORDS;
}

static bool fifo_is_empty(Spu* spu) {
    return spu->fifo_count == 0;
}

static void fifo_push(Spu* spu, uint16_t value) {
    if (fifo_is_full(spu)) return;
    spu->transfer_fifo[spu->fifo_write_pos] = value;
    spu->fifo_write_pos = (spu->fifo_write_pos + 1) % FIFO_SIZE_HALFWORDS;
    spu->fifo_count++;
}

static uint16_t fifo_pop(Spu* spu) {
    if (fifo_is_empty(spu)) return spu->transfer_fifo[spu->fifo_read_pos > 0 ? spu->fifo_read_pos - 1 : FIFO_SIZE_HALFWORDS - 1];
    uint16_t val = spu->transfer_fifo[spu->fifo_read_pos];
    spu->fifo_read_pos = (spu->fifo_read_pos + 1) % FIFO_SIZE_HALFWORDS;
    spu->fifo_count--;
    return val;
}

/* =========================================================================
 * DMA Transfer
 * ========================================================================= */

void spu_dma_write_halfwords(Spu* spu, const uint16_t* data, int count) {
    int mode = (spu->control >> 4) & 0x03;
    if (mode != TRANSFER_DMA_WRITE) return;

    spu->status |= SPU_STATUS_TRANSFER_BUSY;

    for (int i = 0; i < count; i++) {
        if (spu->transfer_addr >= SPU_RAM_SIZE) {
            spu->transfer_addr &= (SPU_RAM_SIZE - 1);
        }
        spu->ram[spu->transfer_addr / 2] = data[i];

        /* IRQ check */
        spu_check_irq(spu, spu->transfer_addr);

        spu->transfer_addr += 2;
        if (spu->transfer_addr >= SPU_RAM_SIZE) {
            spu->transfer_addr = 0;
        }
    }

    spu->transfer_addr_reg = (uint16_t)(spu->transfer_addr / 8);
    spu->status &= ~SPU_STATUS_TRANSFER_BUSY;
}

void spu_dma_read_halfwords(Spu* spu, uint16_t* data, int count) {
    int mode = (spu->control >> 4) & 0x03;
    if (mode != TRANSFER_DMA_READ) return;

    spu->status |= SPU_STATUS_TRANSFER_BUSY;

    uint16_t last_val = 0;
    for (int i = 0; i < count; i++) {
        if (spu->transfer_addr >= SPU_RAM_SIZE) {
            spu->transfer_addr &= (SPU_RAM_SIZE - 1);
        }

        if (spu->transfer_addr / 2 < (SPU_RAM_SIZE / 2)) {
            last_val = spu->ram[spu->transfer_addr / 2];
        }

        /* IRQ check */
        spu_check_irq(spu, spu->transfer_addr);

        data[i] = last_val;

        spu->transfer_addr += 2;
        if (spu->transfer_addr >= SPU_RAM_SIZE) {
            spu->transfer_addr = 0;
        }
    }

    spu->transfer_addr_reg = (uint16_t)(spu->transfer_addr / 8);
    spu->status &= ~SPU_STATUS_TRANSFER_BUSY;
}

bool spu_dma_write_request(Spu* spu) {
    int mode = (spu->control >> 4) & 0x03;
    if (mode != TRANSFER_DMA_WRITE) return false;
    return !fifo_is_full(spu);
}

bool spu_dma_read_request(Spu* spu) {
    int mode = (spu->control >> 4) & 0x03;
    if (mode != TRANSFER_DMA_READ) return false;
    return !fifo_is_empty(spu);
}

/* =========================================================================
 * Manual Transfer
 * ========================================================================= */

void spu_transfer_write(Spu* spu, uint16_t value) {
    int mode = (spu->control >> 4) & 0x03;
    if (mode != TRANSFER_MANUAL_WRITE && mode != TRANSFER_DMA_WRITE) return;

    if (spu->transfer_addr >= SPU_RAM_SIZE) {
        spu->transfer_addr &= (SPU_RAM_SIZE - 1);
    }

    spu->ram[spu->transfer_addr / 2] = value;
    spu_check_irq(spu, spu->transfer_addr);

    spu->transfer_addr += 2;
    if (spu->transfer_addr >= SPU_RAM_SIZE) {
        spu->transfer_addr = 0;
    }

    spu->transfer_addr_reg = (uint16_t)(spu->transfer_addr / 8);
}

uint16_t spu_transfer_read(Spu* spu) {
    int mode = (spu->control >> 4) & 0x03;
    if (mode != TRANSFER_DMA_READ) return 0;

    if (spu->transfer_addr >= SPU_RAM_SIZE) {
        spu->transfer_addr &= (SPU_RAM_SIZE - 1);
    }

    uint16_t val = spu->ram[spu->transfer_addr / 2];
    spu_check_irq(spu, spu->transfer_addr);

    spu->transfer_addr += 2;
    if (spu->transfer_addr >= SPU_RAM_SIZE) {
        spu->transfer_addr = 0;
    }

    spu->transfer_addr_reg = (uint16_t)(spu->transfer_addr / 8);
    return val;
}
