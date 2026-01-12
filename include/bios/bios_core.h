// SPDX-License-Identifier: MIT
// PlayStation 1 Emulator - Modular BIOS Core API
// Based on DuckStation BIOS System Architecture
// Date: January 7, 2026

#ifndef BIOS_CORE_H
#define BIOS_CORE_H

#include "bios_types.h"
#include <stdint.h>
#include <stdbool.h>

// ============================================================================
// INITIALIZATION & LOADING
// ============================================================================

/**
 * @brief Initialize BIOS state to default values
 * 
 * Sets all fields to zero/NULL. Must be called before using BiosState.
 * 
 * @param bios Pointer to BIOS state structure
 * @complexity O(1)
 */
void bios_init(BiosState* bios);

/**
 * @brief Load BIOS ROM from file
 * 
 * Reads 512KB BIOS file, computes MD5 hash, and attempts to match
 * against known BIOS database for verification.
 * 
 * @param bios Pointer to BIOS state structure
 * @param path Path to BIOS ROM file (e.g., "roms/SCPH1001.BIN")
 * @return true on success, false on error
 * @complexity O(n) where n = BIOS_SIZE for MD5 computation
 */
bool bios_load(BiosState* bios, const char* path);

/**
 * @brief Load BIOS and verify against known database
 * 
 * Same as bios_load() but logs verification status and warnings
 * for unknown BIOS images.
 * 
 * @param bios Pointer to BIOS state structure
 * @param path Path to BIOS ROM file
 * @return true on success, false on error
 * @complexity O(n) where n = BIOS_SIZE
 */
bool bios_load_and_verify(BiosState* bios, const char* path);

// ============================================================================
// MEMORY ACCESS
// ============================================================================

/**
 * @brief Read 32-bit value from BIOS at offset
 * 
 * Reads 4 bytes in little-endian format. Returns 0 on out-of-bounds.
 * 
 * @param bios Pointer to BIOS state structure
 * @param offset Byte offset into BIOS (0 to BIOS_SIZE-4)
 * @return 32-bit value in host byte order
 * @complexity O(1)
 */
uint32_t bios_load32(const BiosState* bios, uint32_t offset);

/**
 * @brief Read 16-bit value from BIOS at offset
 * 
 * Reads 2 bytes in little-endian format. Returns 0 on out-of-bounds.
 * 
 * @param bios Pointer to BIOS state structure
 * @param offset Byte offset into BIOS (0 to BIOS_SIZE-2)
 * @return 16-bit value in host byte order
 * @complexity O(1)
 */
uint16_t bios_load16(const BiosState* bios, uint32_t offset);

/**
 * @brief Read 8-bit value from BIOS at offset
 * 
 * Reads 1 byte. Returns 0 on out-of-bounds.
 * 
 * @param bios Pointer to BIOS state structure
 * @param offset Byte offset into BIOS (0 to BIOS_SIZE-1)
 * @return 8-bit value
 * @complexity O(1)
 */
uint8_t bios_load8(const BiosState* bios, uint32_t offset);

// ============================================================================
// HASH & VERIFICATION
// ============================================================================

/**
 * @brief Compute MD5 hash of BIOS data
 * 
 * Calculates MD5 hash and stores in bios->hash. Called automatically
 * by bios_load().
 * 
 * @param bios Pointer to BIOS state structure
 * @complexity O(n) where n = BIOS_SIZE
 */
void bios_compute_hash(BiosState* bios);

/**
 * @brief Find BIOS info by MD5 hash
 * 
 * Searches known BIOS database for matching hash.
 * 
 * @param hash MD5 hash to search for
 * @return Pointer to BiosImageInfo if found, NULL otherwise
 * @complexity O(m) where m = BIOS_KNOWN_COUNT (~20)
 */
const BiosImageInfo* bios_find_info_by_hash(const uint8_t hash[BIOS_HASH_SIZE]);

/**
 * @brief Get BIOS description string
 * 
 * Returns human-readable description like "SCPH-1001 (v2.2 12-04-95 A)"
 * or "Unknown BIOS" if not verified.
 * 
 * @param bios Pointer to BIOS state structure
 * @return Description string (never NULL)
 * @complexity O(1)
 */
const char* bios_get_description(const BiosState* bios);

/**
 * @brief Check if BIOS is verified (known)
 * 
 * @param bios Pointer to BIOS state structure
 * @return true if BIOS matches known database, false otherwise
 * @complexity O(1)
 */
bool bios_is_verified(const BiosState* bios);

/**
 * @brief Get BIOS region
 * 
 * @param bios Pointer to BIOS state structure
 * @return Region code (NTSC-J, NTSC-U, PAL, or Auto)
 * @complexity O(1)
 */
BiosRegion bios_get_region(const BiosState* bios);

/**
 * @brief Check if BIOS supports fast boot
 * 
 * @param bios Pointer to BIOS state structure
 * @return true if fast boot patching is supported
 * @complexity O(1)
 */
bool bios_supports_fastboot(const BiosState* bios);

// ============================================================================
// BIOS FUNCTION CALL INTERCEPTION
// ============================================================================

/**
 * @brief Decode BIOS function call
 * 
 * Looks up function name from table (A/B/C) and function number.
 * 
 * @param table Function table (0xA0, 0xB0, or 0xC0)
 * @param function Function number
 * @return Pointer to BiosFunctionInfo if known, NULL otherwise
 * @complexity O(1) - Direct array lookup
 */
const BiosFunctionInfo* bios_decode_function(uint8_t table, uint8_t function);

/**
 * @brief Log BIOS function call
 * 
 * Logs function call with PC, table, function number, and name.
 * This replaces the BIOS call logging previously in the CPU module.
 * 
 * Format: "[BIOS] @PC=0x12345678: A(3Fh) = printf() [RA=0x80001234]"
 * 
 * @param pc Program counter where call occurred
 * @param table Function table (0xA0, 0xB0, or 0xC0)
 * @param function Function number
 * @param ra Return address (for debugging)
 * @complexity O(1)
 */
void bios_log_function_call(uint32_t pc, uint8_t table, uint8_t function, uint32_t ra);

/**
 * @brief Get function name string
 * 
 * Returns function name or "Unknown" if not in database.
 * 
 * @param table Function table (0xA0, 0xB0, or 0xC0)
 * @param function Function number
 * @return Function name string (never NULL)
 * @complexity O(1)
 */
const char* bios_get_function_name(uint8_t table, uint8_t function);

// ============================================================================
// UTILITY FUNCTIONS
// ============================================================================

/**
 * @brief Format MD5 hash as hex string
 * 
 * Converts 16-byte hash to 32-character hex string.
 * 
 * @param hash MD5 hash bytes
 * @param buffer Output buffer (must be at least 33 bytes)
 * @complexity O(1)
 */
void bios_hash_to_string(const uint8_t hash[BIOS_HASH_SIZE], char buffer[33]);

/**
 * @brief Get BIOS statistics
 * 
 * Returns information about loaded BIOS for debugging.
 * 
 * @param bios Pointer to BIOS state structure
 * @param[out] description BIOS description
 * @param[out] region Region name
 * @param[out] verified Verification status
 * @param[out] hash_str MD5 hash as string (33 bytes)
 * @complexity O(1)
 */
void bios_get_stats(const BiosState* bios, 
                    const char** description,
                    const char** region,
                    bool* verified,
                    char hash_str[33]);

#endif // BIOS_CORE_H
