#ifndef INTERRUPT_CONTROLLER_H
#define INTERRUPT_CONTROLLER_H

#include <stdint.h>
#include <stdbool.h>

#define NUM_IRQS 11

typedef enum {
    IRQ_VBLANK = 0,   // IRQ0 - VBLANK
    IRQ_GPU = 1,       // IRQ1 - GPU via GP0(1Fh)
    IRQ_CDROM = 2,     // IRQ2 - CDROM
    IRQ_DMA = 3,       // IRQ3 - DMA
    IRQ_TMR0 = 4,      // IRQ4 - TMR0 - Sysclk or Dotclk
    IRQ_TMR1 = 5,      // IRQ5 - TMR1 - Sysclk Hblank
    IRQ_TMR2 = 6,      // IRQ6 - TMR2 - Sysclk or Sysclk / 8
    IRQ_PAD = 7,       // IRQ7 - Controller and Memory Card
    IRQ_SIO = 8,       // IRQ8 - SIO
    IRQ_SPU = 9,       // IRQ9 - SPU
    IRQ_10 = 10        // IRQ10 - Lightpen
} IRQNumber;

typedef struct {
    uint32_t status;       // I_STAT - Interrupt status register
    uint32_t mask;         // I_MASK - Interrupt mask register  
    uint32_t line_state;   // Edge detection state
} InterruptController;

void interrupt_init(InterruptController* ic);
uint32_t interrupt_read_status(InterruptController* ic);
uint32_t interrupt_read_mask(InterruptController* ic);
void interrupt_write_status(InterruptController* ic, uint32_t value);
void interrupt_write_mask(InterruptController* ic, uint32_t value);
void interrupt_set_line(InterruptController* ic, IRQNumber irq, bool state);
bool interrupt_has_pending(InterruptController* ic);

#endif // INTERRUPT_CONTROLLER_H
