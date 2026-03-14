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
        LOG_BIOS_ERROR("Error opening BIOS file: %s", path);
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
        LOG_BIOS_ERROR("Error reading BIOS file: Read %zu bytes, expected %d", bytes_read, BIOS_SIZE);
        return false; // Indicate failure.
    }

    // Optional: Verify the BIOS checksum against known values (Guide Table 3) [cite: 115]
    // Add MD5 or SHA1 checksum calculation and comparison logic here if desired.

    // Print a success message including the path and size.
    LOG_BIOS_WARN("BIOS loaded successfully from %s (%d bytes)", path, BIOS_SIZE);
    // Return true to indicate success.
    return true;
}

// Reads a 32-bit value from the loaded BIOS data at a specific 'offset'.
// Handles little-endian conversion. [cite: 124]
// Based on Guide Section 2.7 load32 example [cite: 121]
uint32_t bios_load32(Bios* bios, uint32_t offset) {
    // Basic bounds check: Ensure reading 4 bytes starting at 'offset' stays within the BIOS_SIZE.
    if (offset > BIOS_SIZE - 4) { // Check if offset + 3 would exceed bounds
         LOG_BIOS_ERROR("BIOS read out of bounds: offset 0x%x\n", offset);
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
        LOG_BIOS_TRACE("bios_load32: #%u offset=0x%X value=0x%08X", bios_trace_count, offset, value);
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

// Walk forward from 'start' through consecutive null-terminated strings.
// Stops after 8+ consecutive non-printable/non-whitespace bytes.
// Each string is flushed as a plain line to stderr.
static void print_strings_at_offset(const Bios* bios, uint32_t start) {
    uint32_t pos = start;
    char line[256];
    int  line_len = 0;
    int  null_run = 0;

    while (pos < BIOS_SIZE && null_run < 8) {
        uint8_t b = bios->data[pos++];
        if (b == 0) {
            if (line_len > 0) {
                line[line_len] = '\0';
                fprintf(stderr, "%s\n", line);
                line_len = 0;
            }
            null_run++;
        } else if (b == '\n' || b == '\r') {
            if (line_len > 0) {
                line[line_len] = '\0';
                fprintf(stderr, "%s\n", line);
                line_len = 0;
            }
            null_run = 0;
        } else if (b >= 0x20 && b < 0x7F) {
            if (line_len < (int)(sizeof(line) - 1))
                line[line_len++] = (char)b;
            null_run = 0;
        } else {
            if (line_len > 0) {
                line[line_len] = '\0';
                fprintf(stderr, "%s\n", line);
                line_len = 0;
            }
            null_run++;
        }
    }
}

// Scans the BIOS ROM for the bootstrap string block starting with "PS-X Realtime
// Kernel" and outputs each null-terminated string as a line to stderr.
// On real hardware + devkit BIOS these are printed to the DUART debug port; on
// retail BIOS they live silently in ROM.  This is the "hidden text".
void bios_print_bootstrap_strings(const Bios* bios) {
    static const char marker[] = "PS-X Realtime Kernel";
    const uint32_t marker_len  = (uint32_t)strlen(marker);

    uint32_t start = 0;
    bool found = false;
    for (uint32_t i = 0; i + marker_len < BIOS_SIZE; i++) {
        if (memcmp(&bios->data[i], marker, marker_len) == 0) {
            start = (i > 0 && bios->data[i - 1] == '\n') ? i - 1 : i;
            found = true;
            break;
        }
    }
    if (!found) return;

    print_strings_at_offset(bios, start);
    fprintf(stderr, "\n");
}

// Dumps all TCRF-documented hidden string blocks from the BIOS ROM to stderr.
// Includes the bootstrap/kernel block plus PIO Shell, Control PAD driver,
// Standard Libraries, and CD debug strings (SCPH-1001 offsets).
void bios_print_all_hidden_strings(const Bios* bios) {
    static const struct {
        uint32_t    offset;
        const char* label;
    } blocks[] = {
        { 0x00000,  "Bootstrap/Kernel" }, // searched via marker below
        { 0x0E288,  "PIO Shell"        },
        { 0x16D34,  "Control PAD"      },
        { 0x61160,  "Std Libraries"    },
        { 0x65E48,  "CD Debug"         },
    };
    static const int  block_count = (int)(sizeof(blocks) / sizeof(blocks[0]));

    // Block 0: use marker search (same logic as bios_print_bootstrap_strings)
    {
        static const char marker[] = "PS-X Realtime Kernel";
        const uint32_t marker_len  = (uint32_t)strlen(marker);
        uint32_t start = 0;
        bool found = false;
        for (uint32_t i = 0; i + marker_len < BIOS_SIZE; i++) {
            if (memcmp(&bios->data[i], marker, marker_len) == 0) {
                start = (i > 0 && bios->data[i - 1] == '\n') ? i - 1 : i;
                found = true;
                break;
            }
        }
        if (found) {
            fprintf(stderr, "=== %s ===\n", blocks[0].label);
            print_strings_at_offset(bios, start);
            fprintf(stderr, "\n");
        }
    }

    // Remaining blocks: fixed ROM offsets
    for (int b = 1; b < block_count; b++) {
        uint32_t off = blocks[b].offset;
        if (off >= BIOS_SIZE) continue;
        fprintf(stderr, "=== %s ===\n", blocks[b].label);
        print_strings_at_offset(bios, off);
        fprintf(stderr, "\n");
    }
}