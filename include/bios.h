#ifndef BIOS_H       // Include guard
#define BIOS_H

#include <stdint.h>       // For uint8_t, uint32_t
#include <stddef.h>       // For size_t type
#include <stdbool.h>      // For bool type

// Define the standard size of a PlayStation BIOS ROM. [cite: 114]
#define BIOS_SIZE (512 * 1024) // 512KB

// Structure to hold the BIOS data in memory.
typedef struct {
    // A buffer large enough to hold the entire BIOS content.
    uint8_t data[BIOS_SIZE];
} Bios;

// Loads the BIOS ROM content from a file specified by 'path' into the Bios struct.
// Returns true on success, false on failure (e.g., file not found, wrong size).
// Based on Guide Section 2.7 Loading the BIOS [cite: 117]
bool bios_load(Bios* bios, const char* path);

// Reads a 32-bit value from the loaded BIOS data at a specific 'offset'.
// Handles little-endian conversion required by the MIPS architecture.
// Based on Guide Section 2.7 load32 example [cite: 121]
uint32_t bios_load32(Bios* bios, uint32_t offset);

// Reads a 16-bit value from the loaded BIOS data at a specific 'offset'.
// Handles little-endian conversion required by the MIPS architecture.
// Based on Guide Section 2.7 load16 example [cite: 121]
uint16_t bios_load16(Bios* bios, uint32_t offset);

// Scans the BIOS ROM for the bootstrap string block ("PS-X Realtime Kernel" etc.)
// and prints each string as a [BIOS TTY] line to stderr.
// Call once after bios_load() succeeds.
void bios_print_bootstrap_strings(const Bios* bios);

// Dumps ALL TCRF-documented hidden string blocks: Bootstrap/Kernel, PIO Shell,
// Control PAD driver, Standard Libraries, and CD debug strings.
// Pass --bios-strings on the command line to invoke this instead of the boot-only scan.
void bios_print_all_hidden_strings(const Bios* bios);

#endif // BIOS_H