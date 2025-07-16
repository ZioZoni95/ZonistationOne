#include <stdint.h>
void timers_init(void* timers) { (void)timers; }
uint32_t timer_read32(void) { return 0; }
uint16_t timer_read16(void) { return 0; }
void timer_write32(uint32_t v) { (void)v; }
void timer_write16(uint16_t v) { (void)v; } 