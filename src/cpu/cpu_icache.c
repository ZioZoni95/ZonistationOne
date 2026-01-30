#include "cpu.h"
#include "interconnect.h"

// --- Instruction Cache ---
/**
 * @brief Fetches an instruction word from memory, checking the instruction cache first.
 * Handles cache lookup, hit/miss logic, and fetching from interconnect on miss.
 * Based on Guide Section 8.1 and 8.2 principles.
 * @param cpu Pointer to the Cpu state (containing the cache).
 * @param vaddr The virtual address of the instruction to fetch.
 * @return The 32-bit instruction word.
 */
uint32_t cpu_icache_fetch(Cpu* cpu, uint32_t vaddr) {
    // --- Cache Bypass Check ---
    // KSEG1 region (0xA0000000 - 0xBFFFFFFF) is un-cached.
    // Check the top 3 bits. If they are 101 (binary), it's KSEG1.
    if ((vaddr >> 29) == 0b101) {
        // KSEG1: Bypass cache, fetch directly from interconnect
        // printf("~ I-Cache Bypass (KSEG1 address: 0x%08x)\n", vaddr); // Optional debug
        uint32_t instruction = interconnect_load32(cpu->inter, vaddr);
        return instruction;
    }
    // TODO: Add checks for SR[IsC] (cache isolation) and SR[SwC] (swap caches)
    //       if implementing those features later. For now, assume cache is active.


    // --- Address Calculation ---
    // The cache uses physical addresses for tags and indexing.
    uint32_t paddr = mask_region(vaddr);

    // Extract cache components from physical address (based on 4KB, 4-word lines)
    // Tag:          Bits [31:12] of paddr
    // Line Index:   Bits [11:4] of paddr (determines which of the 256 lines)
    // Word Index:   Bits [3:2]  of paddr (determines which word within the line)
    //
    uint32_t tag        = paddr >> 12;
    uint32_t line_index = (paddr >> 4) & (ICACHE_NUM_LINES - 1); // Mask for 256 lines (0xFF)
    uint32_t word_index = (paddr >> 2) & (ICACHE_LINE_WORDS - 1); // Mask for 4 words (0x3)

    // Get pointer to the relevant cache line
    ICacheLine* line = &cpu->icache[line_index];

    // --- Cache Lookup ---
    if (line->tag == tag && line->valid[word_index]) {
        // Cache Hit! Tags match and the specific word is valid.
        // printf("~ I-Cache Hit:  0x%08x (Line: %u, Word: %u)\n", vaddr, line_index, word_index); // Optional debug
        return line->data[word_index];
    }

    // --- Cache Miss ---
    // printf("~ I-Cache Miss: 0x%08x (Line: %u, Word: %u)\n", vaddr, line_index, word_index); // Optional debug

    // Fetch the required block from memory.
    // According to the guide, on a miss for word N,
    // words N through 3 of that cache line are fetched from memory.
    // Words 0 through N-1 are not fetched in this operation.

    // Calculate the physical address corresponding to the start of the cache line.
    uint32_t line_paddr_start = paddr & ~((ICACHE_LINE_WORDS * 4) - 1); // Align down to 16-byte boundary (mask low 4 bits)

    // Update the tag for the cache line (this happens even on miss)
    line->tag = tag;

    // Invalidate words in the line *before* the one we are fetching,
    // as the tag has changed, making any previous data for a different tag invalid.
    for (uint32_t j = 0; j < word_index; ++j) {
        line->valid[j] = false;
    }

    // Fetch words from memory starting at the missed word's index up to the end of the line.
    for (uint32_t j = word_index; j < ICACHE_LINE_WORDS; ++j) {
        // Calculate the physical address for this word
        uint32_t fetch_paddr = line_paddr_start + (j * 4);
        // Fetch from interconnect (bypassing cache itself - interconnect doesn't call back here)
        uint32_t instruction_data = interconnect_load32(cpu->inter, fetch_paddr);
        // Store fetched data in the cache line
        line->data[j] = instruction_data;
        // Mark this word as valid
        line->valid[j] = true;
    }

    // Return the instruction data for the originally requested word index
    return line->data[word_index];
}