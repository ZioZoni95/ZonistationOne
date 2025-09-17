#ifndef PSX_IRQ_H
#define PSX_IRQ_H

#include "psx_types.h"

// PSX-SPX: Interrupt Controller Implementation
// Following guide.tex structure with PSX-SPX interrupt specifications

// PSX-SPX: Interrupt Controller Registers
#define IRQ_I_STAT      0x1F801070  // Interrupt Status
#define IRQ_I_MASK      0x1F801074  // Interrupt Mask

// PSX-SPX: Interrupt Sources (bit positions)
#define IRQ_VBLANK      0   // VBlank (GPU)
#define IRQ_GPU         1   // GPU Finish
#define IRQ_CDROM       2   // CD-ROM
#define IRQ_DMA         3   // DMA
#define IRQ_TIMER0      4   // Timer 0
#define IRQ_TIMER1      5   // Timer 1  
#define IRQ_TIMER2      6   // Timer 2
#define IRQ_PAD_CARD    7   // Controller/Memory Card
#define IRQ_SIO         8   // Serial I/O
#define IRQ_SPU         9   // Sound
#define IRQ_PIO         10  // Lightpen/IRQ10

typedef struct {
    u32 i_stat;         // Interrupt status register
    u32 i_mask;         // Interrupt mask register
    
    bool irq_lines[11]; // Individual IRQ line states
} psx_irq_t;

// IRQ interface functions
void irq_init(void);
void irq_reset(void);
void irq_step(void);

// Register access
u32 irq_read32(u32 addr);
void irq_write32(u32 addr, u32 value);

// IRQ triggering
void irq_trigger(int irq_num);
void irq_clear(int irq_num);
bool irq_pending(void);

#endif // PSX_IRQ_H