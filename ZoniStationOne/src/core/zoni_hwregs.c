#include "zoni_hwregs.h"

void zoni_hwregs_init(zoni_hwregs_t* hw) {
    hw->io_port = 0x0;
    hw->ram_size = 0x200000;
    hw->interrupt_stat = 0x0;
    hw->interrupt_mask = 0x0;
    hw->timer0_counter = 0;
    hw->timer0_mode = 0;
    hw->timer0_target = 0;
    hw->timer1_counter = 0;
    hw->timer1_mode = 0;
    hw->timer1_target = 0;
    hw->timer2_counter = 0;
    hw->timer2_mode = 0;
    hw->timer2_target = 0;
    for (int i = 0; i < 7; i++) {
        hw->dma_base[i] = 0;
        hw->dma_block[i] = 0;
        hw->dma_control[i] = 0;
    }
    hw->dma_pcr = 0;
    hw->dma_icr = 0;
}