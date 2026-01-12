/**
 * gpu_rendering.h
 * GPU Rendering Layer - Draw Commands Interface
 * 
 * Based on DuckStation's gpu.cpp rendering functions
 * 
 * This module handles:
 *   - Rectangle drawing (fixed size and variable)
 *   - Polygon drawing (triangles and quads)
 *   - Line drawing (single and polylines)
 *   - Textured vs non-textured primitives
 *   - UV coordinate transformation (CRITICAL FIX #1)
 * 
 * CRITICAL BUG FIX PRESERVED:
 *   ✅ Fix #1: UV coordinates use (w-1, h-1) for proper texture mapping
 */
#ifndef GPU_RENDERING_H
#define GPU_RENDERING_H

#include "gpu_types.h"
#include <stdint.h>
#include <stdbool.h>

// Forward declaration
typedef struct GPU GPU;

// ============================================================================
// Rectangle Drawing Functions
// ============================================================================

/**
 * Draw a rectangle primitive (internal helper)
 * 
 * @param gpu       GPU state
 * @param x         Top-left X coordinate
 * @param y         Top-left Y coordinate  
 * @param w         Width in pixels
 * @param h         Height in pixels
 * @param col       Color (RGB888)
 * @param textured  Enable texture mapping
 * @param raw_texture Raw texture mode (no color modulation)
 * @param tex       Texture coordinates (if textured)
 * @param clut      CLUT address (for indexed textures)
 * @param tpage     Texture page info
 * 
 * CRITICAL: UV coordinates are calculated as (w-1, h-1) for proper mapping
 */
void draw_rectangle(GPU* gpu, int16_t x, int16_t y, uint16_t w, uint16_t h,
                   uint32_t color, bool textured, bool raw_texture,
                   uint8_t tex_u, uint8_t tex_v, uint16_t clut, uint16_t tpage);

// ============================================================================
// GP0 Rectangle Handlers
// ============================================================================

/** GP0(0x60-0x63): Monochrome Rectangles (Variable Size) */
void gp0_rect_mono_variable_opaque(GPU* gpu);
void gp0_rect_mono_variable_semi(GPU* gpu);

/** GP0(0x64-0x67): Textured Rectangles (Variable Size) */
void gp0_rect_tex_variable_opaque(GPU* gpu);
void gp0_rect_tex_variable_semi(GPU* gpu);

/** GP0(0x68-0x6B): Monochrome Rectangles (1x1) */
void gp0_rect_mono_1x1_opaque(GPU* gpu);
void gp0_rect_mono_1x1_semi(GPU* gpu);

/** GP0(0x6C-0x6F): Textured Rectangles (1x1) */
void gp0_rect_tex_1x1_opaque(GPU* gpu);
void gp0_rect_tex_1x1_semi(GPU* gpu);

/** GP0(0x70-0x73): Monochrome Rectangles (8x8) */
void gp0_rect_mono_8x8_opaque(GPU* gpu);
void gp0_rect_mono_8x8_semi(GPU* gpu);

/** GP0(0x74-0x77): Textured Rectangles (8x8) */
void gp0_rect_tex_8x8_opaque(GPU* gpu);
void gp0_rect_tex_8x8_semi(GPU* gpu);

/** GP0(0x78-0x7B): Textured Rectangles (16x16) ⭐ CRITICAL FOR FONTS */
void gp0_rect_tex_16x16_opaque(GPU* gpu);
void gp0_rect_tex_16x16_semi(GPU* gpu);

/** GP0(0x7C-0x7F): Monochrome Rectangles (16x16) */
void gp0_rect_mono_16x16_opaque(GPU* gpu);
void gp0_rect_mono_16x16_semi(GPU* gpu);

// ============================================================================
// GP0 Polygon Handlers
// ============================================================================

/** GP0(0x20-0x23): Monochrome Triangles */
void gp0_triangle_mono_opaque(GPU* gpu);
void gp0_triangle_mono_semi(GPU* gpu);

/** GP0(0x24-0x27): Textured Triangles */
void gp0_triangle_tex_opaque(GPU* gpu);
void gp0_triangle_tex_semi(GPU* gpu);

/** GP0(0x28-0x2B): Monochrome Quads */
void gp0_quad_mono_opaque(GPU* gpu);
void gp0_quad_mono_semi(GPU* gpu);

/** GP0(0x2C-0x2F): Textured Quads */
void gp0_quad_tex_opaque(GPU* gpu);
void gp0_quad_tex_semi(GPU* gpu);

/** GP0(0x30-0x33): Shaded Triangles */
void gp0_triangle_shaded_opaque(GPU* gpu);
void gp0_triangle_shaded_semi(GPU* gpu);

/** GP0(0x34-0x37): Shaded Textured Triangles */
void gp0_triangle_shaded_tex_opaque(GPU* gpu);
void gp0_triangle_shaded_tex_semi(GPU* gpu);

/** GP0(0x38-0x3B): Shaded Quads */
void gp0_quad_shaded_opaque(GPU* gpu);
void gp0_quad_shaded_semi(GPU* gpu);

/** GP0(0x3C-0x3F): Shaded Textured Quads */
void gp0_quad_shaded_tex_opaque(GPU* gpu);
void gp0_quad_shaded_tex_semi(GPU* gpu);

// ============================================================================
// GP0 Line Handlers
// ============================================================================

/** GP0(0x40-0x47): Lines (Monochrome/Polyline/Shaded) */
void gp0_line_mono(GPU* gpu);
void gp0_polyline_mono(GPU* gpu);
void gp0_line_shaded(GPU* gpu);
void gp0_polyline_shaded(GPU* gpu);

// ============================================================================
// GP0 Environment/Setup Commands
// ============================================================================

/** GP0(0xE1): Set Draw Mode */
void gp0_draw_mode(GPU* gpu);

/** GP0(0xE2): Set Texture Window */
void gp0_texture_window(GPU* gpu);

/** GP0(0xE3): Set Drawing Area Top Left */
void gp0_drawing_area_top_left(GPU* gpu);

/** GP0(0xE4): Set Drawing Area Bottom Right */
void gp0_drawing_area_bottom_right(GPU* gpu);

/** GP0(0xE5): Set Drawing Offset */
void gp0_drawing_offset(GPU* gpu);

/** GP0(0xE6): Set Mask Bit Setting */
void gp0_mask_bit_setting(GPU* gpu);

// ============================================================================
// Helper Functions
// ============================================================================

/**
 * Update display mapping for renderer
 * Recalculates screen scale and drawing offset based on display configuration
 */
void gpu_update_display_mapping(GPU* gpu);

#endif // GPU_RENDERING_H
