#include "spu.h"
#include "interconnect.h"
#include "log.h"
#include <string.h>

// Minimal SPU implementation: expose register array and basic read/write
// behavior so BIOS reads to 1F801C00 etc. return deterministic values.

void spu_init(Spu* spu) {
    if (!spu) return;
    memset(spu->regs, 0, sizeof(spu->regs));
    LOG_INTERCONNECT_DEBUG("[SPU] SPU: initialized (MMIO %08x-%08x)", SPU_START, SPU_END);
}

static inline int spu_offset_for_addr(uint32_t physical_addr) {
    if (physical_addr < SPU_START || physical_addr > SPU_END) return -1;
    return (int)(physical_addr - SPU_START);
}

uint16_t spu_read16(struct Interconnect* inter, uint32_t physical_addr) {
    (void)inter;
    int off = spu_offset_for_addr(physical_addr);
    if (off < 0) return 0;
    // SPU is 16-bit accessible; addresses are byte offsets
    int idx = off / 2;
    if (idx < 0 || idx >= (int)(sizeof(((Spu*)0)->regs)/2)) return 0;

    // Simple per-register access counters for rate-limited logging
    static uint32_t access_counts[sizeof(((Spu*)0)->regs)/2];
    access_counts[idx]++;
    uint16_t val = ((Spu*)&inter->spu)->regs[idx];

    uint32_t c = access_counts[idx];
    if (c <= 5 || (c % 100) == 0) {
}

    return val;
}

uint32_t spu_read32(struct Interconnect* inter, uint32_t physical_addr) {
    // Combine two 16-bit reads (little endian)
    uint16_t lo = spu_read16(inter, physical_addr);
    uint16_t hi = spu_read16(inter, physical_addr + 2);
    return (uint32_t)lo | ((uint32_t)hi << 16);
}

void spu_write16(struct Interconnect* inter, uint32_t physical_addr, uint16_t value) {
    int off = spu_offset_for_addr(physical_addr);
    if (off < 0) return;
    int idx = off / 2;
    if (idx < 0 || idx >= (int)(sizeof(((Spu*)0)->regs)/2)) return;
    // Track previous value for logging
    uint16_t prev = ((Spu*)&inter->spu)->regs[idx];
    ((Spu*)&inter->spu)->regs[idx] = value;

    static uint32_t write_counts[sizeof(((Spu*)0)->regs)/2];
    write_counts[idx]++;
    uint32_t c = write_counts[idx];
    if (c <= 5 || (c % 100) == 0) {
}
}

void spu_write32(struct Interconnect* inter, uint32_t physical_addr, uint32_t value) {
    // Split into two 16-bit writes
    spu_write16(inter, physical_addr, (uint16_t)(value & 0xFFFF));
    spu_write16(inter, physical_addr + 2, (uint16_t)((value >> 16) & 0xFFFF));
}

void spu_write8(struct Interconnect* inter, uint32_t physical_addr, uint8_t value) {
    // 8-bit writes map to 16-bit write on even addresses; odd addresses ignored
    if (physical_addr & 1) return; // ignore odd writes
    uint16_t prev = spu_read16(inter, physical_addr);
    // store low byte
    uint16_t newv = (prev & 0xFF00) | value;
    spu_write16(inter, physical_addr, newv);
}
