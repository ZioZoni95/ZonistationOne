#include "timers.h"
#include <stddef.h>

void timers_init(Timers* timers, struct Interconnect* inter) { (void)timers; (void)inter; }
uint16_t timer_read16(Timers* timers, int timer_index, uint32_t offset) { (void)timers; (void)timer_index; (void)offset; return 0; }
uint32_t timer_read32(Timers* timers, int timer_index, uint32_t offset) { (void)timers; (void)timer_index; (void)offset; return 0; }
void timer_write16(Timers* timers, int timer_index, uint32_t offset, uint16_t value) { (void)timers; (void)timer_index; (void)offset; (void)value; }
void timer_write32(Timers* timers, int timer_index, uint32_t offset, uint32_t value) { (void)timers; (void)timer_index; (void)offset; (void)value; }
void timers_step(Timers* timers, uint32_t cycles) { (void)timers; (void)cycles; } 