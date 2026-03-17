#ifndef GPU_HELPERS_H
#define GPU_HELPERS_H

#include <stdint.h>
#include <stdbool.h>

/**
 * GPU Helper Functions — Validation & Coordinate Manipulation
 *
 * These helpers implement exact PSX GPU behavior per PSX-SPX specs:
 * https://psx-spx.consoledev.net/graphicsprocessingunitgpu/
 *
 * DuckStation reference: src/core/gpu_helpers.h
 */

/**
 * @brief Clamp a signed coordinate to valid range
 * PSX uses 11-bit signed coordinates: -1024 to 1023
 */
static inline int16_t gpu_clamp_coord(int32_t coord) {
    if (coord < -1024) return -1024;
    if (coord > 1023) return 1023;
    return (int16_t)coord;
}

/**
 * @brief Validate texture page boundaries
 * Checks if texture UV coordinates are within valid VRAM bounds
 *
 * @param page_x Base X of texture page (0-15, represents X*64)
 * @param page_y Base Y of texture page (0-1, represents Y*256)
 * @param depth Texture bit depth (0=4bit, 1=8bit, 2=15bit)
 * @param u Texture U coordinate (0-255)
 * @param v Texture V coordinate (0-255)
 * @return true if coordinates are valid, false if out of bounds
 */
bool gpu_validate_texture_coords(uint8_t page_x, uint8_t page_y, uint8_t depth,
                                  uint8_t u, uint8_t v);

/**
 * @brief Validate CLUT (palette) coordinates
 * CLUT stored at specific VRAM locations with size based on texture depth
 *
 * @param clut_packed Packed CLUT value from tpage (bits 16-31)
 * @param depth Texture bit depth (0=4bit palette 16 colors, 1=8bit palette 256 colors)
 * @return true if CLUT is within valid VRAM bounds
 */
bool gpu_validate_clut_coords(uint16_t clut_packed, uint8_t depth);

/**
 * @brief Check if vertex is within drawing area
 * Drawing area is defined by top-left (x1, y1) and bottom-right (x2, y2)
 *
 * @param x Vertex X coordinate
 * @param y Vertex Y coordinate
 * @param draw_x1 Drawing area top-left X
 * @param draw_y1 Drawing area top-left Y
 * @param draw_x2 Drawing area bottom-right X
 * @param draw_y2 Drawing area bottom-right Y
 * @return true if vertex is within drawing area
 */
static inline bool gpu_vertex_in_draw_area(int16_t x, int16_t y,
                                           uint16_t draw_x1, uint16_t draw_y1,
                                           uint16_t draw_x2, uint16_t draw_y2) {
    return (x >= draw_x1 && x < draw_x2 && y >= draw_y1 && y < draw_y2);
}

/**
 * @brief Dither RGB 24-bit to 15-bit (if dithering enabled)
 * PSX uses 4x4 Bayer dithering matrix when dithering=1
 *
 * @param r8 Red component (0-255)
 * @param g8 Green component (0-255)
 * @param b8 Blue component (0-255)
 * @param x Pixel X coordinate (for dither matrix lookup)
 * @param y Pixel Y coordinate (for dither matrix lookup)
 * @return 15-bit RGB: [B:5][G:5][R:5]
 */
uint16_t gpu_dither_rgb24_to_rgb15(uint8_t r8, uint8_t g8, uint8_t b8,
                                    uint16_t x, uint16_t y, bool dithering);

/**
 * @brief Convert 24-bit RGB to 15-bit without dithering
 * Simple truncation: r>>3, g>>3, b>>3
 */
static inline uint16_t gpu_rgb24_to_rgb15_nodither(uint8_t r8, uint8_t g8, uint8_t b8) {
    uint8_t r5 = r8 >> 3;
    uint8_t g5 = g8 >> 3;
    uint8_t b5 = b8 >> 3;
    return (b5 << 10) | (g5 << 5) | r5;
}

/**
 * @brief Extract texture page info from packed tpage value
 * tpage = [texture_depth:2][page_y:1][page_x:4][01:2]
 */
static inline void gpu_unpack_tpage(uint16_t tpage,
                                     uint8_t *page_x, uint8_t *page_y,
                                     uint8_t *depth, bool *raw_texture) {
    *page_x = (tpage >> 0) & 0x0F;      // Bits 0-3
    *page_y = (tpage >> 4) & 0x01;      // Bit 4
    *depth = (tpage >> 7) & 0x03;       // Bits 7-8
    *raw_texture = (tpage >> 15) & 0x01; // Bit 15
}

/**
 * @brief Check if polygon vertices are within the drawing area bounds
 * Useful for detecting polygons that might need scissor clipping
 *
 * @param vertices Array of [x, y] vertex pairs
 * @param vertex_count Number of vertices (typically 3 for triangles, 4 for quads)
 * @param draw_left Left boundary (inclusive)
 * @param draw_top Top boundary (inclusive)
 * @param draw_right Right boundary (inclusive)
 * @param draw_bottom Bottom boundary (inclusive)
 * @return true if all vertices are within bounds, false if any vertex is outside
 */
bool gpu_check_vertices_in_draw_area(int16_t vertices[][2], int vertex_count,
                                     uint16_t draw_left, uint16_t draw_top,
                                     uint16_t draw_right, uint16_t draw_bottom);

#endif // GPU_HELPERS_H
