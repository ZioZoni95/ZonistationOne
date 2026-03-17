#include "gpu_helpers.h"
#include "log.h"

/**
 * Dithering LUT — 4x4 Bayer matrix for PSX dithering
 * Values from PSX-SPX spec: 15-bit dithering uses this pattern
 * Each entry is per-channel offset (-7 to +7 range typically)
 */
static const int8_t dither_lut[4][4] = {
    {-4, +0, -3, +1},
    {+2, -2, +3, -1},
    {-3, +1, -4, +0},
    {+3, -1, +2, -2}
};

bool gpu_validate_texture_coords(uint8_t page_x, uint8_t page_y, uint8_t depth,
                                  uint8_t u, uint8_t v) {
    // Calculate texture page boundaries in VRAM
    // Page X: page_x * 64 pixels
    // Page Y: page_y * 256 pixels (only 2 pages: 0 and 1)

    (void)page_x;   // page_start_x not used yet
    (void)page_y;   // page_start_y not used yet

    uint16_t page_width, page_height = 256;

    // Texture depth determines page width:
    // 0 = 4-bit (16 colors): 256 pixels wide (16 4-bit pixels = 64 bytes * 4)
    // 1 = 8-bit (256 colors): 128 pixels wide (128 8-bit pixels = 128 bytes)
    // 2 = 15-bit (direct): 64 pixels wide (64 15-bit pixels = 128 bytes)
    switch (depth) {
        case 0: page_width = 256; break;
        case 1: page_width = 128; break;
        case 2: page_width = 64; break;
        default: return false;
    }

    // Texture coordinates are 8-bit, so 0-255 is max valid range
    // Check if U,V are within the page boundaries
    if (u >= page_width || v >= page_height) {
        LOG_GPU_WARN("Texture coordinate out of bounds: U=%d (max %d), V=%d (max %d)",
                     u, page_width - 1, v, page_height - 1);
        return false;
    }

    return true;
}

bool gpu_validate_clut_coords(uint16_t clut_packed, uint8_t depth) {
    // Extract CLUT coordinates from packed value
    uint16_t clut_x = (clut_packed & 0x3F) * 16;        // X: bits 0-5, multiply by 16
    uint16_t clut_y = (clut_packed >> 6) & 0x1FF;       // Y: bits 6-14

    // CLUT is stored in VRAM starting at (clut_x, clut_y)
    // Size depends on texture depth:
    // 4-bit: 16 colors = 16 words = 16 pixels wide, 1 pixel high
    // 8-bit: 256 colors = 256 words = 16 pixels wide, 16 pixels high

    uint16_t clut_width, clut_height;
    if (depth == 0) {
        // 4-bit: 16 colors
        clut_width = 16;
        clut_height = 1;
    } else if (depth == 1) {
        // 8-bit: 256 colors
        clut_width = 16;
        clut_height = 16;
    } else {
        // 15-bit direct color, no CLUT needed (but validate anyway)
        return true;
    }

    // Check bounds: VRAM is 1024x512
    if (clut_x + clut_width > 1024 || clut_y + clut_height > 512) {
        LOG_GPU_WARN("CLUT out of VRAM bounds: X=%d..%d, Y=%d..%d (VRAM: 1024x512)",
                     clut_x, clut_x + clut_width - 1,
                     clut_y, clut_y + clut_height - 1);
        return false;
    }

    return true;
}

uint16_t gpu_dither_rgb24_to_rgb15(uint8_t r8, uint8_t g8, uint8_t b8,
                                    uint16_t x, uint16_t y, bool dithering) {
    if (!dithering) {
        return gpu_rgb24_to_rgb15_nodither(r8, g8, b8);
    }

    // Apply dithering using 4x4 Bayer matrix
    int8_t dither_val = dither_lut[y & 3][x & 3];

    // Apply dither offset and clamp to 5-bit range
    int16_t r5_adj = ((r8 >> 3) + dither_val);
    int16_t g5_adj = ((g8 >> 3) + dither_val);
    int16_t b5_adj = ((b8 >> 3) + dither_val);

    // Clamp to 5-bit range (0-31)
    if (r5_adj < 0) r5_adj = 0;
    if (r5_adj > 31) r5_adj = 31;
    if (g5_adj < 0) g5_adj = 0;
    if (g5_adj > 31) g5_adj = 31;
    if (b5_adj < 0) b5_adj = 0;
    if (b5_adj > 31) b5_adj = 31;

    return (b5_adj << 10) | (g5_adj << 5) | r5_adj;
}

bool gpu_check_vertices_in_draw_area(int16_t vertices[][2], int vertex_count,
                                     uint16_t draw_left, uint16_t draw_top,
                                     uint16_t draw_right, uint16_t draw_bottom) {
    // Check if any vertex is outside the drawing area bounds
    // Returns true if all vertices are within bounds
    // Returns false if any vertex is outside (which might need clipping)

    for (int i = 0; i < vertex_count; i++) {
        int16_t x = vertices[i][0];
        int16_t y = vertices[i][1];

        if (x < (int16_t)draw_left || x > (int16_t)draw_right ||
            y < (int16_t)draw_top || y > (int16_t)draw_bottom) {
            // Vertex is outside drawing area
            LOG_GPU_DEBUG("Vertex %d at (%d, %d) outside draw area [%u..%u, %u..%u]",
                i, x, y, draw_left, draw_right, draw_top, draw_bottom);
            return false;
        }
    }

    return true; // All vertices are within bounds
}
