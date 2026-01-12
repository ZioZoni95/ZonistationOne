#ifndef SPU_H
#define SPU_H

#include <stdint.h>

struct Interconnect; // forward

// Simple SPU state structure (minimal implementation to satisfy BIOS register reads)
typedef struct Spu {
    // SPU has 640 bytes of IO registers (16-bit accessible)
    uint16_t regs[640/2];
} Spu;

// Initialize SPU state
void spu_init(Spu* spu);

// Read/Write helpers (16-bit, 32-bit and 8-bit helpers provided)
uint16_t spu_read16(struct Interconnect* inter, uint32_t physical_addr);
uint32_t spu_read32(struct Interconnect* inter, uint32_t physical_addr);
void spu_write16(struct Interconnect* inter, uint32_t physical_addr, uint16_t value);
void spu_write32(struct Interconnect* inter, uint32_t physical_addr, uint32_t value);
void spu_write8(struct Interconnect* inter, uint32_t physical_addr, uint8_t value);

#endif // SPU_H
