/* SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 ZioZoni95
 *
 * Part of ZoniStation One, a PlayStation 1 emulator.
 * See LICENSE for the full licence text and THIRD-PARTY.md for the
 * components of this project that have other authors.
 */
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

    // UV is always 8-bit (0..255). For 15bpp: 1 texel per halfword, page spans
    // 256 VRAM pixels. For 8bpp: 2 texels per halfword → 128 VRAM pixels = 256 UV.
    // For 4bpp: 4 texels per halfword → 64 VRAM pixels = 256 UV.
    switch (depth) {
        case 0: page_width = 256; break;
        case 1: page_width = 256; break;
        case 2: page_width = 256; break;
        default: return false;
    }

    // Texture coordinates are 8-bit, so 0-255 is max valid range
    // Check if U,V are within the page boundaries
    if (u >= page_width || v >= page_height) {
        /* Not a hard error: UV can legitimately span adjacent VRAM regions.
         * The shader handles it correctly. Log at DEBUG to avoid spam. */
        LOG_GPU_DEBUG("[GPU] Texture UV out of page: U=%d (page_w=%d) V=%d depth=%d",
                      u, page_width, v, depth);
        return false;
    }

    return true;
}

bool gpu_validate_clut_coords(uint16_t clut_packed, uint8_t depth) {
    // Extract CLUT coordinates from packed value
    uint16_t clut_x = (clut_packed & 0x3F) * 16;        // X: bits 0-5, multiply by 16
    uint16_t clut_y = (clut_packed >> 6) & 0x1FF;       // Y: bits 6-14

    /* A CLUT is a single horizontal strip of entries at (clut_x, clut_y): 16
     * halfwords for a 4-bit texture, 256 for an 8-bit one — one line tall in
     * both cases. The sampler in renderer.c does exactly that
     * (`clut_pos_x = clut_x + index` at one fixed `clut_y`, renderer.c:379-380).
     *
     * This used to model the 8-bit CLUT as a 16x16 block, so every palette
     * parked near the bottom of VRAM — y=511 is a favourite spot — was reported
     * as out of bounds. That is where Monsters & Co.'s 4662 warnings per 250
     * fields came from: a wrong bounds model, not a wrong CLUT. */
    uint16_t clut_entries;
    if (depth == 0)      clut_entries = 16;    /* 4-bit */
    else if (depth == 1) clut_entries = 256;   /* 8-bit */
    else                 return true;          /* 15-bit direct colour: no CLUT */

    if (clut_y >= 512 || (uint32_t)clut_x + clut_entries > 1024u) {
        LOG_GPU_WARN("[GPU] CLUT out of VRAM bounds: X=%u..%u, Y=%u (VRAM: 1024x512)",
                     (unsigned)clut_x, (unsigned)(clut_x + clut_entries - 1),
                     (unsigned)clut_y);
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
            LOG_GPU_DEBUG("[GPU] Vertex %d at (%d, %d) outside draw area [%u..%u, %u..%u]",
                i, x, y, draw_left, draw_right, draw_top, draw_bottom);
            return false;
        }
    }

    return true; // All vertices are within bounds
}
