// SPDX-License-Identifier: MIT
// PlayStation 1 Emulator - Modular BIOS Type Definitions
// Based on DuckStation BIOS System Architecture
// Date: January 7, 2026

#ifndef BIOS_TYPES_H
#define BIOS_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// ============================================================================
// CONSTANTS
// ============================================================================

/// PlayStation BIOS ROM size (512KB)
#define BIOS_SIZE 524288  // 0x80000

/// BIOS base address in physical memory
#define BIOS_BASE_ADDR 0x1FC00000

/// MD5 hash size (128 bits = 16 bytes)
#define BIOS_HASH_SIZE 16

/// Maximum length for BIOS description string
#define BIOS_DESC_MAX_LEN 128

// ============================================================================
// ENUMERATIONS
// ============================================================================

/// PlayStation console regions
typedef enum {
    BIOS_REGION_AUTO = 0,     ///< Auto-detect region
    BIOS_REGION_NTSC_J = 1,   ///< Japan (NTSC)
    BIOS_REGION_NTSC_U = 2,   ///< North America (NTSC)
    BIOS_REGION_PAL = 3,      ///< Europe/Australia (PAL)
} BiosRegion;

/// Fast-boot patch types (for skipping logo animation)
typedef enum {
    BIOS_FASTBOOT_UNSUPPORTED = 0,  ///< BIOS doesn't support fast boot
    BIOS_FASTBOOT_TYPE1 = 1,        ///< Type 1 patch (PS1 BIOSes)
    BIOS_FASTBOOT_TYPE2 = 2,        ///< Type 2 patch (PS2 BIOSes)
} BiosFastBootPatch;

/// BIOS function call tables
typedef enum {
    BIOS_TABLE_A = 0xA0,  ///< Table A: File & System Functions
    BIOS_TABLE_B = 0xB0,  ///< Table B: Device & Event Functions
    BIOS_TABLE_C = 0xC0,  ///< Table C: Advanced System Functions
} BiosFunctionTable;

// ============================================================================
// STRUCTURES
// ============================================================================

/// BIOS image information (known BIOS database entry)
/// Based on DuckStation's ImageInfo structure
typedef struct {
    const char* description;           ///< Human-readable BIOS description
    BiosRegion region;                 ///< Console region
    bool region_check;                 ///< Has region check code
    BiosFastBootPatch fastboot_patch;  ///< Fast boot patch type
    uint8_t priority;                  ///< Selection priority (lower = better)
    uint8_t hash[BIOS_HASH_SIZE];      ///< MD5 hash of BIOS image
} BiosImageInfo;

/// Loaded BIOS state
typedef struct {
    uint8_t data[BIOS_SIZE];           ///< 512KB ROM data
    uint8_t hash[BIOS_HASH_SIZE];      ///< Computed MD5 hash
    const BiosImageInfo* info;         ///< Matched BIOS info (NULL if unknown)
    BiosRegion region;                 ///< Detected region
    bool verified;                     ///< Hash matched known BIOS
    bool loaded;                       ///< BIOS successfully loaded
} BiosState;

/// BIOS function call information
typedef struct {
    uint8_t table;     ///< Function table ('A', 'B', or 'C')
    uint8_t function;  ///< Function number
    const char* name;  ///< Function name
} BiosFunctionInfo;

// ============================================================================
// KNOWN BIOS DATABASE
// ============================================================================

/// Known BIOS images database (top 20 most common BIOSes)
/// Priority: Lower values = higher priority for auto-selection
/// Based on DuckStation's s_image_info_by_hash array

// Helper macro for hash initialization
#define BIOS_HASH(h0,h1,h2,h3,h4,h5,h6,h7,h8,h9,h10,h11,h12,h13,h14,h15) \
    {h0,h1,h2,h3,h4,h5,h6,h7,h8,h9,h10,h11,h12,h13,h14,h15}

// Database size
#define BIOS_KNOWN_COUNT 20

// ============================================================================
// BIOS FUNCTION TABLES
// ============================================================================

/// Table A (0xA0) - File & System Functions
#define BIOS_FUNC_A_FILE_OPEN       0x00
#define BIOS_FUNC_A_FILE_SEEK       0x01
#define BIOS_FUNC_A_FILE_READ       0x02
#define BIOS_FUNC_A_FILE_WRITE      0x03
#define BIOS_FUNC_A_FILE_CLOSE      0x04
#define BIOS_FUNC_A_IOABORT         0x05
#define BIOS_FUNC_A_FILE_GETS       0x06
#define BIOS_FUNC_A_FILE_GETC       0x07
#define BIOS_FUNC_A_FILE_PUTC       0x08
#define BIOS_FUNC_A_TODIGIT         0x0A
#define BIOS_FUNC_A_ATOF            0x0B
#define BIOS_FUNC_A_STRTOUL         0x0C
#define BIOS_FUNC_A_STRTOL          0x0D
#define BIOS_FUNC_A_ABS             0x0E
#define BIOS_FUNC_A_LABS            0x0F
#define BIOS_FUNC_A_ATOI            0x10
#define BIOS_FUNC_A_ATOL            0x11
#define BIOS_FUNC_A_ATOB            0x12
#define BIOS_FUNC_A_INDEX           0x1C
#define BIOS_FUNC_A_RINDEX          0x1D
#define BIOS_FUNC_A_STRCHR          0x1E
#define BIOS_FUNC_A_STRRCHR         0x1F
#define BIOS_FUNC_A_STRPBRK         0x20
#define BIOS_FUNC_A_STRSPN          0x21
#define BIOS_FUNC_A_STRCSPN         0x22
#define BIOS_FUNC_A_STRTOK          0x23
#define BIOS_FUNC_A_STRSTR          0x24
#define BIOS_FUNC_A_TOUPPER         0x25
#define BIOS_FUNC_A_TOLOWER         0x26
#define BIOS_FUNC_A_BCOPY           0x27
#define BIOS_FUNC_A_BZERO           0x28
#define BIOS_FUNC_A_BCMP            0x29
#define BIOS_FUNC_A_MEMCPY          0x2A
#define BIOS_FUNC_A_MEMSET          0x2B
#define BIOS_FUNC_A_MEMMOVE         0x2C
#define BIOS_FUNC_A_MEMCMP          0x2D
#define BIOS_FUNC_A_MEMCHR          0x2E
#define BIOS_FUNC_A_RAND            0x2F
#define BIOS_FUNC_A_STD_OUT_PUTCHAR 0x30
#define BIOS_FUNC_A_STD_IN_GETCHAR  0x31
#define BIOS_FUNC_A_STD_OUT_PUTS    0x32
#define BIOS_FUNC_A_STD_IN_GETS     0x33
#define BIOS_FUNC_A_PRINTF          0x3F
#define BIOS_FUNC_A_SETJMP          0xA8

/// Table B (0xB0) - Device & Event Functions
#define BIOS_FUNC_B_ALLOC_KERNEL_MEMORY  0x00
#define BIOS_FUNC_B_FREE_KERNEL_MEMORY   0x01
#define BIOS_FUNC_B_INIT_TIMER           0x02
#define BIOS_FUNC_B_GET_TIMER_MODE       0x03
#define BIOS_FUNC_B_ENABLE_TIMER_IRQ     0x04
#define BIOS_FUNC_B_DISABLE_TIMER_IRQ    0x05
#define BIOS_FUNC_B_RESTART_TIMER        0x06
#define BIOS_FUNC_B_UNDELIVER_EVENT      0x07
#define BIOS_FUNC_B_OPEN_EVENT           0x08
#define BIOS_FUNC_B_CLOSE_EVENT          0x09
#define BIOS_FUNC_B_WAIT_EVENT           0x0A
#define BIOS_FUNC_B_TEST_EVENT           0x0B
#define BIOS_FUNC_B_ENABLE_EVENT         0x0C
#define BIOS_FUNC_B_DISABLE_EVENT        0x0D
#define BIOS_FUNC_B_OPEN_TH              0x0E
#define BIOS_FUNC_B_CLOSE_TH             0x0F
#define BIOS_FUNC_B_CHANGE_TH            0x10
#define BIOS_FUNC_B_DELIVER_EVENT        0x32

/// Table C (0xC0) - Advanced System Functions
#define BIOS_FUNC_C_ENQUEUECDIRQ        0x02
#define BIOS_FUNC_C_DEQUEUECDIRQ        0x03
#define BIOS_FUNC_C_ENQUEUETIMERORQ     0x00
#define BIOS_FUNC_C_DEQUEUETIMERORQ     0x01
#define BIOS_FUNC_C_ENQUEUESUBFUNCIRQ   0x04
#define BIOS_FUNC_C_DEQUEUESUBFUNCIRQ   0x05

// ============================================================================
// INLINE HELPER FUNCTIONS
// ============================================================================

/// Check if two MD5 hashes are equal
/// Complexity: O(1)
static inline bool bios_hash_equal(const uint8_t hash1[BIOS_HASH_SIZE], 
                                   const uint8_t hash2[BIOS_HASH_SIZE]) {
    for (int i = 0; i < BIOS_HASH_SIZE; i++) {
        if (hash1[i] != hash2[i]) return false;
    }
    return true;
}

/// Get region name string
/// Complexity: O(1)
static inline const char* bios_region_name(BiosRegion region) {
    switch (region) {
        case BIOS_REGION_NTSC_J: return "NTSC-J";
        case BIOS_REGION_NTSC_U: return "NTSC-U";
        case BIOS_REGION_PAL: return "PAL";
        case BIOS_REGION_AUTO: return "Auto";
        default: return "Unknown";
    }
}

/// Get function table name
/// Complexity: O(1)
static inline const char* bios_table_name(uint8_t table) {
    switch (table) {
        case BIOS_TABLE_A: return "A";
        case BIOS_TABLE_B: return "B";
        case BIOS_TABLE_C: return "C";
        default: return "?";
    }
}

/// Check if offset is within BIOS bounds
/// Complexity: O(1)
static inline bool bios_offset_valid(uint32_t offset, uint32_t size) {
    return (offset + size <= BIOS_SIZE);
}

#endif // BIOS_TYPES_H
