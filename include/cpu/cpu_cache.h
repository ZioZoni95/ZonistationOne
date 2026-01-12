#ifndef CPU_CACHE_H
#define CPU_CACHE_H

#include <stdbool.h>
#include <stdint.h>

// Forward declaration
typedef struct Cpu Cpu;

// ============================================================
// Instruction Cache Constants
// ============================================================

#define ICACHE_NUM_LINES 256
#define ICACHE_LINE_WORDS 4
#define ICACHE_SIZE_BYTES (ICACHE_NUM_LINES * ICACHE_LINE_WORDS * 4)

// ============================================================
// Cache Line Structure
// ============================================================

typedef struct {
    uint32_t tag;
    bool valid[ICACHE_LINE_WORDS];
    uint32_t data[ICACHE_LINE_WORDS];
} ICacheLine;

// ============================================================
// Cache Operations
// ============================================================

/**
 * @brief Fetches an instruction from memory using the I-cache
 * @param cpu Pointer to CPU state
 * @param vaddr Virtual address to fetch from
 * @return 32-bit instruction word
 */
uint32_t cpu_icache_fetch(Cpu* cpu, uint32_t vaddr);

/**
 * @brief Clears/invalidates the entire instruction cache
 * @param cpu Pointer to CPU state
 */
void cpu_icache_clear(Cpu* cpu);

#endif // CPU_CACHE_H
