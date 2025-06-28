#include "dma.h"
#include <stddef.h>
 
void dma_init(Dma* dma) { (void)dma; }
uint32_t dma_read(Dma* dma, uint32_t offset) { (void)dma; (void)offset; return 0; }
bool dma_write(Dma* dma, uint32_t offset, uint32_t value) { (void)dma; (void)offset; (void)value; return false; }
void dma_channel_done(DmaChannel* ch) { (void)ch; } 