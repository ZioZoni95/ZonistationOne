/**
 * @file zoni_endian.h
 * @brief Endianness conversion utilities for ZoniStationOne
 * 
 * This file provides utilities for handling endianness conversion between
 * little-endian (MIPS R3000A native) and big-endian (used by some hardware components).
 */

#ifndef ZONI_ENDIAN_H
#define ZONI_ENDIAN_H

#include "zoni_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Byte swap 16-bit value
 * @param value 16-bit value to swap
 * @return Byte-swapped 16-bit value
 */
static inline u16 zoni_bswap16(u16 value) {
    return ((value & 0xFF) << 8) | ((value >> 8) & 0xFF);
}

/**
 * @brief Byte swap 32-bit value
 * @param value 32-bit value to swap
 * @return Byte-swapped 32-bit value
 */
static inline u32 zoni_bswap32(u32 value) {
    return ((value & 0xFF) << 24) | ((value & 0xFF00) << 8) |
           ((value & 0xFF0000) >> 8) | ((value >> 24) & 0xFF);
}

/**
 * @brief Convert little-endian 16-bit value to big-endian
 * @param le_value Little-endian value
 * @return Big-endian value
 */
static inline u16 zoni_le_to_be16(u16 le_value) {
    return zoni_bswap16(le_value);
}

/**
 * @brief Convert big-endian 16-bit value to little-endian
 * @param be_value Big-endian value
 * @return Little-endian value
 */
static inline u16 zoni_be_to_le16(u16 be_value) {
    return zoni_bswap16(be_value);
}

/**
 * @brief Convert little-endian 32-bit value to big-endian
 * @param le_value Little-endian value
 * @return Big-endian value
 */
static inline u32 zoni_le_to_be32(u32 le_value) {
    return zoni_bswap32(le_value);
}

/**
 * @brief Convert big-endian 32-bit value to little-endian
 * @param be_value Big-endian value
 * @return Little-endian value
 */
static inline u32 zoni_be_to_le32(u32 be_value) {
    return zoni_bswap32(be_value);
}

/**
 * @brief Read 16-bit value from memory as little-endian
 * @param ptr Pointer to memory location
 * @return Little-endian 16-bit value
 */
static inline u16 zoni_read_le16(const u8* ptr) {
    return ptr[0] | (ptr[1] << 8);
}

/**
 * @brief Read 32-bit value from memory as little-endian
 * @param ptr Pointer to memory location
 * @return Little-endian 32-bit value
 */
static inline u32 zoni_read_le32(const u8* ptr) {
    return ptr[0] | (ptr[1] << 8) | (ptr[2] << 16) | (ptr[3] << 24);
}

/**
 * @brief Write 16-bit value to memory as little-endian
 * @param ptr Pointer to memory location
 * @param value 16-bit value to write
 */
static inline void zoni_write_le16(u8* ptr, u16 value) {
    ptr[0] = value & 0xFF;
    ptr[1] = (value >> 8) & 0xFF;
}

/**
 * @brief Write 32-bit value to memory as little-endian
 * @param ptr Pointer to memory location
 * @param value 32-bit value to write
 */
static inline void zoni_write_le32(u8* ptr, u32 value) {
    ptr[0] = value & 0xFF;
    ptr[1] = (value >> 8) & 0xFF;
    ptr[2] = (value >> 16) & 0xFF;
    ptr[3] = (value >> 24) & 0xFF;
}

/**
 * @brief Read 16-bit value from memory as big-endian
 * @param ptr Pointer to memory location
 * @return Big-endian 16-bit value
 */
static inline u16 zoni_read_be16(const u8* ptr) {
    return (ptr[0] << 8) | ptr[1];
}

/**
 * @brief Read 32-bit value from memory as big-endian
 * @param ptr Pointer to memory location
 * @return Big-endian 32-bit value
 */
static inline u32 zoni_read_be32(const u8* ptr) {
    return (ptr[0] << 24) | (ptr[1] << 16) | (ptr[2] << 8) | ptr[3];
}

/**
 * @brief Write 16-bit value to memory as big-endian
 * @param ptr Pointer to memory location
 * @param value 16-bit value to write
 */
static inline void zoni_write_be16(u8* ptr, u16 value) {
    ptr[0] = (value >> 8) & 0xFF;
    ptr[1] = value & 0xFF;
}

/**
 * @brief Write 32-bit value to memory as big-endian
 * @param ptr Pointer to memory location
 * @param value 32-bit value to write
 */
static inline void zoni_write_be32(u8* ptr, u32 value) {
    ptr[0] = (value >> 24) & 0xFF;
    ptr[1] = (value >> 16) & 0xFF;
    ptr[2] = (value >> 8) & 0xFF;
    ptr[3] = value & 0xFF;
}

/**
 * @brief Convert array of 16-bit values from little-endian to big-endian
 * @param dest Destination array
 * @param src Source array
 * @param count Number of 16-bit values to convert
 */
static inline void zoni_convert_le16_to_be16(u16* dest, const u16* src, u32 count) {
    for (u32 i = 0; i < count; i++) {
        dest[i] = zoni_le_to_be16(src[i]);
    }
}

/**
 * @brief Convert array of 32-bit values from little-endian to big-endian
 * @param dest Destination array
 * @param src Source array
 * @param count Number of 32-bit values to convert
 */
static inline void zoni_convert_le32_to_be32(u32* dest, const u32* src, u32 count) {
    for (u32 i = 0; i < count; i++) {
        dest[i] = zoni_le_to_be32(src[i]);
    }
}

/**
 * @brief Convert array of 16-bit values from big-endian to little-endian
 * @param dest Destination array
 * @param src Source array
 * @param count Number of 16-bit values to convert
 */
static inline void zoni_convert_be16_to_le16(u16* dest, const u16* src, u32 count) {
    for (u32 i = 0; i < count; i++) {
        dest[i] = zoni_be_to_le16(src[i]);
    }
}

/**
 * @brief Convert array of 32-bit values from big-endian to little-endian
 * @param dest Destination array
 * @param src Source array
 * @param count Number of 32-bit values to convert
 */
static inline void zoni_convert_be32_to_le32(u32* dest, const u32* src, u32 count) {
    for (u32 i = 0; i < count; i++) {
        dest[i] = zoni_be_to_le32(src[i]);
    }
}

#ifdef __cplusplus
}
#endif

#endif // ZONI_ENDIAN_H 