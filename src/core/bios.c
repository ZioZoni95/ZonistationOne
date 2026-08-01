/* SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 ZioZoni95
 *
 * Part of ZoniStation One, a PlayStation 1 emulator.
 * See LICENSE for the full licence text and THIRD-PARTY.md for the
 * components of this project that have other authors.
 */
#include "bios.h"       // Include the corresponding header file
#include <stdio.h>      // For file operations (fopen, fread, fclose, perror, fprintf)
#include <string.h>     // For memcmp, strlen
#include "log.h"

// Example: Replace LOG_BIOS_INFO or LOG_BIOS_DEBUG for frequent memory accesses with LOG_BIOS_TRACE or wrap in a higher debug level check.
#ifdef LOG_BIOS_TRACE
#define LOG_BIOS_TRACE_ENABLED 1
#else
#define LOG_BIOS_TRACE_ENABLED 0
#endif

// Loads the BIOS ROM content from a file specified by 'path' into the Bios struct.
// Based on Guide Section 2.7 Loading the BIOS [cite: 117]
bool bios_load(Bios* bios, const char* path) {
    // Attempt to open the specified file in binary read mode ("rb").
    FILE *file = fopen(path, "rb");
    // Check if the file was opened successfully.
    if (!file) {
        LOG_BIOS_ERROR("[BIOS] Error opening BIOS file: %s", path);
        return false; // Indicate failure.
    }

    // Read data from the file directly into the 'data' buffer within the Bios struct.
    // - bios->data: destination buffer
    // - 1: size of each element to read (1 byte)
    // - BIOS_SIZE: number of elements to read (total size)
    // - file: the file stream to read from
    // fread returns the number of elements successfully read.
    size_t bytes_read = fread(bios->data, 1, BIOS_SIZE, file);

    // Close the file stream now that we are done with it.
    fclose(file);

    // Check if the number of bytes read matches the expected BIOS size.
    if (bytes_read != BIOS_SIZE) {
        LOG_BIOS_ERROR("[BIOS] Error reading BIOS file: Read %zu bytes, expected %d", bytes_read, BIOS_SIZE);
        return false; // Indicate failure.
    }

    // Optional: Verify the BIOS checksum against known values (Guide Table 3) [cite: 115]
    // Add MD5 or SHA1 checksum calculation and comparison logic here if desired.

    // Print a success message including the path and size.
    LOG_BIOS_WARN("[BIOS] BIOS loaded successfully from %s (%d bytes)", path, BIOS_SIZE);
    // bios_apply_fastboot_patch(bios);  // disabled: only helps when region check passes
    return true;
}

void bios_apply_fastboot_patch(Bios* bios) {
    // Replace BIOS shell entry point with: enable display + jr $ra
    // This skips region check and shell entirely (DuckStation PatchBIOSFastBoot Type1).
    static const uint32_t patch[] = {
        0x3C011F80,  // lui  $at, 0x1F80
        0x3C0A0300,  // lui  $t2, 0x0300
        0xAC2A1814,  // sw   $t2, 0x1814($at)  (GP1: enable display)
        0x03E00008,  // jr   $ra
        0x00000000,  // nop
    };
    // Type1B pattern: shell decompressor prolog (first 12 bytes)
    static const uint8_t pat[] = {
        0xe0,0xff,0xbd,0x27, 0x1c,0x00,0xbf,0xaf, 0x20,0x00,0xa4,0xaf
    };
    uint32_t patch_offset = 0x18000; // Type1A fallback
    for (uint32_t i = 0; i + 32 < BIOS_SIZE; i++) {
        if (memcmp(&bios->data[i], pat, sizeof(pat)) == 0) {
            patch_offset = i;
            LOG_BIOS_INFO("[BIOS] Fast boot: Type1B pattern at 0x%05x", i);
            break;
        }
    }
    if (patch_offset == 0x18000) LOG_BIOS_INFO("[BIOS] Fast boot: fallback at 0x18000");
    for (int i = 0; i < 5; i++) {
        uint32_t w = patch[i];
        bios->data[patch_offset + i*4 + 0] = (uint8_t)(w >>  0);
        bios->data[patch_offset + i*4 + 1] = (uint8_t)(w >>  8);
        bios->data[patch_offset + i*4 + 2] = (uint8_t)(w >> 16);
        bios->data[patch_offset + i*4 + 3] = (uint8_t)(w >> 24);
    }
    LOG_BIOS_INFO("[BIOS] Fast boot patch applied (region check bypassed)");
}

// Reads a 32-bit value from the loaded BIOS data at a specific 'offset'.
// Handles little-endian conversion. [cite: 124]
// Based on Guide Section 2.7 load32 example [cite: 121]
uint32_t bios_load32(Bios* bios, uint32_t offset) {
    // Basic bounds check: Ensure reading 4 bytes starting at 'offset' stays within the BIOS_SIZE.
    if (offset > BIOS_SIZE - 4) { // Check if offset + 3 would exceed bounds
         LOG_BIOS_ERROR("[BIOS] BIOS read out of bounds: offset 0x%x", offset);
         // A real emulator might trigger an exception here. For now, return 0.
         return 0; // Placeholder error value
    }

    // Read the 4 individual bytes from the data buffer at the calculated offset.
    uint32_t b0 = bios->data[offset + 0]; // Least significant byte
    uint32_t b1 = bios->data[offset + 1];
    uint32_t b2 = bios->data[offset + 2];
    uint32_t b3 = bios->data[offset + 3]; // Most significant byte

    uint32_t value = b0 | (b1 << 8) | (b2 << 16) | (b3 << 24);
    // Per-access logging is TRACE level - rate limited to every 1000th access
    static uint32_t bios_trace_count = 0;
    if (++bios_trace_count % 1000 == 0) {
}
    return value;
}

// Reads a 16-bit value from the loaded BIOS data at a specific 'offset'.
// Handles little-endian conversion. [cite: 124]
// Based on Guide Section 2.7 load16 example [cite: 121]
uint16_t bios_load16(Bios* bios, uint32_t offset) {
    uint8_t b0 = bios->data[offset];
    uint8_t b1 = bios->data[offset + 1];
    return (uint16_t)(b0 | (b1 << 8));
}

