#ifndef BIOS_H       // Include guard
#define BIOS_H

#include <stdint.h>       // For uint8_t, uint32_t
#include <stddef.h>       // For size_t type
#include <stdbool.h>      // For bool type

// Define the standard size of a PlayStation BIOS ROM. [cite: 114]
#define BIOS_SIZE (512 * 1024) // 512KB

#pragma pack(push, 1)
typedef struct {
    char id[8];            // 0x000-0x007 PS-X EXE
    char pad1[8];          // 0x008-0x00F
    uint32_t initial_pc;   // 0x010
    uint32_t initial_gp;   // 0x014
    uint32_t load_address; // 0x018
    uint32_t file_size;    // 0x01C excluding 0x800-byte header
    uint32_t unk0;         // 0x020
    uint32_t unk1;         // 0x024
    uint32_t memfill_start;// 0x028
    uint32_t memfill_size; // 0x02C
    uint32_t initial_sp_base;   // 0x030
    uint32_t initial_sp_offset;  // 0x034
    uint32_t reserved[5];  // 0x038-0x04B
    char marker[0x7B4];    // 0x04C-0x7FF
} PSEXEHeader;
#pragma pack(pop)

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

// Patch BIOS ROM in memory to skip the shell (and region check).
// DuckStation-style fast boot: replaces shell entry with display-enable + jr $ra.
void bios_apply_fastboot_patch(Bios* bios);

#endif // BIOS_H