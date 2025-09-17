#include "../include/psx_irq.h"
#include <stdio.h>
#include <string.h>

// PSX-SPX: Interrupt Controller implementation (skeleton)
static psx_irq_t irq_controller;

void irq_init(void) {
    memset(&irq_controller, 0, sizeof(irq_controller));
    irq_reset();
    printf("[IRQ] Interrupt controller initialized\n");
}

void irq_reset(void) {
    // PSX-SPX: Reset interrupt controller
    irq_controller.i_stat = 0;
    irq_controller.i_mask = 0;
    
    for (int i = 0; i < 11; i++) {
        irq_controller.irq_lines[i] = false;
    }
    
    printf("[IRQ] Interrupt controller reset\n");
}

void irq_step(void) {
    // TODO: Handle interrupt processing timing
}

u32 irq_read32(u32 addr) {
    switch (addr) {
        case IRQ_I_STAT:
            printf("[IRQ] I_STAT read = 0x%08X\n", irq_controller.i_stat);
            return irq_controller.i_stat;
            
        case IRQ_I_MASK:
            printf("[IRQ] I_MASK read = 0x%08X\n", irq_controller.i_mask);
            return irq_controller.i_mask;
            
        default:
            printf("[IRQ] ERROR: Unmapped read32 at 0x%08X\n", addr);
            return 0;
    }
}

void irq_write32(u32 addr, u32 value) {
    switch (addr) {
        case IRQ_I_STAT:
            // PSX-SPX: Writing 1 clears interrupt bits
            irq_controller.i_stat &= ~value;
            printf("[IRQ] I_STAT write = 0x%08X (clear bits), new value = 0x%08X\n", 
                   value, irq_controller.i_stat);
            break;
            
        case IRQ_I_MASK:
            irq_controller.i_mask = value;
            printf("[IRQ] I_MASK write = 0x%08X\n", value);
            break;
            
        default:
            printf("[IRQ] ERROR: Unmapped write32 at 0x%08X = 0x%08X\n", addr, value);
            break;
    }
}

void irq_trigger(int irq_num) {
    if (irq_num < 0 || irq_num >= 11) return;
    
    irq_controller.irq_lines[irq_num] = true;
    irq_controller.i_stat |= (1 << irq_num);
    
    printf("[IRQ] IRQ %d triggered\n", irq_num);
    
    // TODO: Signal CPU interrupt if masked
}

void irq_clear(int irq_num) {
    if (irq_num < 0 || irq_num >= 11) return;
    
    irq_controller.irq_lines[irq_num] = false;
    irq_controller.i_stat &= ~(1 << irq_num);
    
    printf("[IRQ] IRQ %d cleared\n", irq_num);
}

bool irq_pending(void) {
    // Check if any enabled interrupt is pending
    return (irq_controller.i_stat & irq_controller.i_mask) != 0;
}