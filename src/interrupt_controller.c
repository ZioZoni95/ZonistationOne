#include "interrupt_controller.h"
#include "cpu.h"

#define IRQ_WRITE_MASK ((1u << NUM_IRQS) - 1)

void interrupt_init(InterruptController* ic) {
    ic->status = 0;
    ic->mask = 0;
    ic->line_state = 0;
}

uint32_t interrupt_read_status(InterruptController* ic) {
    return ic->status;
}

uint32_t interrupt_read_mask(InterruptController* ic) {
    return ic->mask;
}

void interrupt_write_status(InterruptController* ic, uint32_t value) {
    ic->status = ic->status & (value & IRQ_WRITE_MASK);
}

void interrupt_write_mask(InterruptController* ic, uint32_t value) {
    ic->mask = value & IRQ_WRITE_MASK;
}

void interrupt_set_line(InterruptController* ic, IRQNumber irq, bool state) {
    uint32_t bit = 1u << irq;
    uint32_t prev_state = ic->line_state;
    
    if (state) {
        ic->line_state |= bit;
    } else {
        ic->line_state &= ~bit;
    }
    
    if ((ic->line_state & bit) != (prev_state & bit)) {
        if (state) {
            ic->status |= bit;
        } else {
            ic->status &= ~bit;
        }
    }
}

bool interrupt_has_pending(InterruptController* ic) {
    return (ic->status & ic->mask) != 0;
}
