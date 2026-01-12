/**
 * gpu_rendering.c
 * GPU Rendering Layer - Draw Commands Implementation
 * 
 * Based on DuckStation's gpu.cpp rendering functions
 * 
 * CRITICAL BUG FIX PRESERVED:
 *   ✅ Fix #1: UV coordinates use (w-1, h-1) for proper texture mapping (line ~82-85)
 */

#include "gpu/gpu_rendering.h"
#include "gpu/gpu_core.h"
#include "renderer.h"
#include "vram.h"
#include "log.h"
#include <stdio.h>

// ============================================================================
// Helper: Draw Rectangle (Core Function with UV FIX)
// ============================================================================

/**
 * Draw a rectangle primitive
 * 
 * CRITICAL FIX #1: UV coordinates calculated as (w-1, h-1)
 * This ensures textures map correctly to rectangle dimensions
 */
void draw_rectangle(GPU* gpu, int16_t x, int16_t y, uint16_t w, uint16_t h,
                   uint32_t color, bool textured, bool raw_texture,
                   uint8_t tex_u, uint8_t tex_v, uint16_t clut, uint16_t tpage) {
    RendererPosition p[4];
    RendererColor c[4];
    RendererTexCoord t[4];
    
    // Extract RGB from color word
    c[0].r = c[1].r = c[2].r = c[3].r = (GLubyte)(color & 0xFF);
    c[0].g = c[1].g = c[2].g = c[3].g = (GLubyte)((color >> 8) & 0xFF);
    c[0].b = c[1].b = c[2].b = c[3].b = (GLubyte)((color >> 16) & 0xFF);
    
    // Rectangle vertices: top-left, top-right, bottom-left, bottom-right
    p[0].x = x;       p[0].y = y;
    p[1].x = x + w;   p[1].y = y;
    p[2].x = x;       p[2].y = y + h;
    p[3].x = x + w;   p[3].y = y + h;
    
    if (textured) {
        // Ensure VRAM texture is up to date before textured draws
        mutex_lock(&gpu->vram_mutex);
        renderer_upload_vram(&gpu->renderer, (const uint16_t*)gpu->vram.data);
        mutex_unlock(&gpu->vram_mutex);
        
        // ⭐ CRITICAL FIX #1: UV coordinates use (w-1, h-1) ⭐
        // This ensures proper texture mapping for rectangles
        // DuckStation: texcoord + offset within primitive
        t[0].u = tex_u;           t[0].v = tex_v;
        t[1].u = tex_u + (w-1);   t[1].v = tex_v;           // ← (w-1)
        t[2].u = tex_u;           t[2].v = tex_v + (h-1);   // ← (h-1)
        t[3].u = tex_u + (w-1);   t[3].v = tex_v + (h-1);   // ← Both
        
        renderer_set_raw_texture_mode(&gpu->renderer, raw_texture);
        renderer_set_texture_mode(&gpu->renderer, true);
        renderer_push_quad(&gpu->renderer, p, c, t, clut, tpage);
    } else {
        renderer_set_raw_texture_mode(&gpu->renderer, false);
        renderer_set_texture_mode(&gpu->renderer, false);
        renderer_push_quad(&gpu->renderer, p, c, NULL, 0, 0);
    }
}

// ============================================================================
// Rectangle Handlers - Variable Size
// ============================================================================

/** GP0(0x60): Monochrome Rectangle (variable size, opaque) */
void gp0_rect_mono_variable_opaque(GPU* gpu) {
    if (gpu->gp0_command_buffer.count < 3) return;
    
    uint32_t color = gpu->gp0_command_buffer.buffer[0] & 0xFFFFFF;
    uint32_t vtx = gpu->gp0_command_buffer.buffer[1];
    uint32_t dim = gpu->gp0_command_buffer.buffer[2];
    
    int16_t x = (int16_t)(vtx & 0xFFFF);
    int16_t y = (int16_t)(vtx >> 16);
    uint16_t w = (uint16_t)(dim & 0xFFFF);
    uint16_t h = (uint16_t)(dim >> 16);
    
    if (w == 0) w = 1; 
    if (h == 0) h = 1;
    
    draw_rectangle(gpu, x, y, w, h, color, false, false, 0, 0, 0, 0);
}

/** GP0(0x62): Monochrome Rectangle (variable size, semi-transparent) */
void gp0_rect_mono_variable_semi(GPU* gpu) {
    // For now, treat same as opaque (TODO: implement semi-transparency)
    gp0_rect_mono_variable_opaque(gpu);
}

/** GP0(0x64): Textured Rectangle (variable size, opaque) */
void gp0_rect_tex_variable_opaque(GPU* gpu) {
    if (gpu->gp0_command_buffer.count < 4) return;
    
    uint32_t cmd = gpu->gp0_command_buffer.buffer[0];
    uint32_t color = cmd & 0xFFFFFF;
    uint32_t vtx = gpu->gp0_command_buffer.buffer[1];
    uint32_t uv_clut = gpu->gp0_command_buffer.buffer[2];
    uint32_t dim = gpu->gp0_command_buffer.buffer[3];
    
    int16_t x = (int16_t)(vtx & 0xFFFF);
    int16_t y = (int16_t)(vtx >> 16);
    uint8_t tex_u = (uint8_t)(uv_clut & 0xFF);
    uint8_t tex_v = (uint8_t)((uv_clut >> 8) & 0xFF);
    uint16_t clut = (uint16_t)(uv_clut >> 16);
    uint16_t w = (uint16_t)(dim & 0xFFFF);
    uint16_t h = (uint16_t)(dim >> 16);
    
    if (w == 0) w = 1;
    if (h == 0) h = 1;
    
    // Use current texture page from GPU state
    uint16_t tpage = (uint16_t)((gpu->page_base_x) | (gpu->page_base_y << 4) | (gpu->texture_depth << 7));
    
    uint8_t opcode = (uint8_t)(cmd >> 24);
    bool raw_texture = ((cmd & 0x01000000) != 0) || (opcode & 1);
    
    draw_rectangle(gpu, x, y, w, h, color, true, raw_texture, tex_u, tex_v, clut, tpage);
}

/** GP0(0x66): Textured Rectangle (variable size, semi-transparent) */
void gp0_rect_tex_variable_semi(GPU* gpu) {
    gp0_rect_tex_variable_opaque(gpu); // TODO: implement semi-transparency
}

// ============================================================================
// Rectangle Handlers - 1x1
// ============================================================================

/** GP0(0x68): Monochrome Rectangle 1x1 (opaque) */
void gp0_rect_mono_1x1_opaque(GPU* gpu) {
    if (gpu->gp0_command_buffer.count < 2) return;
    
    uint32_t color = gpu->gp0_command_buffer.buffer[0] & 0xFFFFFF;
    uint32_t vtx = gpu->gp0_command_buffer.buffer[1];
    
    int16_t x = (int16_t)(vtx & 0xFFFF);
    int16_t y = (int16_t)(vtx >> 16);
    
    draw_rectangle(gpu, x, y, 1, 1, color, false, false, 0, 0, 0, 0);
}

/** GP0(0x6A): Monochrome Rectangle 1x1 (semi-transparent) */
void gp0_rect_mono_1x1_semi(GPU* gpu) {
    gp0_rect_mono_1x1_opaque(gpu);
}

/** GP0(0x6C): Textured Rectangle 1x1 (opaque) */
void gp0_rect_tex_1x1_opaque(GPU* gpu) {
    if (gpu->gp0_command_buffer.count < 3) return;
    
    uint32_t cmd = gpu->gp0_command_buffer.buffer[0];
    uint32_t color = cmd & 0xFFFFFF;
    uint32_t vtx = gpu->gp0_command_buffer.buffer[1];
    uint32_t uv_clut = gpu->gp0_command_buffer.buffer[2];
    
    int16_t x = (int16_t)(vtx & 0xFFFF);
    int16_t y = (int16_t)(vtx >> 16);
    uint8_t tex_u = (uint8_t)(uv_clut & 0xFF);
    uint8_t tex_v = (uint8_t)((uv_clut >> 8) & 0xFF);
    uint16_t clut = (uint16_t)(uv_clut >> 16);
    uint16_t tpage = (uint16_t)((gpu->page_base_x) | (gpu->page_base_y << 4) | (gpu->texture_depth << 7));
    
    uint8_t opcode = (uint8_t)(cmd >> 24);
    bool raw_texture = ((cmd & 0x01000000) != 0) || (opcode & 1);
    
    draw_rectangle(gpu, x, y, 1, 1, color, true, raw_texture, tex_u, tex_v, clut, tpage);
}

/** GP0(0x6E): Textured Rectangle 1x1 (semi-transparent) */
void gp0_rect_tex_1x1_semi(GPU* gpu) {
    gp0_rect_tex_1x1_opaque(gpu);
}

// ============================================================================
// Rectangle Handlers - 8x8
// ============================================================================

/** GP0(0x70): Monochrome Rectangle 8x8 (opaque) */
void gp0_rect_mono_8x8_opaque(GPU* gpu) {
    if (gpu->gp0_command_buffer.count < 2) return;
    
    uint32_t color = gpu->gp0_command_buffer.buffer[0] & 0xFFFFFF;
    uint32_t vtx = gpu->gp0_command_buffer.buffer[1];
    
    int16_t x = (int16_t)(vtx & 0xFFFF);
    int16_t y = (int16_t)(vtx >> 16);
    
    draw_rectangle(gpu, x, y, 8, 8, color, false, false, 0, 0, 0, 0);
}

/** GP0(0x72): Monochrome Rectangle 8x8 (semi-transparent) */
void gp0_rect_mono_8x8_semi(GPU* gpu) {
    gp0_rect_mono_8x8_opaque(gpu);
}

/** GP0(0x74): Textured Rectangle 8x8 (opaque) */
void gp0_rect_tex_8x8_opaque(GPU* gpu) {
    if (gpu->gp0_command_buffer.count < 3) return;
    
    uint32_t cmd = gpu->gp0_command_buffer.buffer[0];
    uint32_t color = cmd & 0xFFFFFF;
    uint32_t vtx = gpu->gp0_command_buffer.buffer[1];
    uint32_t uv_clut = gpu->gp0_command_buffer.buffer[2];
    
    int16_t x = (int16_t)(vtx & 0xFFFF);
    int16_t y = (int16_t)(vtx >> 16);
    uint8_t tex_u = (uint8_t)(uv_clut & 0xFF);
    uint8_t tex_v = (uint8_t)((uv_clut >> 8) & 0xFF);
    uint16_t clut = (uint16_t)(uv_clut >> 16);
    uint16_t tpage = (uint16_t)((gpu->page_base_x) | (gpu->page_base_y << 4) | (gpu->texture_depth << 7));
    
    uint8_t opcode = (uint8_t)(cmd >> 24);
    bool raw_texture = ((cmd & 0x01000000) != 0) || (opcode & 1);
    
    draw_rectangle(gpu, x, y, 8, 8, color, true, raw_texture, tex_u, tex_v, clut, tpage);
}

/** GP0(0x76): Textured Rectangle 8x8 (semi-transparent) */
void gp0_rect_tex_8x8_semi(GPU* gpu) {
    gp0_rect_tex_8x8_opaque(gpu);
}

// ============================================================================
// Rectangle Handlers - 16x16 ⭐ CRITICAL FOR FONTS
// ============================================================================

/** GP0(0x78): Textured Rectangle 16x16 (opaque) ⭐ CRITICAL FOR BIOS TEXT */
void gp0_rect_tex_16x16_opaque(GPU* gpu) {
    if (gpu->gp0_command_buffer.count < 3) return;
    
    uint32_t cmd = gpu->gp0_command_buffer.buffer[0];
    uint32_t color = cmd & 0xFFFFFF;
    uint32_t vtx = gpu->gp0_command_buffer.buffer[1];
    uint32_t uv_clut = gpu->gp0_command_buffer.buffer[2];
    
    int16_t x = (int16_t)(vtx & 0xFFFF);
    int16_t y = (int16_t)(vtx >> 16);
    uint8_t tex_u = (uint8_t)(uv_clut & 0xFF);
    uint8_t tex_v = (uint8_t)((uv_clut >> 8) & 0xFF);
    uint16_t clut = (uint16_t)(uv_clut >> 16);
    uint16_t tpage = (uint16_t)((gpu->page_base_x) | (gpu->page_base_y << 4) | (gpu->texture_depth << 7));
    
    uint8_t opcode = (uint8_t)(cmd >> 24);
    bool raw_texture = ((cmd & 0x01000000) != 0) || (opcode & 1);
    
    // Debug logging for font rendering
    static int debug_count = 0;
    if (debug_count < 10) {
        LOG_GPU_INFO("[GPU] GP0(0x%02X) Textured Rect 16x16: Pos(%d,%d) UV(%d,%d) CLUT=0x%04X TPage=0x%04X\n",
                      opcode, x, y, tex_u, tex_v, clut, tpage);
        debug_count++;
    }
    
    draw_rectangle(gpu, x, y, 16, 16, color, true, raw_texture, tex_u, tex_v, clut, tpage);
}

/** GP0(0x7A): Textured Rectangle 16x16 (semi-transparent) */
void gp0_rect_tex_16x16_semi(GPU* gpu) {
    gp0_rect_tex_16x16_opaque(gpu);
}

/** GP0(0x7C): Monochrome Rectangle 16x16 (opaque) */
void gp0_rect_mono_16x16_opaque(GPU* gpu) {
    if (gpu->gp0_command_buffer.count < 2) return;
    
    uint32_t color = gpu->gp0_command_buffer.buffer[0] & 0xFFFFFF;
    uint32_t vtx = gpu->gp0_command_buffer.buffer[1];
    
    int16_t x = (int16_t)(vtx & 0xFFFF);
    int16_t y = (int16_t)(vtx >> 16);
    
    draw_rectangle(gpu, x, y, 16, 16, color, false, false, 0, 0, 0, 0);
}

/** GP0(0x7E): Monochrome Rectangle 16x16 (semi-transparent) */
void gp0_rect_mono_16x16_semi(GPU* gpu) {
    gp0_rect_mono_16x16_opaque(gpu);
}

// ============================================================================
// Polygon Handlers (Stubs - will be implemented in next iteration)
// ============================================================================

void gp0_triangle_mono_opaque(GPU* gpu) {
    LOG_GPU_WARN("GP0 Triangle Mono Opaque - TODO\n");
}

void gp0_triangle_mono_semi(GPU* gpu) {
    LOG_GPU_WARN("GP0 Triangle Mono Semi - TODO\n");
}

void gp0_triangle_tex_opaque(GPU* gpu) {
    if (gpu->gp0_command_buffer.count < 7) return;
    
    RendererColor c[3];
    RendererPosition p[3];
    RendererTexCoord uv[3];
    uint16_t clut = 0;
    uint16_t texpage = 0;
    
    // Parse vertices with UV coordinates
    for (int i = 0; i < 3; i++) {
        uint32_t cw = gpu->gp0_command_buffer.buffer[i * 2];
        uint32_t vw = gpu->gp0_command_buffer.buffer[i * 2 + 1];
        uint32_t uvw = gpu->gp0_command_buffer.buffer[i * 2 + 2];
        
        // Color (same for all vertices in flat shading)
        c[i].r = (GLubyte)(cw & 0xFF);
        c[i].g = (GLubyte)((cw >> 8) & 0xFF);
        c[i].b = (GLubyte)((cw >> 16) & 0xFF);
        
        // Position
        p[i].x = (GLshort)(int16_t)(vw & 0xFFFF);
        p[i].y = (GLshort)(int16_t)(vw >> 16);
        
        // UV coordinates
        uv[i].u = (GLubyte)(uvw & 0xFF);
        uv[i].v = (GLubyte)((uvw >> 8) & 0xFF);
        
        // Extract CLUT from first UV word
        if (i == 0) {
            clut = (uint16_t)(uvw >> 16);
        }
        // Extract texpage from second UV word
        if (i == 1) {
            texpage = (uint16_t)(uvw >> 16);
        }
    }
    
    renderer_set_texture_mode(&gpu->renderer, true);
    renderer_push_triangle(&gpu->renderer, p, c, uv, clut, texpage);
}

void gp0_triangle_tex_semi(GPU* gpu) {
    // TODO: Implement proper semi-transparency blending
    gp0_triangle_tex_opaque(gpu);
}

void gp0_quad_mono_opaque(GPU* gpu) {
    if (gpu->gp0_command_buffer.count < 5) return;
    
    uint32_t color = gpu->gp0_command_buffer.buffer[0] & 0xFFFFFF;
    RendererColor c;
    c.r = (GLubyte)(color & 0xFF);
    c.g = (GLubyte)((color >> 8) & 0xFF);
    c.b = (GLubyte)((color >> 16) & 0xFF);
    
    RendererColor colors[4] = {c, c, c, c};
    RendererPosition positions[4];
    
    for (int i = 0; i < 4; i++) {
        uint32_t v = gpu->gp0_command_buffer.buffer[i + 1];
        positions[i].x = (GLshort)(int16_t)(v & 0xFFFF);
        positions[i].y = (GLshort)(int16_t)(v >> 16);
    }
    
    renderer_set_texture_mode(&gpu->renderer, false);
    renderer_push_quad(&gpu->renderer, positions, colors, NULL, 0, 0);
}

void gp0_quad_mono_semi(GPU* gpu) {
    gp0_quad_mono_opaque(gpu);
}

void gp0_quad_tex_opaque(GPU* gpu) {
    if (gpu->gp0_command_buffer.count < 9) return;
    
    RendererColor c[4];
    RendererPosition p[4];
    RendererTexCoord uv[4];
    uint16_t clut = 0;
    uint16_t texpage = 0;
    
    // Parse vertices with UV coordinates
    for (int i = 0; i < 4; i++) {
        uint32_t cw = gpu->gp0_command_buffer.buffer[i * 2];
        uint32_t vw = gpu->gp0_command_buffer.buffer[i * 2 + 1];
        uint32_t uvw = gpu->gp0_command_buffer.buffer[i * 2 + 2];
        
        // Color (same for all vertices in flat shading)
        c[i].r = (GLubyte)(cw & 0xFF);
        c[i].g = (GLubyte)((cw >> 8) & 0xFF);
        c[i].b = (GLubyte)((cw >> 16) & 0xFF);
        
        // Position
        p[i].x = (GLshort)(int16_t)(vw & 0xFFFF);
        p[i].y = (GLshort)(int16_t)(vw >> 16);
        
        // UV coordinates - apply (w-1, h-1) fix for textured rects
        uv[i].u = (GLubyte)(uvw & 0xFF);
        uv[i].v = (GLubyte)((uvw >> 8) & 0xFF);
        
        // Extract CLUT from first UV word
        if (i == 0) {
            clut = (uint16_t)(uvw >> 16);
        }
        // Extract texpage from second UV word
        if (i == 1) {
            texpage = (uint16_t)(uvw >> 16);
        }
    }
    
    renderer_set_texture_mode(&gpu->renderer, true);
    renderer_push_quad(&gpu->renderer, p, c, uv, clut, texpage);
}

void gp0_quad_tex_semi(GPU* gpu) {
    // TODO: Implement proper semi-transparency blending
    gp0_quad_tex_opaque(gpu);
}

void gp0_triangle_shaded_opaque(GPU* gpu) {
    if (gpu->gp0_command_buffer.count < 6) return;
    
    RendererColor c[3];
    RendererPosition p[3];
    
    for (int i = 0; i < 3; i++) {
        uint32_t cw = gpu->gp0_command_buffer.buffer[i * 2];
        uint32_t vw = gpu->gp0_command_buffer.buffer[i * 2 + 1];
        
        c[i].r = (GLubyte)(cw & 0xFF);
        c[i].g = (GLubyte)((cw >> 8) & 0xFF);
        c[i].b = (GLubyte)((cw >> 16) & 0xFF);
        
        p[i].x = (GLshort)(int16_t)(vw & 0xFFFF);
        p[i].y = (GLshort)(int16_t)(vw >> 16);
    }
    
    renderer_set_texture_mode(&gpu->renderer, false);
    renderer_push_triangle(&gpu->renderer, p, c, NULL, 0, 0);
}

void gp0_triangle_shaded_semi(GPU* gpu) {
    gp0_triangle_shaded_opaque(gpu);
}

void gp0_triangle_shaded_tex_opaque(GPU* gpu) {
    if (gpu->gp0_command_buffer.count < 9) return;
    
    RendererColor c[3];
    RendererPosition p[3];
    RendererTexCoord uv[3];
    uint16_t clut = 0;
    uint16_t texpage = 0;
    
    // Parse vertices with per-vertex colors and UV coordinates
    for (int i = 0; i < 3; i++) {
        uint32_t cw = gpu->gp0_command_buffer.buffer[i * 3];
        uint32_t vw = gpu->gp0_command_buffer.buffer[i * 3 + 1];
        uint32_t uvw = gpu->gp0_command_buffer.buffer[i * 3 + 2];
        
        // Per-vertex color (Gouraud shading)
        c[i].r = (GLubyte)(cw & 0xFF);
        c[i].g = (GLubyte)((cw >> 8) & 0xFF);
        c[i].b = (GLubyte)((cw >> 16) & 0xFF);
        
        // Position
        p[i].x = (GLshort)(int16_t)(vw & 0xFFFF);
        p[i].y = (GLshort)(int16_t)(vw >> 16);
        
        // UV coordinates
        uv[i].u = (GLubyte)(uvw & 0xFF);
        uv[i].v = (GLubyte)((uvw >> 8) & 0xFF);
        
        // Extract CLUT from first UV word
        if (i == 0) {
            clut = (uint16_t)(uvw >> 16);
        }
        // Extract texpage from second UV word
        if (i == 1) {
            texpage = (uint16_t)(uvw >> 16);
        }
    }
    
    renderer_set_texture_mode(&gpu->renderer, true);
    renderer_push_triangle(&gpu->renderer, p, c, uv, clut, texpage);
}

void gp0_triangle_shaded_tex_semi(GPU* gpu) {
    // TODO: Implement proper semi-transparency blending
    gp0_triangle_shaded_tex_opaque(gpu);
}

void gp0_quad_shaded_opaque(GPU* gpu) {
    if (gpu->gp0_command_buffer.count < 8) return;
    
    RendererColor c[4];
    RendererPosition p[4];
    
    for (int i = 0; i < 4; i++) {
        uint32_t cw = gpu->gp0_command_buffer.buffer[i * 2];
        uint32_t vw = gpu->gp0_command_buffer.buffer[i * 2 + 1];
        
        c[i].r = (GLubyte)(cw & 0xFF);
        c[i].g = (GLubyte)((cw >> 8) & 0xFF);
        c[i].b = (GLubyte)((cw >> 16) & 0xFF);
        
        p[i].x = (GLshort)(int16_t)(vw & 0xFFFF);
        p[i].y = (GLshort)(int16_t)(vw >> 16);
    }
    
    renderer_set_texture_mode(&gpu->renderer, false);
    renderer_push_quad(&gpu->renderer, p, c, NULL, 0, 0);
}

void gp0_quad_shaded_semi(GPU* gpu) {
    gp0_quad_shaded_opaque(gpu);
}

void gp0_quad_shaded_tex_opaque(GPU* gpu) {
    if (gpu->gp0_command_buffer.count < 12) return;
    
    RendererColor c[4];
    RendererPosition p[4];
    RendererTexCoord uv[4];
    uint16_t clut = 0;
    uint16_t texpage = 0;
    
    // Parse vertices with per-vertex colors and UV coordinates
    for (int i = 0; i < 4; i++) {
        uint32_t cw = gpu->gp0_command_buffer.buffer[i * 3];
        uint32_t vw = gpu->gp0_command_buffer.buffer[i * 3 + 1];
        uint32_t uvw = gpu->gp0_command_buffer.buffer[i * 3 + 2];
        
        // Per-vertex color (Gouraud shading)
        c[i].r = (GLubyte)(cw & 0xFF);
        c[i].g = (GLubyte)((cw >> 8) & 0xFF);
        c[i].b = (GLubyte)((cw >> 16) & 0xFF);
        
        // Position
        p[i].x = (GLshort)(int16_t)(vw & 0xFFFF);
        p[i].y = (GLshort)(int16_t)(vw >> 16);
        
        // UV coordinates
        uv[i].u = (GLubyte)(uvw & 0xFF);
        uv[i].v = (GLubyte)((uvw >> 8) & 0xFF);
        
        // Extract CLUT from first UV word
        if (i == 0) {
            clut = (uint16_t)(uvw >> 16);
        }
        // Extract texpage from second UV word
        if (i == 1) {
            texpage = (uint16_t)(uvw >> 16);
        }
    }
    
    renderer_set_texture_mode(&gpu->renderer, true);
    renderer_push_quad(&gpu->renderer, p, c, uv, clut, texpage);
}

void gp0_quad_shaded_tex_semi(GPU* gpu) {
    // TODO: Implement proper semi-transparency blending
    gp0_quad_shaded_tex_opaque(gpu);
}

void gp0_line_mono(GPU* gpu) {
    if (gpu->gp0_command_buffer.count < 3) return;
    
    uint32_t color = gpu->gp0_command_buffer.buffer[0] & 0xFFFFFF;
    uint32_t v0 = gpu->gp0_command_buffer.buffer[1];
    uint32_t v1 = gpu->gp0_command_buffer.buffer[2];
    
    int16_t x0 = (int16_t)(v0 & 0xFFFF);
    int16_t y0 = (int16_t)(v0 >> 16);
    int16_t x1 = (int16_t)(v1 & 0xFFFF);
    int16_t y1 = (int16_t)(v1 >> 16);
    
    // Simple line implementation - renderer should handle Bresenham
    RendererColor c;
    c.r = (GLubyte)(color & 0xFF);
    c.g = (GLubyte)((color >> 8) & 0xFF);
    c.b = (GLubyte)((color >> 16) & 0xFF);
    
    RendererPosition p[2];
    p[0].x = x0;
    p[0].y = y0;
    p[1].x = x1;
    p[1].y = y1;
    
    RendererColor colors[2] = {c, c};
    
    renderer_set_texture_mode(&gpu->renderer, false);
    renderer_push_line(&gpu->renderer, p, colors);
}

void gp0_polyline_mono(GPU* gpu) {
    // Polylines terminated by 0x5xxx5xxx pattern
    uint32_t color = gpu->gp0_command_buffer.buffer[0] & 0xFFFFFF;
    
    RendererColor c;
    c.r = (GLubyte)(color & 0xFF);
    c.g = (GLubyte)((color >> 8) & 0xFF);
    c.b = (GLubyte)((color >> 16) & 0xFF);
    
    renderer_set_texture_mode(&gpu->renderer, false);
    
    // Draw lines between consecutive vertices
    for (int i = 1; i < (int)gpu->gp0_command_buffer.count - 1; i++) {
        uint32_t v0 = gpu->gp0_command_buffer.buffer[i];
        uint32_t v1 = gpu->gp0_command_buffer.buffer[i + 1];
        
        // Check for terminator
        if ((v1 & 0xF000F000) == 0x50005000) break;
        
        RendererPosition p[2];
        p[0].x = (int16_t)(v0 & 0xFFFF);
        p[0].y = (int16_t)(v0 >> 16);
        p[1].x = (int16_t)(v1 & 0xFFFF);
        p[1].y = (int16_t)(v1 >> 16);
        
        RendererColor colors[2] = {c, c};
        renderer_push_line(&gpu->renderer, p, colors);
    }
}

void gp0_line_shaded(GPU* gpu) {
    if (gpu->gp0_command_buffer.count < 4) return;
    
    uint32_t c0 = gpu->gp0_command_buffer.buffer[0];
    uint32_t v0 = gpu->gp0_command_buffer.buffer[1];
    uint32_t c1 = gpu->gp0_command_buffer.buffer[2];
    uint32_t v1 = gpu->gp0_command_buffer.buffer[3];
    
    RendererColor colors[2];
    colors[0].r = (GLubyte)(c0 & 0xFF);
    colors[0].g = (GLubyte)((c0 >> 8) & 0xFF);
    colors[0].b = (GLubyte)((c0 >> 16) & 0xFF);
    
    colors[1].r = (GLubyte)(c1 & 0xFF);
    colors[1].g = (GLubyte)((c1 >> 8) & 0xFF);
    colors[1].b = (GLubyte)((c1 >> 16) & 0xFF);
    
    RendererPosition p[2];
    p[0].x = (int16_t)(v0 & 0xFFFF);
    p[0].y = (int16_t)(v0 >> 16);
    p[1].x = (int16_t)(v1 & 0xFFFF);
    p[1].y = (int16_t)(v1 >> 16);
    
    renderer_set_texture_mode(&gpu->renderer, false);
    renderer_push_line(&gpu->renderer, p, colors);
}

void gp0_polyline_shaded(GPU* gpu) {
    // Shaded polylines have color/vertex pairs, terminated by 0x5xxx5xxx
    renderer_set_texture_mode(&gpu->renderer, false);
    
    // Each segment has: color, vertex pairs
    int idx = 0;
    uint32_t c0 = gpu->gp0_command_buffer.buffer[idx++];
    uint32_t v0 = gpu->gp0_command_buffer.buffer[idx++];
    
    while (idx < (int)gpu->gp0_command_buffer.count - 1) {
        uint32_t c1 = gpu->gp0_command_buffer.buffer[idx++];
        
        // Check for terminator on color word
        if ((c1 & 0xF000F000) == 0x50005000) break;
        
        uint32_t v1 = gpu->gp0_command_buffer.buffer[idx++];
        
        RendererColor colors[2];
        colors[0].r = (GLubyte)(c0 & 0xFF);
        colors[0].g = (GLubyte)((c0 >> 8) & 0xFF);
        colors[0].b = (GLubyte)((c0 >> 16) & 0xFF);
        
        colors[1].r = (GLubyte)(c1 & 0xFF);
        colors[1].g = (GLubyte)((c1 >> 8) & 0xFF);
        colors[1].b = (GLubyte)((c1 >> 16) & 0xFF);
        
        RendererPosition p[2];
        p[0].x = (int16_t)(v0 & 0xFFFF);
        p[0].y = (int16_t)(v0 >> 16);
        p[1].x = (int16_t)(v1 & 0xFFFF);
        p[1].y = (int16_t)(v1 >> 16);
        
        renderer_push_line(&gpu->renderer, p, colors);
        
        // Move to next segment
        c0 = c1;
        v0 = v1;
    }
}

// ============================================================================
// Environment/Setup Commands (from gpu.c lines 305-395)
// ============================================================================

/** GP0(0xE1): Set Draw Mode */
void gp0_draw_mode(GPU* gpu) {
    uint32_t value = gpu->gp0_command_buffer.buffer[0];
    
    gpu->page_base_x = (uint8_t)(value & 0xF);
    gpu->page_base_y = (uint8_t)((value >> 4) & 1);
    gpu->semi_transparency = (uint8_t)((value >> 5) & 3);
    
    switch ((value >> 7) & 3) {
        case 0: gpu->texture_depth = GPU_TEXTURE_MODE_PALETTE_4BIT; break;
        case 1: gpu->texture_depth = GPU_TEXTURE_MODE_PALETTE_8BIT; break;
        case 2: gpu->texture_depth = GPU_TEXTURE_MODE_DIRECT_16BIT; break;
        default: 
            LOG_GPU_WARN("GP0(E1) Unknown texture depth %d\n", (value >> 7) & 3); 
            break;
    }
    
    gpu->dithering = ((value >> 9) & 1);
    gpu->draw_to_display = ((value >> 10) & 1);
    gpu->texture_disable = ((value >> 11) & 1);
    gpu->rectangle_texture_x_flip = ((value >> 12) & 1);
    gpu->rectangle_texture_y_flip = ((value >> 13) & 1);
    
    LOG_GPU_INFO("GP0(0xE1): Draw Mode set page_base=(%u,%u) texture_depth=%d\n",
                 gpu->page_base_x, gpu->page_base_y, (int)gpu->texture_depth);
}

/** GP0(0xE2): Set Texture Window */
void gp0_texture_window(GPU* gpu) {
    uint32_t value = gpu->gp0_command_buffer.buffer[0];
    
    gpu->texture_window_x_mask = (uint8_t)(value & 0x1F);
    gpu->texture_window_y_mask = (uint8_t)((value >> 5) & 0x1F);
    gpu->texture_window_x_offset = (uint8_t)((value >> 10) & 0x1F);
    gpu->texture_window_y_offset = (uint8_t)((value >> 15) & 0x1F);
    
    renderer_set_texture_window(&gpu->renderer, 
        gpu->texture_window_x_mask, gpu->texture_window_y_mask,
        gpu->texture_window_x_offset, gpu->texture_window_y_offset);
}

/** GP0(0xE3): Set Drawing Area Top Left */
void gp0_drawing_area_top_left(GPU* gpu) {
    uint32_t value = gpu->gp0_command_buffer.buffer[0];
    
    gpu->drawing_area_left = (uint16_t)(value & 0x3FF);
    gpu->drawing_area_top = (uint16_t)((value >> 10) & 0x3FF);
}

/** GP0(0xE4): Set Drawing Area Bottom Right */
void gp0_drawing_area_bottom_right(GPU* gpu) {
    uint32_t value = gpu->gp0_command_buffer.buffer[0];
    
    gpu->drawing_area_right = (uint16_t)(value & 0x3FF);
    gpu->drawing_area_bottom = (uint16_t)((value >> 10) & 0x3FF);
}

/** GP0(0xE5): Set Drawing Offset */
void gp0_drawing_offset(GPU* gpu) {
    uint32_t value = gpu->gp0_command_buffer.buffer[0];
    
    uint16_t x_raw = (uint16_t)(value & 0x7FF);
    uint16_t y_raw = (uint16_t)((value >> 11) & 0x7FF);
    
    // Sign extend 11-bit values
    int16_t offset_x = (int16_t)(x_raw << 5) >> 5;
    int16_t offset_y = (int16_t)(y_raw << 5) >> 5;
    
    gpu->drawing_x_offset = offset_x;
    gpu->drawing_y_offset = offset_y;
    
    gpu_update_display_mapping(gpu);
}

/** GP0(0xE6): Set Mask Bit Setting */
void gp0_mask_bit_setting(GPU* gpu) {
    uint32_t value = gpu->gp0_command_buffer.buffer[0];
    
    gpu->force_set_mask_bit = (value & 1);
    gpu->preserve_masked_pixels = ((value >> 1) & 1);
    
    LOG_GPU_DEBUG("GP0(0xE6): Mask Bit Setting = Force:%d Preserve:%d", 
                  gpu->force_set_mask_bit, gpu->preserve_masked_pixels);
}

// ============================================================================
// Helper Functions
// ============================================================================

/** Recompute renderer screen scale and draw offset based on display area */
void gpu_update_display_mapping(GPU* gpu) {
    // Use full VRAM dimensions for renderer screen scale
    renderer_set_screen_scale(&gpu->renderer, VRAM_WIDTH, VRAM_HEIGHT);
    
    // Draw offset maps VRAM coordinates to screen coordinates
    renderer_set_draw_offset(&gpu->renderer, gpu->drawing_x_offset, gpu->drawing_y_offset);
}
