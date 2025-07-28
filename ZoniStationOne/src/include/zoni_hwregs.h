#ifndef ZONI_HWREGS_H
#define ZONI_HWREGS_H

#include <stdint.h>

typedef struct {
    uint32_t io_port;         // 0x1F801040
    uint32_t ram_size;        // 0x1F801060
    uint32_t interrupt_stat;  // 0x1F801070
    uint32_t interrupt_mask;  // 0x1F801074
    // Timer 0
    uint32_t timer0_counter;  // 0x1F801080
    uint32_t timer0_mode;     // 0x1F801084
    uint32_t timer0_target;   // 0x1F801088
    // Timer 1
    uint32_t timer1_counter;  // 0x1F80108C
    uint32_t timer1_mode;     // 0x1F801090
    uint32_t timer1_target;   // 0x1F801094
    // Timer 2
    uint32_t timer2_counter;  // 0x1F801098
    uint32_t timer2_mode;     // 0x1F80109C
    uint32_t timer2_target;   // 0x1F8010A0
    // DMA controller (solo i principali, puoi espandere)
    uint32_t dma_base[7];      // 0x1F801080, 0x1F801090, ..., uno per ogni canale
    uint32_t dma_block[7];     // 0x1F801084, 0x1F801094, ...
    uint32_t dma_control[7];   // 0x1F801088, 0x1F801098, ...
    uint32_t dma_pcr;          // 0x1F8010F0
    uint32_t dma_icr;          // 0x1F8010F4
} zoni_hwregs_t;

void zoni_hwregs_init(zoni_hwregs_t* hw);

#endif