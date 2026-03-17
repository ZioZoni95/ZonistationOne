/**
 * gpu_commands.c
 * All GP0 command handlers for the PlayStation GPU.
 * Includes command dispatch, primitive rendering (poly, rect, line),
 * VRAM transfer operations, and state-setting commands.
 *
 * Modular split from gpu.c — DuckStation-inspired architecture.
 */

#include "gpu.h"
#include "renderer.h"
#include "vram.h"
#include "log.h"
#include "interconnect.h"
#include <string.h>
#include <stdlib.h>

// ---------------------------------------------------------------------------
// Forward declarations
// ---------------------------------------------------------------------------
static void gp0_nop(Gpu* gpu);
static void gp0_clear_cache(Gpu* gpu);
static void gp0_fill_rectangle(Gpu* gpu);
static void gp0_draw_mode(Gpu* gpu);
static void gp0_texture_window(Gpu* gpu);
static void gp0_drawing_area_top_left(Gpu* gpu);
static void gp0_drawing_area_bottom_right(Gpu* gpu);
static void gp0_drawing_offset(Gpu* gpu);
static void gp0_mask_bit_setting(Gpu* gpu);

// Polygon handlers
static void gp0_tri_mono_opaque(Gpu* gpu);
static void gp0_tri_mono_semi(Gpu* gpu);
static void gp0_tri_tex_blend_opaque(Gpu* gpu);
static void gp0_tri_tex_blend_semi(Gpu* gpu);
static void gp0_tri_tex_raw_opaque(Gpu* gpu);
static void gp0_tri_tex_raw_semi(Gpu* gpu);
static void gp0_quad_mono_opaque(Gpu* gpu);
static void gp0_quad_mono_semi(Gpu* gpu);
static void gp0_quad_tex_blend_opaque(Gpu* gpu);
static void gp0_quad_tex_blend_semi(Gpu* gpu);
static void gp0_quad_tex_raw_opaque(Gpu* gpu);
static void gp0_quad_tex_raw_semi(Gpu* gpu);
static void gp0_tri_shaded_opaque(Gpu* gpu);
static void gp0_tri_shaded_semi(Gpu* gpu);
static void gp0_quad_shaded_opaque(Gpu* gpu);
static void gp0_quad_shaded_semi(Gpu* gpu);
static void gp0_tri_shaded_tex_blend_opaque(Gpu* gpu);
static void gp0_tri_shaded_tex_blend_semi(Gpu* gpu);
static void gp0_quad_shaded_tex_blend_opaque(Gpu* gpu);
static void gp0_quad_shaded_tex_blend_semi(Gpu* gpu);

// Rectangle handlers
static void gp0_rect_variable_opaque(Gpu* gpu);
static void gp0_rect_variable_semi(Gpu* gpu);
static void gp0_rect_tex_variable_opaque(Gpu* gpu);
static void gp0_rect_tex_variable_semi(Gpu* gpu);
static void gp0_rect_1x1_opaque(Gpu* gpu);
static void gp0_rect_1x1_semi(Gpu* gpu);
static void gp0_rect_tex_1x1_opaque(Gpu* gpu);
static void gp0_rect_tex_1x1_semi(Gpu* gpu);
static void gp0_rect_8x8_opaque(Gpu* gpu);
static void gp0_rect_8x8_semi(Gpu* gpu);
static void gp0_rect_tex_8x8_opaque(Gpu* gpu);
static void gp0_rect_tex_8x8_semi(Gpu* gpu);
static void gp0_rect_16x16_opaque(Gpu* gpu);
static void gp0_rect_16x16_semi(Gpu* gpu);
static void gp0_rect_tex_16x16_opaque(Gpu* gpu);
static void gp0_rect_tex_16x16_semi(Gpu* gpu);

// VRAM ops
static void gp0_copy_rectangle(Gpu* gpu);
static void gp0_image_load(Gpu* gpu);
static void gp0_image_store(Gpu* gpu);

// Line handlers (simple 2-vertex forms)
static void gp0_line_mono_opaque(Gpu* gpu);
static void gp0_line_mono_semi(Gpu* gpu);
static void gp0_line_shaded_opaque(Gpu* gpu);
static void gp0_line_shaded_semi(Gpu* gpu);

// Polyline (variable-length, starts state machine in gpu_gp0_handle_word)
static void gp0_polyline_mono_opaque(Gpu* gpu);
static void gp0_polyline_mono_semi(Gpu* gpu);
static void gp0_polyline_shaded_opaque(Gpu* gpu);
static void gp0_polyline_shaded_semi(Gpu* gpu);

// Internal helpers
static void draw_rectangle(Gpu* gpu, int16_t x, int16_t y, uint16_t w, uint16_t h,
                            RendererColor col, bool textured, bool raw_texture,
                            RendererTexCoord* tex, uint16_t clut, uint16_t tpage,
                            bool semi_trans);
static void vram_write_masked(Gpu* gpu, uint32_t offset, uint16_t pixel);
static void flush_polyline(Gpu* gpu);

// ---------------------------------------------------------------------------
// Shared helpers (also used by gpu.c via non-static linkage)
// ---------------------------------------------------------------------------

void gpu_clear_cmd_buf(Gpu* gpu) {
    gpu->gp0_command_buffer.count = 0;
}

void gpu_push_cmd_word(Gpu* gpu, uint32_t word) {
    if (gpu->gp0_command_buffer.count >= MAX_GPU_COMMAND_WORDS) {
        LOG_GPU_ERROR("FATAL: GP0 Command Buffer Overflow! Opcode: 0x%02x",
                      gpu->gp0_current_opcode);
        exit(EXIT_FAILURE);
    }
    gpu->gp0_command_buffer.buffer[gpu->gp0_command_buffer.count++] = word;
}

// ---------------------------------------------------------------------------
// VRAM masked write (respects force_set_mask_bit / preserve_masked_pixels)
// ---------------------------------------------------------------------------
static void vram_write_masked(Gpu* gpu, uint32_t offset, uint16_t pixel) {
    if (gpu->force_set_mask_bit)
        pixel |= 0x8000;
    if (gpu->preserve_masked_pixels) {
        uint16_t dst = vram_load16(&gpu->vram, offset);
        if (dst & 0x8000)
            return;
    }
    vram_store16(&gpu->vram, offset, pixel);
}

// ---------------------------------------------------------------------------
// Helper: Draw a rectangle as a quad
// ---------------------------------------------------------------------------
static void draw_rectangle(Gpu* gpu, int16_t x, int16_t y, uint16_t w, uint16_t h,
                            RendererColor col, bool textured, bool raw_texture,
                            RendererTexCoord* tex, uint16_t clut, uint16_t tpage,
                            bool semi_trans)
{
    RendererPosition p[4];
    RendererColor c[4] = {col, col, col, col};
    RendererTexCoord t[4];

    p[0].x = x;       p[0].y = y;
    p[1].x = x + w;   p[1].y = y;
    p[2].x = x;       p[2].y = y + h;
    p[3].x = x + w;   p[3].y = y + h;

    bool use_texture = textured && tex;

    renderer_set_semi_trans_mode(&gpu->renderer, semi_trans, gpu->semi_transparency);

    if (use_texture) {
        renderer_upload_vram(&gpu->renderer, (const uint16_t*)gpu->vram.data);
        gpu->vram_dirty = false;
        // Apply texture flip if enabled (for rectangle primitives)
        t[0].u = tex->u;       t[0].v = tex->v;
        t[1].u = tex->u + w;   t[1].v = tex->v;
        t[2].u = tex->u;       t[2].v = tex->v + h;
        t[3].u = tex->u + w;   t[3].v = tex->v + h;
        if (gpu->rectangle_texture_x_flip) {
            t[0].u = tex->u + w - 1; t[1].u = tex->u;
            t[2].u = tex->u + w - 1; t[3].u = tex->u;
        }
        if (gpu->rectangle_texture_y_flip) {
            t[0].v = tex->v + h - 1; t[1].v = tex->v + h - 1;
            t[2].v = tex->v;         t[3].v = tex->v;
        }
        renderer_set_raw_texture_mode(&gpu->renderer, raw_texture);
        renderer_set_texture_mode(&gpu->renderer, true);
        renderer_push_quad(&gpu->renderer, p, c, t, clut, tpage);
    } else {
        renderer_set_raw_texture_mode(&gpu->renderer, false);
        renderer_set_texture_mode(&gpu->renderer, false);
        renderer_push_quad(&gpu->renderer, p, c, NULL, 0, 0);
    }
}

// ---------------------------------------------------------------------------
// Helper: make tpage word from current GPU state
// ---------------------------------------------------------------------------
static inline uint16_t make_tpage(Gpu* gpu) {
    return (uint16_t)(gpu->page_base_x | (gpu->page_base_y << 4) |
                      ((uint16_t)gpu->texture_depth << 7));
}

// ---------------------------------------------------------------------------
// Helper: upload VRAM only when dirty
// ---------------------------------------------------------------------------
static inline void upload_vram_if_dirty(Gpu* gpu) {
    if (gpu->vram_dirty) {
        renderer_upload_vram(&gpu->renderer, (const uint16_t*)gpu->vram.data);
        gpu->vram_dirty = false;
    }
}

// ---------------------------------------------------------------------------
// NOP / Clear Cache
// ---------------------------------------------------------------------------
static void gp0_nop(Gpu* gpu) { (void)gpu; }

static void gp0_clear_cache(Gpu* gpu) {
    LOG_GPU_DEBUG("GP0(0x01): Clear Cache (no-op)");
    (void)gpu;
}

// ---------------------------------------------------------------------------
// GP0(0x02): Fill Rectangle in VRAM
// ---------------------------------------------------------------------------
static void gp0_fill_rectangle(Gpu* gpu) {
    if (gpu->gp0_command_buffer.count < 3) {
        LOG_GPU_ERROR("GP0(0x02) Error: Expected 3 words, got %u", gpu->gp0_command_buffer.count);
        return;
    }

    uint32_t color_val = gpu->gp0_command_buffer.buffer[0];
    uint32_t pos_val   = gpu->gp0_command_buffer.buffer[1];
    uint32_t dim_val   = gpu->gp0_command_buffer.buffer[2];

    RendererColor col = {
        .r = (GLubyte)(color_val & 0xFF),
        .g = (GLubyte)((color_val >> 8) & 0xFF),
        .b = (GLubyte)((color_val >> 16) & 0xFF)
    };

    int16_t x  = (int16_t)(pos_val & 0xFFFF);
    int16_t y  = (int16_t)(pos_val >> 16);
    uint16_t w = (uint16_t)(dim_val & 0xFFFF);
    uint16_t h = (uint16_t)(dim_val >> 16);

    // Align to 16-pixel grid
    x = x & ~0xF;
    w = (w + 0xF) & ~0xF;

    // Fill ignores drawing offset — subtract it so the renderer's offset doesn't double-add
    int16_t adj_x = x - gpu->drawing_x_offset;
    int16_t adj_y = y - gpu->drawing_y_offset;

    LOG_GPU_DEBUG("GP0(0x02): Fill Rect (%d,%d) %dx%d Color=%06x", x, y, w, h, color_val & 0xFFFFFF);

    // Render via OpenGL (opaque, no semi-trans, no texture)
    draw_rectangle(gpu, adj_x, adj_y, w, h, col, false, false, NULL, 0, 0, false);

    // Sync CPU-side VRAM (for future texture reads / VRAM→CPU)
    uint16_t r5 = (col.r >> 3) & 0x1F;
    uint16_t g5 = (col.g >> 3) & 0x1F;
    uint16_t b5 = (col.b >> 3) & 0x1F;
    uint16_t pixel = r5 | (g5 << 5) | (b5 << 10);

    int16_t clip_l = gpu->drawing_area_left;
    int16_t clip_t = gpu->drawing_area_top;
    int16_t clip_r = gpu->drawing_area_right;
    int16_t clip_b = gpu->drawing_area_bottom;

    int16_t sx = (x > clip_l) ? x : clip_l;
    int16_t sy = (y > clip_t) ? y : clip_t;
    int16_t ex = ((x + w) < (clip_r + 1)) ? (x + w) : (clip_r + 1);
    int16_t ey = ((y + h) < (clip_b + 1)) ? (y + h) : (clip_b + 1);

    for (int iy = sy; iy < ey; iy++) {
        int vy = iy & 0x1FF;
        for (int ix = sx; ix < ex; ix++) {
            int vx = ix & 0x3FF;
            uint32_t off = (uint32_t)vy * VRAM_WIDTH * VRAM_BPP + (uint32_t)vx * VRAM_BPP;
            vram_write_masked(gpu, off, pixel);
        }
    }
    gpu->vram_dirty = true;
}

// ---------------------------------------------------------------------------
// State-setting commands (GP0 E1–E6)
// ---------------------------------------------------------------------------
static void gp0_draw_mode(Gpu* gpu) {
    uint32_t value = gpu->gp0_command_buffer.buffer[0];
    gpu->page_base_x = (uint8_t)(value & 0xF);
    gpu->page_base_y = (uint8_t)((value >> 4) & 1);
    gpu->semi_transparency = (uint8_t)((value >> 5) & 3);
    switch ((value >> 7) & 3) {
        case 0: gpu->texture_depth = T4Bit;  break;
        case 1: gpu->texture_depth = T8Bit;  break;
        case 2: gpu->texture_depth = T15Bit; break;
        default: LOG_GPU_WARN("GP0(E1) unknown texture depth %d", (value >> 7) & 3); break;
    }
    gpu->dithering                  = ((value >> 9) & 1) != 0;
    gpu->draw_to_display            = ((value >> 10) & 1) != 0;
    gpu->texture_disable            = ((value >> 11) & 1) != 0;
    gpu->rectangle_texture_x_flip   = ((value >> 12) & 1) != 0;
    gpu->rectangle_texture_y_flip   = ((value >> 13) & 1) != 0;
    LOG_GPU_DEBUG("GP0(0xE1): Draw Mode page=(%u,%u) depth=%d semi=%u draw_to_disp=%u tex_disable=%u flip=(%u,%u)",
                 gpu->page_base_x, gpu->page_base_y, (int)gpu->texture_depth,
                 gpu->semi_transparency, (int)gpu->draw_to_display,
                 (int)gpu->texture_disable,
                 (int)gpu->rectangle_texture_x_flip, (int)gpu->rectangle_texture_y_flip);
}

static void gp0_texture_window(Gpu* gpu) {
    uint32_t value = gpu->gp0_command_buffer.buffer[0];
    gpu->texture_window_x_mask   = (uint8_t)(value & 0x1F);
    gpu->texture_window_y_mask   = (uint8_t)((value >> 5) & 0x1F);
    gpu->texture_window_x_offset = (uint8_t)((value >> 10) & 0x1F);
    gpu->texture_window_y_offset = (uint8_t)((value >> 15) & 0x1F);
    renderer_set_texture_window(&gpu->renderer,
        gpu->texture_window_x_mask, gpu->texture_window_y_mask,
        gpu->texture_window_x_offset, gpu->texture_window_y_offset);
    LOG_GPU_DEBUG("GP0(0xE2): Texture Window -> Mask(%u,%u) Offset(%u,%u)",
        gpu->texture_window_x_mask, gpu->texture_window_y_mask,
        gpu->texture_window_x_offset, gpu->texture_window_y_offset);
}

static void gp0_drawing_area_top_left(Gpu* gpu) {
    uint32_t value = gpu->gp0_command_buffer.buffer[0];
    gpu->drawing_area_left = (uint16_t)(value & 0x3FF);
    gpu->drawing_area_top  = (uint16_t)((value >> 10) & 0x3FF);
    renderer_set_drawing_area(&gpu->renderer,
        gpu->drawing_area_left, gpu->drawing_area_top,
        gpu->drawing_area_right, gpu->drawing_area_bottom);
}

static void gp0_drawing_area_bottom_right(Gpu* gpu) {
    uint32_t value = gpu->gp0_command_buffer.buffer[0];
    gpu->drawing_area_right  = (uint16_t)(value & 0x3FF);
    gpu->drawing_area_bottom = (uint16_t)((value >> 10) & 0x3FF);
    renderer_set_drawing_area(&gpu->renderer,
        gpu->drawing_area_left, gpu->drawing_area_top,
        gpu->drawing_area_right, gpu->drawing_area_bottom);
}

static void gp0_drawing_offset(Gpu* gpu) {
    uint32_t value = gpu->gp0_command_buffer.buffer[0];
    uint16_t x_raw = (uint16_t)(value & 0x7FF);
    uint16_t y_raw = (uint16_t)((value >> 11) & 0x7FF);
    gpu->drawing_x_offset = (int16_t)(x_raw << 5) >> 5;
    gpu->drawing_y_offset = (int16_t)(y_raw << 5) >> 5;
    LOG_GPU_DEBUG("GP0(0xE5): Drawing Offset (%d, %d)", gpu->drawing_x_offset, gpu->drawing_y_offset);
    renderer_set_draw_offset(&gpu->renderer, gpu->drawing_x_offset, gpu->drawing_y_offset);
    gpu_update_display_mapping(gpu);
}

static void gp0_mask_bit_setting(Gpu* gpu) {
    uint32_t value = gpu->gp0_command_buffer.buffer[0];
    gpu->force_set_mask_bit      = (value & 1) != 0;
    gpu->preserve_masked_pixels  = ((value >> 1) & 1) != 0;
    LOG_GPU_DEBUG("GP0(0xE6): Mask Bit force=%d preserve=%d",
                  gpu->force_set_mask_bit, gpu->preserve_masked_pixels);
}

// ---------------------------------------------------------------------------
// GP0(0x1F): Interrupt Request
// ---------------------------------------------------------------------------
static void gp0_interrupt_request(Gpu* gpu) {
    gpu->interrupt = true;
    LOG_GPU_DEBUG("GP0(0x1F): Interrupt Request");
}

// ---------------------------------------------------------------------------
// Monochrome Triangle (0x20–0x23)
// ---------------------------------------------------------------------------
static void gp0_tri_mono_opaque(Gpu* gpu) {
    if (gpu->gp0_command_buffer.count < 4) return;
    RendererColor col;
    col.r = (GLubyte)(gpu->gp0_command_buffer.buffer[0] & 0xFF);
    col.g = (GLubyte)((gpu->gp0_command_buffer.buffer[0] >> 8) & 0xFF);
    col.b = (GLubyte)((gpu->gp0_command_buffer.buffer[0] >> 16) & 0xFF);
    RendererColor colors[3] = {col, col, col};
    RendererPosition p[3];
    for (int i = 0; i < 3; i++) {
        uint32_t v = gpu->gp0_command_buffer.buffer[i + 1];
        p[i].x = (GLshort)(int16_t)(v & 0xFFFF);
        p[i].y = (GLshort)(int16_t)(v >> 16);
    }
    renderer_set_semi_trans_mode(&gpu->renderer, false, 0);
    renderer_set_texture_mode(&gpu->renderer, false);
    renderer_push_triangle(&gpu->renderer, p, colors, NULL, 0, 0);
}

static void gp0_tri_mono_semi(Gpu* gpu) {
    if (gpu->gp0_command_buffer.count < 4) return;
    RendererColor col;
    col.r = (GLubyte)(gpu->gp0_command_buffer.buffer[0] & 0xFF);
    col.g = (GLubyte)((gpu->gp0_command_buffer.buffer[0] >> 8) & 0xFF);
    col.b = (GLubyte)((gpu->gp0_command_buffer.buffer[0] >> 16) & 0xFF);
    RendererColor colors[3] = {col, col, col};
    RendererPosition p[3];
    for (int i = 0; i < 3; i++) {
        uint32_t v = gpu->gp0_command_buffer.buffer[i + 1];
        p[i].x = (GLshort)(int16_t)(v & 0xFFFF);
        p[i].y = (GLshort)(int16_t)(v >> 16);
    }
    renderer_set_semi_trans_mode(&gpu->renderer, true, gpu->semi_transparency);
    renderer_set_texture_mode(&gpu->renderer, false);
    renderer_push_triangle(&gpu->renderer, p, colors, NULL, 0, 0);
}

// ---------------------------------------------------------------------------
// Textured Triangle (0x24–0x27): cmd, v0, uv0+clut, v1, uv1+tpage, v2, uv2
// ---------------------------------------------------------------------------
static void gp0_tri_tex_impl(Gpu* gpu, bool semi_trans, bool raw_texture) {
    if (gpu->gp0_command_buffer.count < 7) return;
    RendererColor col;
    col.r = (GLubyte)(gpu->gp0_command_buffer.buffer[0] & 0xFF);
    col.g = (GLubyte)((gpu->gp0_command_buffer.buffer[0] >> 8) & 0xFF);
    col.b = (GLubyte)((gpu->gp0_command_buffer.buffer[0] >> 16) & 0xFF);
    RendererColor colors[3] = {col, col, col};
    RendererPosition p[3];
    RendererTexCoord t[3];
    uint16_t clut = 0, texpage = 0;

    p[0].x = (GLshort)(int16_t)(gpu->gp0_command_buffer.buffer[1] & 0xFFFF);
    p[0].y = (GLshort)(int16_t)(gpu->gp0_command_buffer.buffer[1] >> 16);
    t[0].u = (GLshort)(gpu->gp0_command_buffer.buffer[2] & 0xFF);
    t[0].v = (GLshort)((gpu->gp0_command_buffer.buffer[2] >> 8) & 0xFF);
    clut   = (uint16_t)(gpu->gp0_command_buffer.buffer[2] >> 16);

    p[1].x = (GLshort)(int16_t)(gpu->gp0_command_buffer.buffer[3] & 0xFFFF);
    p[1].y = (GLshort)(int16_t)(gpu->gp0_command_buffer.buffer[3] >> 16);
    t[1].u = (GLshort)(gpu->gp0_command_buffer.buffer[4] & 0xFF);
    t[1].v = (GLshort)((gpu->gp0_command_buffer.buffer[4] >> 8) & 0xFF);
    texpage = (uint16_t)(gpu->gp0_command_buffer.buffer[4] >> 16);

    p[2].x = (GLshort)(int16_t)(gpu->gp0_command_buffer.buffer[5] & 0xFFFF);
    p[2].y = (GLshort)(int16_t)(gpu->gp0_command_buffer.buffer[5] >> 16);
    t[2].u = (GLshort)(gpu->gp0_command_buffer.buffer[6] & 0xFF);
    t[2].v = (GLshort)((gpu->gp0_command_buffer.buffer[6] >> 8) & 0xFF);

    upload_vram_if_dirty(gpu);
    renderer_set_semi_trans_mode(&gpu->renderer, semi_trans, gpu->semi_transparency);
    renderer_set_raw_texture_mode(&gpu->renderer, raw_texture);
    renderer_set_texture_mode(&gpu->renderer, true);
    renderer_push_triangle(&gpu->renderer, p, colors, t, clut, texpage);
}

static void gp0_tri_tex_blend_opaque(Gpu* gpu) { gp0_tri_tex_impl(gpu, false, false); }
static void gp0_tri_tex_blend_semi(Gpu* gpu)   { gp0_tri_tex_impl(gpu, true,  false); }
static void gp0_tri_tex_raw_opaque(Gpu* gpu)   { gp0_tri_tex_impl(gpu, false, true);  }
static void gp0_tri_tex_raw_semi(Gpu* gpu)     { gp0_tri_tex_impl(gpu, true,  true);  }

// ---------------------------------------------------------------------------
// Shaded Triangle (0x30–0x37)
// ---------------------------------------------------------------------------
static void gp0_tri_shaded_impl(Gpu* gpu, bool semi_trans) {
    if (gpu->gp0_command_buffer.count < 6) return;
    RendererColor c[3]; RendererPosition p[3];
    for (int i = 0; i < 3; i++) {
        uint32_t cw = gpu->gp0_command_buffer.buffer[i * 2];
        uint32_t vw = gpu->gp0_command_buffer.buffer[i * 2 + 1];
        c[i].r = (GLubyte)(cw & 0xFF);
        c[i].g = (GLubyte)((cw >> 8) & 0xFF);
        c[i].b = (GLubyte)((cw >> 16) & 0xFF);
        p[i].x = (GLshort)(int16_t)(vw & 0xFFFF);
        p[i].y = (GLshort)(int16_t)(vw >> 16);
    }
    renderer_set_semi_trans_mode(&gpu->renderer, semi_trans, gpu->semi_transparency);
    renderer_set_texture_mode(&gpu->renderer, false);
    renderer_push_triangle(&gpu->renderer, p, c, NULL, 0, 0);
}

static void gp0_tri_shaded_opaque(Gpu* gpu) { gp0_tri_shaded_impl(gpu, false); }
static void gp0_tri_shaded_semi(Gpu* gpu)   { gp0_tri_shaded_impl(gpu, true);  }

// ---------------------------------------------------------------------------
// Shaded+Textured Triangle (0x34–0x37)
// Words: c0/cmd, v0, uv0+clut, c1, v1, uv1+tpage, c2, v2, uv2
// ---------------------------------------------------------------------------
static void gp0_tri_shaded_tex_impl(Gpu* gpu, bool semi_trans) {
    if (gpu->gp0_command_buffer.count < 9) return;
    RendererColor c[3]; RendererPosition p[3]; RendererTexCoord t[3];
    uint16_t clut = 0, texpage = 0;
    // v0
    c[0].r = (GLubyte)(gpu->gp0_command_buffer.buffer[0] & 0xFF);
    c[0].g = (GLubyte)((gpu->gp0_command_buffer.buffer[0] >> 8) & 0xFF);
    c[0].b = (GLubyte)((gpu->gp0_command_buffer.buffer[0] >> 16) & 0xFF);
    p[0].x = (GLshort)(int16_t)(gpu->gp0_command_buffer.buffer[1] & 0xFFFF);
    p[0].y = (GLshort)(int16_t)(gpu->gp0_command_buffer.buffer[1] >> 16);
    t[0].u = (GLshort)(gpu->gp0_command_buffer.buffer[2] & 0xFF);
    t[0].v = (GLshort)((gpu->gp0_command_buffer.buffer[2] >> 8) & 0xFF);
    clut   = (uint16_t)(gpu->gp0_command_buffer.buffer[2] >> 16);
    // v1
    c[1].r = (GLubyte)(gpu->gp0_command_buffer.buffer[3] & 0xFF);
    c[1].g = (GLubyte)((gpu->gp0_command_buffer.buffer[3] >> 8) & 0xFF);
    c[1].b = (GLubyte)((gpu->gp0_command_buffer.buffer[3] >> 16) & 0xFF);
    p[1].x = (GLshort)(int16_t)(gpu->gp0_command_buffer.buffer[4] & 0xFFFF);
    p[1].y = (GLshort)(int16_t)(gpu->gp0_command_buffer.buffer[4] >> 16);
    t[1].u = (GLshort)(gpu->gp0_command_buffer.buffer[5] & 0xFF);
    t[1].v = (GLshort)((gpu->gp0_command_buffer.buffer[5] >> 8) & 0xFF);
    texpage = (uint16_t)(gpu->gp0_command_buffer.buffer[5] >> 16);
    // v2
    c[2].r = (GLubyte)(gpu->gp0_command_buffer.buffer[6] & 0xFF);
    c[2].g = (GLubyte)((gpu->gp0_command_buffer.buffer[6] >> 8) & 0xFF);
    c[2].b = (GLubyte)((gpu->gp0_command_buffer.buffer[6] >> 16) & 0xFF);
    p[2].x = (GLshort)(int16_t)(gpu->gp0_command_buffer.buffer[7] & 0xFFFF);
    p[2].y = (GLshort)(int16_t)(gpu->gp0_command_buffer.buffer[7] >> 16);
    t[2].u = (GLshort)(gpu->gp0_command_buffer.buffer[8] & 0xFF);
    t[2].v = (GLshort)((gpu->gp0_command_buffer.buffer[8] >> 8) & 0xFF);

    upload_vram_if_dirty(gpu);
    renderer_set_semi_trans_mode(&gpu->renderer, semi_trans, gpu->semi_transparency);
    renderer_set_raw_texture_mode(&gpu->renderer, false);
    renderer_set_texture_mode(&gpu->renderer, true);
    renderer_push_triangle(&gpu->renderer, p, c, t, clut, texpage);
}

static void gp0_tri_shaded_tex_blend_opaque(Gpu* gpu) { gp0_tri_shaded_tex_impl(gpu, false); }
static void gp0_tri_shaded_tex_blend_semi(Gpu* gpu)   { gp0_tri_shaded_tex_impl(gpu, true);  }

// ---------------------------------------------------------------------------
// Monochrome Quad (0x28–0x2B)
// ---------------------------------------------------------------------------
static void gp0_quad_mono_impl(Gpu* gpu, bool semi_trans) {
    if (gpu->gp0_command_buffer.count < 5) return;
    RendererColor col;
    col.r = (GLubyte)(gpu->gp0_command_buffer.buffer[0] & 0xFF);
    col.g = (GLubyte)((gpu->gp0_command_buffer.buffer[0] >> 8) & 0xFF);
    col.b = (GLubyte)((gpu->gp0_command_buffer.buffer[0] >> 16) & 0xFF);
    RendererColor colors[4] = {col, col, col, col};
    RendererPosition p[4];
    for (int i = 0; i < 4; i++) {
        uint32_t v = gpu->gp0_command_buffer.buffer[i + 1];
        p[i].x = (GLshort)(int16_t)(v & 0xFFFF);
        p[i].y = (GLshort)(int16_t)(v >> 16);
    }
    renderer_set_semi_trans_mode(&gpu->renderer, semi_trans, gpu->semi_transparency);
    renderer_set_texture_mode(&gpu->renderer, false);
    renderer_push_quad(&gpu->renderer, p, colors, NULL, 0, 0);
}

static void gp0_quad_mono_opaque(Gpu* gpu) { gp0_quad_mono_impl(gpu, false); }
static void gp0_quad_mono_semi(Gpu* gpu)   { gp0_quad_mono_impl(gpu, true);  }

// ---------------------------------------------------------------------------
// Textured Quad (0x2C–0x2F)
// Words: cmd, v0, uv0+clut, v1, uv1+tpage, v2, uv2, v3, uv3
// ---------------------------------------------------------------------------
static void gp0_quad_tex_impl(Gpu* gpu, bool semi_trans, bool raw_texture) {
    if (gpu->gp0_command_buffer.count < 9) return;
    RendererColor col;
    col.r = (GLubyte)(gpu->gp0_command_buffer.buffer[0] & 0xFF);
    col.g = (GLubyte)((gpu->gp0_command_buffer.buffer[0] >> 8) & 0xFF);
    col.b = (GLubyte)((gpu->gp0_command_buffer.buffer[0] >> 16) & 0xFF);
    RendererColor c[4] = {col, col, col, col};
    RendererPosition p[4]; RendererTexCoord t[4];
    uint16_t clut = 0, texpage = 0;

    p[0].x = (GLshort)(int16_t)(gpu->gp0_command_buffer.buffer[1] & 0xFFFF);
    p[0].y = (GLshort)(int16_t)(gpu->gp0_command_buffer.buffer[1] >> 16);
    t[0].u = (GLshort)(gpu->gp0_command_buffer.buffer[2] & 0xFF);
    t[0].v = (GLshort)((gpu->gp0_command_buffer.buffer[2] >> 8) & 0xFF);
    clut   = (uint16_t)(gpu->gp0_command_buffer.buffer[2] >> 16);

    p[1].x = (GLshort)(int16_t)(gpu->gp0_command_buffer.buffer[3] & 0xFFFF);
    p[1].y = (GLshort)(int16_t)(gpu->gp0_command_buffer.buffer[3] >> 16);
    t[1].u = (GLshort)(gpu->gp0_command_buffer.buffer[4] & 0xFF);
    t[1].v = (GLshort)((gpu->gp0_command_buffer.buffer[4] >> 8) & 0xFF);
    texpage = (uint16_t)(gpu->gp0_command_buffer.buffer[4] >> 16);

    p[2].x = (GLshort)(int16_t)(gpu->gp0_command_buffer.buffer[5] & 0xFFFF);
    p[2].y = (GLshort)(int16_t)(gpu->gp0_command_buffer.buffer[5] >> 16);
    t[2].u = (GLshort)(gpu->gp0_command_buffer.buffer[6] & 0xFF);
    t[2].v = (GLshort)((gpu->gp0_command_buffer.buffer[6] >> 8) & 0xFF);

    p[3].x = (GLshort)(int16_t)(gpu->gp0_command_buffer.buffer[7] & 0xFFFF);
    p[3].y = (GLshort)(int16_t)(gpu->gp0_command_buffer.buffer[7] >> 16);
    t[3].u = (GLshort)(gpu->gp0_command_buffer.buffer[8] & 0xFF);
    t[3].v = (GLshort)((gpu->gp0_command_buffer.buffer[8] >> 8) & 0xFF);

    static int log_limiter = 0;
    if (log_limiter < 10) {
        LOG_GPU_DEBUG("GP0 textured quad V0(%d,%d) UV(%d,%d) CLUT=%04x TPage=%04x",
            p[0].x, p[0].y, t[0].u, t[0].v, clut, texpage);
        log_limiter++;
    }

    upload_vram_if_dirty(gpu);
    renderer_set_semi_trans_mode(&gpu->renderer, semi_trans, gpu->semi_transparency);
    renderer_set_raw_texture_mode(&gpu->renderer, raw_texture);
    renderer_set_texture_mode(&gpu->renderer, true);
    renderer_push_quad(&gpu->renderer, p, c, t, clut, texpage);
}

static void gp0_quad_tex_blend_opaque(Gpu* gpu) { gp0_quad_tex_impl(gpu, false, false); }
static void gp0_quad_tex_blend_semi(Gpu* gpu)   { gp0_quad_tex_impl(gpu, true,  false); }
static void gp0_quad_tex_raw_opaque(Gpu* gpu)   { gp0_quad_tex_impl(gpu, false, true);  }
static void gp0_quad_tex_raw_semi(Gpu* gpu)     { gp0_quad_tex_impl(gpu, true,  true);  }

// ---------------------------------------------------------------------------
// Shaded Quad (0x38–0x3B)
// ---------------------------------------------------------------------------
static void gp0_quad_shaded_impl(Gpu* gpu, bool semi_trans) {
    if (gpu->gp0_command_buffer.count < 8) return;
    RendererColor c[4]; RendererPosition p[4];
    for (int i = 0; i < 4; i++) {
        uint32_t cw = gpu->gp0_command_buffer.buffer[i * 2];
        uint32_t vw = gpu->gp0_command_buffer.buffer[i * 2 + 1];
        c[i].r = (GLubyte)(cw & 0xFF);
        c[i].g = (GLubyte)((cw >> 8) & 0xFF);
        c[i].b = (GLubyte)((cw >> 16) & 0xFF);
        p[i].x = (GLshort)(int16_t)(vw & 0xFFFF);
        p[i].y = (GLshort)(int16_t)(vw >> 16);
    }
    renderer_set_semi_trans_mode(&gpu->renderer, semi_trans, gpu->semi_transparency);
    renderer_set_texture_mode(&gpu->renderer, false);
    renderer_push_quad(&gpu->renderer, p, c, NULL, 0, 0);
}

static void gp0_quad_shaded_opaque(Gpu* gpu) { gp0_quad_shaded_impl(gpu, false); }
static void gp0_quad_shaded_semi(Gpu* gpu)   { gp0_quad_shaded_impl(gpu, true);  }

// ---------------------------------------------------------------------------
// Shaded+Textured Quad (0x3C–0x3F)
// Words: c0/cmd, v0, uv0+clut, c1, v1, uv1+tpage, c2, v2, uv2, c3, v3, uv3
// ---------------------------------------------------------------------------
static void gp0_quad_shaded_tex_impl(Gpu* gpu, bool semi_trans) {
    if (gpu->gp0_command_buffer.count < 12) return;
    RendererColor c[4]; RendererPosition p[4]; RendererTexCoord t[4];
    uint16_t clut = 0, texpage = 0;
    for (int i = 0; i < 4; i++) {
        c[i].r = (GLubyte)(gpu->gp0_command_buffer.buffer[i * 3] & 0xFF);
        c[i].g = (GLubyte)((gpu->gp0_command_buffer.buffer[i * 3] >> 8) & 0xFF);
        c[i].b = (GLubyte)((gpu->gp0_command_buffer.buffer[i * 3] >> 16) & 0xFF);
        p[i].x = (GLshort)(int16_t)(gpu->gp0_command_buffer.buffer[i * 3 + 1] & 0xFFFF);
        p[i].y = (GLshort)(int16_t)(gpu->gp0_command_buffer.buffer[i * 3 + 1] >> 16);
        t[i].u = (GLshort)(gpu->gp0_command_buffer.buffer[i * 3 + 2] & 0xFF);
        t[i].v = (GLshort)((gpu->gp0_command_buffer.buffer[i * 3 + 2] >> 8) & 0xFF);
        if (i == 0) clut    = (uint16_t)(gpu->gp0_command_buffer.buffer[2] >> 16);
        if (i == 1) texpage = (uint16_t)(gpu->gp0_command_buffer.buffer[5] >> 16);
    }
    upload_vram_if_dirty(gpu);
    renderer_set_semi_trans_mode(&gpu->renderer, semi_trans, gpu->semi_transparency);
    renderer_set_raw_texture_mode(&gpu->renderer, false);
    renderer_set_texture_mode(&gpu->renderer, true);
    renderer_push_quad(&gpu->renderer, p, c, t, clut, texpage);
}

static void gp0_quad_shaded_tex_blend_opaque(Gpu* gpu) { gp0_quad_shaded_tex_impl(gpu, false); }
static void gp0_quad_shaded_tex_blend_semi(Gpu* gpu)   { gp0_quad_shaded_tex_impl(gpu, true);  }

// ---------------------------------------------------------------------------
// Line commands (0x40–0x5F)
// ---------------------------------------------------------------------------

// Monochrome line: cmd+color, v0, v1  (3 words total)
static void gp0_line_mono_impl(Gpu* gpu, bool semi_trans) {
    if (gpu->gp0_command_buffer.count < 3) return;
    RendererColor col;
    col.r = (GLubyte)(gpu->gp0_command_buffer.buffer[0] & 0xFF);
    col.g = (GLubyte)((gpu->gp0_command_buffer.buffer[0] >> 8) & 0xFF);
    col.b = (GLubyte)((gpu->gp0_command_buffer.buffer[0] >> 16) & 0xFF);
    RendererPosition p[2];
    RendererColor c[2] = {col, col};
    p[0].x = (GLshort)(int16_t)(gpu->gp0_command_buffer.buffer[1] & 0xFFFF);
    p[0].y = (GLshort)(int16_t)(gpu->gp0_command_buffer.buffer[1] >> 16);
    p[1].x = (GLshort)(int16_t)(gpu->gp0_command_buffer.buffer[2] & 0xFFFF);
    p[1].y = (GLshort)(int16_t)(gpu->gp0_command_buffer.buffer[2] >> 16);
    renderer_set_semi_trans_mode(&gpu->renderer, semi_trans, gpu->semi_transparency);
    renderer_push_line(&gpu->renderer, p, c);
}

static void gp0_line_mono_opaque(Gpu* gpu) { gp0_line_mono_impl(gpu, false); }
static void gp0_line_mono_semi(Gpu* gpu)   { gp0_line_mono_impl(gpu, true);  }

// Shaded line: cmd+c0, v0, c1, v1  (4 words total)
static void gp0_line_shaded_impl(Gpu* gpu, bool semi_trans) {
    if (gpu->gp0_command_buffer.count < 4) return;
    RendererPosition p[2];
    RendererColor c[2];
    c[0].r = (GLubyte)(gpu->gp0_command_buffer.buffer[0] & 0xFF);
    c[0].g = (GLubyte)((gpu->gp0_command_buffer.buffer[0] >> 8) & 0xFF);
    c[0].b = (GLubyte)((gpu->gp0_command_buffer.buffer[0] >> 16) & 0xFF);
    p[0].x = (GLshort)(int16_t)(gpu->gp0_command_buffer.buffer[1] & 0xFFFF);
    p[0].y = (GLshort)(int16_t)(gpu->gp0_command_buffer.buffer[1] >> 16);
    c[1].r = (GLubyte)(gpu->gp0_command_buffer.buffer[2] & 0xFF);
    c[1].g = (GLubyte)((gpu->gp0_command_buffer.buffer[2] >> 8) & 0xFF);
    c[1].b = (GLubyte)((gpu->gp0_command_buffer.buffer[2] >> 16) & 0xFF);
    p[1].x = (GLshort)(int16_t)(gpu->gp0_command_buffer.buffer[3] & 0xFFFF);
    p[1].y = (GLshort)(int16_t)(gpu->gp0_command_buffer.buffer[3] >> 16);
    renderer_set_semi_trans_mode(&gpu->renderer, semi_trans, gpu->semi_transparency);
    renderer_push_line(&gpu->renderer, p, c);
}

static void gp0_line_shaded_opaque(Gpu* gpu) { gp0_line_shaded_impl(gpu, false); }
static void gp0_line_shaded_semi(Gpu* gpu)   { gp0_line_shaded_impl(gpu, true);  }

// Polyline initiators — actual data consumed in the handle_word state machine
static void gp0_polyline_mono_opaque(Gpu* gpu)   {
    gpu->polyline_shaded = false; gpu->polyline_semi_trans = false;
    gpu->polyline_buffer[0] = gpu->gp0_command_buffer.buffer[0];
    gpu->polyline_count = 1;
    gpu->gp0_mode = GP0_MODE_POLYLINE;
}
static void gp0_polyline_mono_semi(Gpu* gpu)    {
    gpu->polyline_shaded = false; gpu->polyline_semi_trans = true;
    gpu->polyline_buffer[0] = gpu->gp0_command_buffer.buffer[0];
    gpu->polyline_count = 1;
    gpu->gp0_mode = GP0_MODE_POLYLINE;
}
static void gp0_polyline_shaded_opaque(Gpu* gpu) {
    gpu->polyline_shaded = true; gpu->polyline_semi_trans = false;
    gpu->polyline_buffer[0] = gpu->gp0_command_buffer.buffer[0];
    gpu->polyline_count = 1;
    gpu->gp0_mode = GP0_MODE_POLYLINE;
}
static void gp0_polyline_shaded_semi(Gpu* gpu) {
    gpu->polyline_shaded = true; gpu->polyline_semi_trans = true;
    gpu->polyline_buffer[0] = gpu->gp0_command_buffer.buffer[0];
    gpu->polyline_count = 1;
    gpu->gp0_mode = GP0_MODE_POLYLINE;
}

// Flush accumulated polyline vertices as line segments
static void flush_polyline(Gpu* gpu) {
    if (gpu->polyline_count < 3) { // Need at least 1 segment (color, v0, v1)
        gpu->polyline_count = 0;
        gpu->gp0_mode = GP0_MODE_COMMAND;
        return;
    }

    renderer_set_semi_trans_mode(&gpu->renderer, gpu->polyline_semi_trans,
                                  gpu->semi_transparency);

    if (!gpu->polyline_shaded) {
        // Monochrome: buf[0]=cmd+color, buf[1]=v0, buf[2]=v1, buf[3]=v2, ...
        RendererColor col;
        col.r = (GLubyte)(gpu->polyline_buffer[0] & 0xFF);
        col.g = (GLubyte)((gpu->polyline_buffer[0] >> 8) & 0xFF);
        col.b = (GLubyte)((gpu->polyline_buffer[0] >> 16) & 0xFF);
        for (uint32_t i = 1; i + 1 < gpu->polyline_count; i++) {
            RendererPosition p[2];
            RendererColor c[2] = {col, col};
            p[0].x = (GLshort)(int16_t)(gpu->polyline_buffer[i] & 0xFFFF);
            p[0].y = (GLshort)(int16_t)(gpu->polyline_buffer[i] >> 16);
            p[1].x = (GLshort)(int16_t)(gpu->polyline_buffer[i + 1] & 0xFFFF);
            p[1].y = (GLshort)(int16_t)(gpu->polyline_buffer[i + 1] >> 16);
            renderer_push_line(&gpu->renderer, p, c);
        }
    } else {
        // Shaded: buf[0]=cmd+c0, buf[1]=v0, buf[2]=c1, buf[3]=v1, buf[4]=c2, buf[5]=v2, ...
        // Segment k: c0=buf[2k], v0=buf[2k+1], c1=buf[2k+2], v1=buf[2k+3]
        for (uint32_t k = 0; k * 2 + 3 < gpu->polyline_count; k++) {
            uint32_t c0_idx = k * 2;
            uint32_t v0_idx = k * 2 + 1;
            uint32_t c1_idx = k * 2 + 2;
            uint32_t v1_idx = k * 2 + 3;
            RendererPosition p[2];
            RendererColor c[2];
            c[0].r = (GLubyte)(gpu->polyline_buffer[c0_idx] & 0xFF);
            c[0].g = (GLubyte)((gpu->polyline_buffer[c0_idx] >> 8) & 0xFF);
            c[0].b = (GLubyte)((gpu->polyline_buffer[c0_idx] >> 16) & 0xFF);
            p[0].x = (GLshort)(int16_t)(gpu->polyline_buffer[v0_idx] & 0xFFFF);
            p[0].y = (GLshort)(int16_t)(gpu->polyline_buffer[v0_idx] >> 16);
            c[1].r = (GLubyte)(gpu->polyline_buffer[c1_idx] & 0xFF);
            c[1].g = (GLubyte)((gpu->polyline_buffer[c1_idx] >> 8) & 0xFF);
            c[1].b = (GLubyte)((gpu->polyline_buffer[c1_idx] >> 16) & 0xFF);
            p[1].x = (GLshort)(int16_t)(gpu->polyline_buffer[v1_idx] & 0xFFFF);
            p[1].y = (GLshort)(int16_t)(gpu->polyline_buffer[v1_idx] >> 16);
            renderer_push_line(&gpu->renderer, p, c);
        }
    }

    gpu->polyline_count = 0;
    gpu->gp0_mode = GP0_MODE_COMMAND;
}

// ---------------------------------------------------------------------------
// Rectangle commands (0x60–0x7F)
// ---------------------------------------------------------------------------

// Variable-size monochrome rect: cmd+color, vtx, w+h
static void gp0_rect_var_mono_impl(Gpu* gpu, bool semi_trans) {
    if (gpu->gp0_command_buffer.count < 3) return;
    uint32_t cmd = gpu->gp0_command_buffer.buffer[0];
    uint32_t vtx = gpu->gp0_command_buffer.buffer[1];
    uint32_t dim = gpu->gp0_command_buffer.buffer[2];
    RendererColor col = { .r=(GLubyte)(cmd&0xFF), .g=(GLubyte)((cmd>>8)&0xFF), .b=(GLubyte)((cmd>>16)&0xFF) };
    int16_t x = (int16_t)(vtx & 0xFFFF); int16_t y = (int16_t)(vtx >> 16);
    uint16_t w = (uint16_t)(dim & 0xFFFF); uint16_t h = (uint16_t)(dim >> 16);
    if (w == 0) w = 1; if (h == 0) h = 1;
    draw_rectangle(gpu, x, y, w, h, col, false, false, NULL, 0, 0, semi_trans);
}

static void gp0_rect_variable_opaque(Gpu* gpu) { gp0_rect_var_mono_impl(gpu, false); }
static void gp0_rect_variable_semi(Gpu* gpu)   { gp0_rect_var_mono_impl(gpu, true);  }

// Variable-size textured rect: cmd+color, vtx, uv+clut, w+h
static void gp0_rect_var_tex_impl(Gpu* gpu, bool semi_trans, bool raw_texture) {
    if (gpu->gp0_command_buffer.count < 4) return;
    uint32_t cmd     = gpu->gp0_command_buffer.buffer[0];
    uint32_t vtx     = gpu->gp0_command_buffer.buffer[1];
    uint32_t uv_clut = gpu->gp0_command_buffer.buffer[2];
    uint32_t dim     = gpu->gp0_command_buffer.buffer[3];
    RendererColor col = { .r=(GLubyte)(cmd&0xFF), .g=(GLubyte)((cmd>>8)&0xFF), .b=(GLubyte)((cmd>>16)&0xFF) };
    int16_t x = (int16_t)(vtx & 0xFFFF); int16_t y = (int16_t)(vtx >> 16);
    RendererTexCoord tex = { .u=(GLshort)(uv_clut & 0xFF), .v=(GLshort)((uv_clut>>8)&0xFF) };
    uint16_t clut  = (uint16_t)(uv_clut >> 16);
    uint16_t tpage = make_tpage(gpu);
    uint16_t w = (uint16_t)(dim & 0xFFFF); uint16_t h = (uint16_t)(dim >> 16);
    if (w == 0) w = 1; if (h == 0) h = 1;
    upload_vram_if_dirty(gpu);
    draw_rectangle(gpu, x, y, w, h, col, true, raw_texture, &tex, clut, tpage, semi_trans);
}

static void gp0_rect_tex_variable_opaque(Gpu* gpu)     { gp0_rect_var_tex_impl(gpu, false, false); }
static void gp0_rect_tex_variable_semi(Gpu* gpu)       { gp0_rect_var_tex_impl(gpu, true,  false); }
static void gp0_rect_tex_variable_raw_opaque(Gpu* gpu) { gp0_rect_var_tex_impl(gpu, false, true);  }
static void gp0_rect_tex_variable_raw_semi(Gpu* gpu)   { gp0_rect_var_tex_impl(gpu, true,  true);  }

// Fixed-size monochrome rects: cmd+color, vtx
static void gp0_rect_fixed_mono_impl(Gpu* gpu, bool semi_trans, uint16_t size) {
    if (gpu->gp0_command_buffer.count < 2) return;
    uint32_t cmd = gpu->gp0_command_buffer.buffer[0];
    uint32_t vtx = gpu->gp0_command_buffer.buffer[1];
    RendererColor col = { .r=(GLubyte)(cmd&0xFF), .g=(GLubyte)((cmd>>8)&0xFF), .b=(GLubyte)((cmd>>16)&0xFF) };
    int16_t x = (int16_t)(vtx & 0xFFFF); int16_t y = (int16_t)(vtx >> 16);
    draw_rectangle(gpu, x, y, size, size, col, false, false, NULL, 0, 0, semi_trans);
}

static void gp0_rect_1x1_opaque(Gpu* gpu)  { gp0_rect_fixed_mono_impl(gpu, false, 1);  }
static void gp0_rect_1x1_semi(Gpu* gpu)    { gp0_rect_fixed_mono_impl(gpu, true,  1);  }
static void gp0_rect_8x8_opaque(Gpu* gpu)  { gp0_rect_fixed_mono_impl(gpu, false, 8);  }
static void gp0_rect_8x8_semi(Gpu* gpu)    { gp0_rect_fixed_mono_impl(gpu, true,  8);  }
static void gp0_rect_16x16_opaque(Gpu* gpu){ gp0_rect_fixed_mono_impl(gpu, false, 16); }
static void gp0_rect_16x16_semi(Gpu* gpu)  { gp0_rect_fixed_mono_impl(gpu, true,  16); }

// Fixed-size textured rects: cmd+color, vtx, uv+clut
static void gp0_rect_fixed_tex_impl(Gpu* gpu, bool semi_trans, bool raw_texture, uint16_t size) {
    if (gpu->gp0_command_buffer.count < 3) return;
    uint32_t cmd     = gpu->gp0_command_buffer.buffer[0];
    uint32_t vtx     = gpu->gp0_command_buffer.buffer[1];
    uint32_t uv_clut = gpu->gp0_command_buffer.buffer[2];
    RendererColor col = { .r=(GLubyte)(cmd&0xFF), .g=(GLubyte)((cmd>>8)&0xFF), .b=(GLubyte)((cmd>>16)&0xFF) };
    int16_t x = (int16_t)(vtx & 0xFFFF); int16_t y = (int16_t)(vtx >> 16);
    RendererTexCoord tex = { .u=(GLshort)(uv_clut & 0xFF), .v=(GLshort)((uv_clut>>8)&0xFF) };
    uint16_t clut  = (uint16_t)(uv_clut >> 16);
    uint16_t tpage = make_tpage(gpu);
    upload_vram_if_dirty(gpu);
    draw_rectangle(gpu, x, y, size, size, col, true, raw_texture, &tex, clut, tpage, semi_trans);
}

static void gp0_rect_tex_1x1_opaque(Gpu* gpu)  { gp0_rect_fixed_tex_impl(gpu, false, false, 1);  }
static void gp0_rect_tex_1x1_semi(Gpu* gpu)    { gp0_rect_fixed_tex_impl(gpu, true,  false, 1);  }
static void gp0_rect_tex_1x1_raw(Gpu* gpu)     { gp0_rect_fixed_tex_impl(gpu, false, true,  1);  }
static void gp0_rect_tex_1x1_raw_semi(Gpu* gpu){ gp0_rect_fixed_tex_impl(gpu, true,  true,  1);  }
static void gp0_rect_tex_8x8_opaque(Gpu* gpu)  { gp0_rect_fixed_tex_impl(gpu, false, false, 8);  }
static void gp0_rect_tex_8x8_semi(Gpu* gpu)    { gp0_rect_fixed_tex_impl(gpu, true,  false, 8);  }
static void gp0_rect_tex_8x8_raw(Gpu* gpu)     { gp0_rect_fixed_tex_impl(gpu, false, true,  8);  }
static void gp0_rect_tex_8x8_raw_semi(Gpu* gpu){ gp0_rect_fixed_tex_impl(gpu, true,  true,  8);  }
static void gp0_rect_tex_16x16_opaque(Gpu* gpu)  { gp0_rect_fixed_tex_impl(gpu, false, false, 16); }
static void gp0_rect_tex_16x16_semi(Gpu* gpu)    { gp0_rect_fixed_tex_impl(gpu, true,  false, 16); }
static void gp0_rect_tex_16x16_raw(Gpu* gpu)     { gp0_rect_fixed_tex_impl(gpu, false, true,  16); }
static void gp0_rect_tex_16x16_raw_semi(Gpu* gpu){ gp0_rect_fixed_tex_impl(gpu, true,  true,  16); }

// ---------------------------------------------------------------------------
// VRAM ops (0x80, 0xA0, 0xC0)
// ---------------------------------------------------------------------------

/** GP0(0x80): Copy Rectangle (VRAM to VRAM) */
static void gp0_copy_rectangle(Gpu* gpu) {
    if (gpu->gp0_command_buffer.count < 4) {
        LOG_GPU_ERROR("GP0(0x80) Error: Expected 4 words, got %u", gpu->gp0_command_buffer.count);
        return;
    }
    uint32_t src_val = gpu->gp0_command_buffer.buffer[1];
    uint32_t dst_val = gpu->gp0_command_buffer.buffer[2];
    uint32_t dim_val = gpu->gp0_command_buffer.buffer[3];

    uint16_t src_x = (uint16_t)(src_val & 0x3FF);
    uint16_t src_y = (uint16_t)((src_val >> 16) & 0x1FF);
    uint16_t dst_x = (uint16_t)(dst_val & 0x3FF);
    uint16_t dst_y = (uint16_t)((dst_val >> 16) & 0x1FF);
    uint16_t w     = (uint16_t)(dim_val & 0x3FF);
    uint16_t h     = (uint16_t)((dim_val >> 16) & 0x1FF);
    if (w == 0) w = 1024;
    if (h == 0) h = 512;

    LOG_GPU_DEBUG("GP0(0x80): VRAM Copy (%u,%u)->(%u,%u) %ux%u", src_x, src_y, dst_x, dst_y, w, h);

    // Overlap-safe directional copy
    int16_t step_x = 1, step_y = 1;
    int16_t start_x = 0, start_y = 0, end_x = w, end_y = h;
    if (dst_y > src_y) { step_y = -1; start_y = h - 1; end_y = -1; }
    if (dst_x > src_x) { step_x = -1; start_x = w - 1; end_x = -1; }

    for (int16_t y = start_y; y != end_y; y += step_y) {
        for (int16_t x = start_x; x != end_x; x += step_x) {
            uint16_t sx = (src_x + x) & 0x3FF;
            uint16_t sy = (src_y + y) & 0x1FF;
            uint16_t dx = (dst_x + x) & 0x3FF;
            uint16_t dy = (dst_y + y) & 0x1FF;
            uint32_t soff = (uint32_t)sy * VRAM_WIDTH * VRAM_BPP + (uint32_t)sx * VRAM_BPP;
            uint32_t doff = (uint32_t)dy * VRAM_WIDTH * VRAM_BPP + (uint32_t)dx * VRAM_BPP;
            uint16_t pixel = vram_load16(&gpu->vram, soff);
            vram_write_masked(gpu, doff, pixel);
        }
    }
    gpu->vram_dirty = true;
    renderer_upload_vram(&gpu->renderer, (const uint16_t*)gpu->vram.data);
    gpu->vram_dirty = false;
}

/** GP0(0xA0): Copy Rectangle CPU/DMA → VRAM (setup) */
static void gp0_image_load(Gpu* gpu) {
    if (gpu->gp0_command_buffer.count < 3) {
        LOG_GPU_ERROR("GP0(0xA0) Error: Expected 3 words, got %u", gpu->gp0_command_buffer.count);
        return;
    }
    uint32_t pos = gpu->gp0_command_buffer.buffer[1];
    uint32_t dim = gpu->gp0_command_buffer.buffer[2];
    gpu->vram_load_x = (uint16_t)(pos & 0x3FF);
    gpu->vram_load_y = (uint16_t)((pos >> 16) & 0x1FF);
    gpu->vram_load_w = (uint16_t)(dim & 0x3FF);
    gpu->vram_load_h = (uint16_t)((dim >> 16) & 0x1FF);
    if (gpu->vram_load_w == 0) gpu->vram_load_w = 1024;
    if (gpu->vram_load_h == 0) gpu->vram_load_h = 512;
    if (gpu->vram_load_w > VRAM_WIDTH)  gpu->vram_load_w = VRAM_WIDTH;
    if (gpu->vram_load_h > VRAM_HEIGHT) gpu->vram_load_h = VRAM_HEIGHT;

    uint32_t pixels = (uint32_t)gpu->vram_load_w * gpu->vram_load_h;
    uint32_t words  = (pixels + 1) & ~1; words >>= 1;

    LOG_GPU_DEBUG("GP0(0xA0): VRAM UPLOAD START (%u,%u) %ux%u = %u words",
                 gpu->vram_load_x, gpu->vram_load_y,
                 gpu->vram_load_w, gpu->vram_load_h, words);

    if (words == 0) {
        gpu->gp0_words_remaining = 0;
        gpu->gp0_mode = GP0_MODE_COMMAND;
        return;
    }
    gpu->gp0_words_remaining = words;
    gpu->gp0_mode = GP0_MODE_IMAGE_LOAD;
    gpu->vram_load_count = 0;
}

/** GP0(0xC0): Copy Rectangle VRAM → CPU (setup) */
static void gp0_image_store(Gpu* gpu) {
    if (gpu->gp0_command_buffer.count < 3) {
        LOG_GPU_ERROR("GP0(0xC0) Error: Expected 3 words, got %u", gpu->gp0_command_buffer.count);
        return;
    }
    uint32_t val1 = gpu->gp0_command_buffer.buffer[1];
    uint32_t val2 = gpu->gp0_command_buffer.buffer[2];
    uint16_t x = (uint16_t)(val1 & 0x3FF);
    uint16_t y = (uint16_t)((val1 >> 16) & 0x1FF);
    uint16_t w = (uint16_t)(val2 & 0xFFFF);
    uint16_t h = (uint16_t)(val2 >> 16);
    if (x >= VRAM_WIDTH)  x = VRAM_WIDTH - 1;
    if (y >= VRAM_HEIGHT) y = VRAM_HEIGHT - 1;
    if (w == 0 || w > VRAM_WIDTH)  w = VRAM_WIDTH;
    if (h == 0 || h > VRAM_HEIGHT) h = VRAM_HEIGHT;
    gpu->vram_load_x = x; gpu->vram_load_y = y;
    gpu->vram_load_w = w; gpu->vram_load_h = h;
    uint32_t words = ((uint32_t)w * h + 1) / 2;
    gpu->gp0_words_remaining = words;
    gpu->vram_load_count = 0;
    gpu->gp0_mode = GP0_MODE_IMAGE_STORE;
    LOG_GPU_DEBUG("GP0(0xC0): VRAM\xE2\x86\x92CPU START (%u,%u) %ux%u = %u words", x, y, w, h, words);
}

// ---------------------------------------------------------------------------
// GP0 Command Dispatch Table
// ---------------------------------------------------------------------------

typedef struct {
    uint8_t  words;         // Number of command words (including the first)
    void (*handler)(Gpu*);  // Handler function
} Gp0TableEntry;

// Polyline mode: 0xFF means "enter polyline mode" — handled specially
#define POLY_WORDS 0xFF

static const Gp0TableEntry gp0_table[256] = {
    // NOP/Cache
    [0x00] = {1, gp0_nop},
    [0x01] = {1, gp0_clear_cache},
    [0x02] = {3, gp0_fill_rectangle},
    [0x1F] = {1, gp0_interrupt_request},

    // Monochrome poly (flat: 4 words for tri, 5 for quad)
    [0x20] = {4, gp0_tri_mono_opaque},
    [0x21] = {4, gp0_tri_mono_opaque},
    [0x22] = {4, gp0_tri_mono_semi},
    [0x23] = {4, gp0_tri_mono_semi},
    [0x28] = {5, gp0_quad_mono_opaque},
    [0x29] = {5, gp0_quad_mono_opaque},
    [0x2A] = {5, gp0_quad_mono_semi},
    [0x2B] = {5, gp0_quad_mono_semi},

    // Textured tri (7 words)
    [0x24] = {7, gp0_tri_tex_blend_opaque},
    [0x25] = {7, gp0_tri_tex_raw_opaque},
    [0x26] = {7, gp0_tri_tex_blend_semi},
    [0x27] = {7, gp0_tri_tex_raw_semi},

    // Textured quad (9 words)
    [0x2C] = {9, gp0_quad_tex_blend_opaque},
    [0x2D] = {9, gp0_quad_tex_raw_opaque},
    [0x2E] = {9, gp0_quad_tex_blend_semi},
    [0x2F] = {9, gp0_quad_tex_raw_semi},

    // Shaded tri (6 words: c0/cmd, v0, c1, v1, c2, v2)
    [0x30] = {6, gp0_tri_shaded_opaque},
    [0x31] = {6, gp0_tri_shaded_opaque},
    [0x32] = {6, gp0_tri_shaded_semi},
    [0x33] = {6, gp0_tri_shaded_semi},

    // Shaded+textured tri (9 words)
    [0x34] = {9, gp0_tri_shaded_tex_blend_opaque},
    [0x35] = {9, gp0_tri_shaded_tex_blend_opaque},
    [0x36] = {9, gp0_tri_shaded_tex_blend_semi},
    [0x37] = {9, gp0_tri_shaded_tex_blend_semi},

    // Shaded quad (8 words)
    [0x38] = {8, gp0_quad_shaded_opaque},
    [0x39] = {8, gp0_quad_shaded_opaque},
    [0x3A] = {8, gp0_quad_shaded_semi},
    [0x3B] = {8, gp0_quad_shaded_semi},

    // Shaded+textured quad (12 words)
    [0x3C] = {12, gp0_quad_shaded_tex_blend_opaque},
    [0x3D] = {12, gp0_quad_shaded_tex_blend_opaque},
    [0x3E] = {12, gp0_quad_shaded_tex_blend_semi},
    [0x3F] = {12, gp0_quad_shaded_tex_blend_semi},

    // Lines (monochrome: 3 words, shaded: 4 words)
    [0x40] = {3, gp0_line_mono_opaque},
    [0x41] = {3, gp0_line_mono_opaque},
    [0x42] = {3, gp0_line_mono_semi},
    [0x43] = {3, gp0_line_mono_semi},
    [0x50] = {4, gp0_line_shaded_opaque},
    [0x51] = {4, gp0_line_shaded_opaque},
    [0x52] = {4, gp0_line_shaded_semi},
    [0x53] = {4, gp0_line_shaded_semi},

    // Polylines (variable length: 1 word for initial entry then handled by state machine)
    [0x48] = {1, gp0_polyline_mono_opaque},
    [0x49] = {1, gp0_polyline_mono_opaque},
    [0x4A] = {1, gp0_polyline_mono_semi},
    [0x4B] = {1, gp0_polyline_mono_semi},
    [0x58] = {1, gp0_polyline_shaded_opaque},
    [0x59] = {1, gp0_polyline_shaded_opaque},
    [0x5A] = {1, gp0_polyline_shaded_semi},
    [0x5B] = {1, gp0_polyline_shaded_semi},

    // Variable rect (mono: 3 words, textured: 4 words)
    [0x60] = {3, gp0_rect_variable_opaque},
    [0x61] = {3, gp0_rect_variable_opaque},
    [0x62] = {3, gp0_rect_variable_semi},
    [0x63] = {3, gp0_rect_variable_semi},
    [0x64] = {4, gp0_rect_tex_variable_opaque},
    [0x65] = {4, gp0_rect_tex_variable_raw_opaque},
    [0x66] = {4, gp0_rect_tex_variable_semi},
    [0x67] = {4, gp0_rect_tex_variable_raw_semi},

    // 1x1 rect
    [0x68] = {2, gp0_rect_1x1_opaque},
    [0x69] = {2, gp0_rect_1x1_opaque},
    [0x6A] = {2, gp0_rect_1x1_semi},
    [0x6B] = {2, gp0_rect_1x1_semi},
    [0x6C] = {3, gp0_rect_tex_1x1_opaque},
    [0x6D] = {3, gp0_rect_tex_1x1_raw},
    [0x6E] = {3, gp0_rect_tex_1x1_semi},
    [0x6F] = {3, gp0_rect_tex_1x1_raw_semi},

    // 8x8 rect
    [0x70] = {2, gp0_rect_8x8_opaque},
    [0x71] = {2, gp0_rect_8x8_opaque},
    [0x72] = {2, gp0_rect_8x8_semi},
    [0x73] = {2, gp0_rect_8x8_semi},
    [0x74] = {3, gp0_rect_tex_8x8_opaque},
    [0x75] = {3, gp0_rect_tex_8x8_raw},
    [0x76] = {3, gp0_rect_tex_8x8_semi},
    [0x77] = {3, gp0_rect_tex_8x8_raw_semi},

    // 16x16 rect
    [0x78] = {2, gp0_rect_16x16_opaque},
    [0x79] = {2, gp0_rect_16x16_opaque},
    [0x7A] = {2, gp0_rect_16x16_semi},
    [0x7B] = {2, gp0_rect_16x16_semi},
    [0x7C] = {3, gp0_rect_tex_16x16_opaque},
    [0x7D] = {3, gp0_rect_tex_16x16_raw},
    [0x7E] = {3, gp0_rect_tex_16x16_semi},
    [0x7F] = {3, gp0_rect_tex_16x16_raw_semi},

    // VRAM ops
    [0x80] = {4, gp0_copy_rectangle},
    [0x81] = {4, gp0_copy_rectangle},
    [0x82] = {4, gp0_copy_rectangle},
    [0x83] = {4, gp0_copy_rectangle},
    [0x84] = {4, gp0_copy_rectangle},
    [0x85] = {4, gp0_copy_rectangle},
    [0x86] = {4, gp0_copy_rectangle},
    [0x87] = {4, gp0_copy_rectangle},
    [0x88] = {4, gp0_copy_rectangle},
    [0x89] = {4, gp0_copy_rectangle},
    [0x8A] = {4, gp0_copy_rectangle},
    [0x8B] = {4, gp0_copy_rectangle},
    [0x8C] = {4, gp0_copy_rectangle},
    [0x8D] = {4, gp0_copy_rectangle},
    [0x8E] = {4, gp0_copy_rectangle},
    [0x8F] = {4, gp0_copy_rectangle},
    [0x90] = {4, gp0_copy_rectangle},
    [0x91] = {4, gp0_copy_rectangle},
    [0x92] = {4, gp0_copy_rectangle},
    [0x93] = {4, gp0_copy_rectangle},
    [0x94] = {4, gp0_copy_rectangle},
    [0x95] = {4, gp0_copy_rectangle},
    [0x96] = {4, gp0_copy_rectangle},
    [0x97] = {4, gp0_copy_rectangle},
    [0x98] = {4, gp0_copy_rectangle},
    [0x99] = {4, gp0_copy_rectangle},
    [0x9A] = {4, gp0_copy_rectangle},
    [0x9B] = {4, gp0_copy_rectangle},
    [0x9C] = {4, gp0_copy_rectangle},
    [0x9D] = {4, gp0_copy_rectangle},
    [0x9E] = {4, gp0_copy_rectangle},
    [0x9F] = {4, gp0_copy_rectangle},

    [0xA0] = {3, gp0_image_load},
    [0xC0] = {3, gp0_image_store},

    // State commands
    [0xE1] = {1, gp0_draw_mode},
    [0xE2] = {1, gp0_texture_window},
    [0xE3] = {1, gp0_drawing_area_top_left},
    [0xE4] = {1, gp0_drawing_area_bottom_right},
    [0xE5] = {1, gp0_drawing_offset},
    [0xE6] = {1, gp0_mask_bit_setting},
    // 0x03-0x1E and unhandled: NULL handler = NOP
};

// ---------------------------------------------------------------------------
// GP0 word processor (the dispatch engine)
// ---------------------------------------------------------------------------

static void gpu_gp0_handle_word(Gpu* gpu, uint32_t word) {
    static uint32_t gp0_cmd_count = 0;
    gp0_cmd_count++;

    // --- Polyline accumulation mode ---
    if (gpu->gp0_mode == GP0_MODE_POLYLINE) {
        // Terminator: 0x50005000 (or 0x55555555 for some variants)
        if ((word & 0xF000F000) == 0x50005000) {
            flush_polyline(gpu);
            return;
        }
        if (gpu->polyline_count < 256)
            gpu->polyline_buffer[gpu->polyline_count++] = word;
        return;
    }

    // --- Image load mode ---
    if (gpu->gp0_mode == GP0_MODE_IMAGE_LOAD) {
        uint16_t pixel1 = (uint16_t)(word & 0xFFFF);
        uint16_t pixel2 = (uint16_t)(word >> 16);
        uint32_t idx = gpu->vram_load_count;
        uint32_t total = (uint32_t)gpu->vram_load_w * gpu->vram_load_h;

        if (idx < total) {
            uint16_t x  = gpu->vram_load_x + (uint16_t)(idx % gpu->vram_load_w);
            uint16_t y  = gpu->vram_load_y + (uint16_t)(idx / gpu->vram_load_w);
            if (y < VRAM_HEIGHT && x < VRAM_WIDTH)
                vram_write_masked(gpu, (uint32_t)y * VRAM_WIDTH * VRAM_BPP + (uint32_t)x * VRAM_BPP, pixel1);
        }
        idx++;
        if (idx < total) {
            uint16_t x  = gpu->vram_load_x + (uint16_t)(idx % gpu->vram_load_w);
            uint16_t y  = gpu->vram_load_y + (uint16_t)(idx / gpu->vram_load_w);
            if (y < VRAM_HEIGHT && x < VRAM_WIDTH)
                vram_write_masked(gpu, (uint32_t)y * VRAM_WIDTH * VRAM_BPP + (uint32_t)x * VRAM_BPP, pixel2);
        }

        gpu->vram_load_count += 2;
        gpu->gp0_words_remaining--;
        if (gpu->gp0_words_remaining == 0) {
            gpu->gp0_mode = GP0_MODE_COMMAND;
            gpu->vram_dirty = true;
            renderer_upload_vram(&gpu->renderer, (const uint16_t*)gpu->vram.data);
            gpu->vram_dirty = false;
            LOG_GPU_DEBUG("GP0(0xA0): VRAM UPLOAD COMPLETE (%u,%u) %ux%u",
                         gpu->vram_load_x, gpu->vram_load_y,
                         gpu->vram_load_w, gpu->vram_load_h);
        }
        return;
    }

    // --- Command mode ---
    if (gpu->gp0_words_remaining == 0) {
        // Start of a new command
        uint8_t opcode = (uint8_t)(word >> 24);
        gpu->gp0_current_opcode = opcode;
        gpu_clear_cmd_buf(gpu);

        Gp0TableEntry entry = gp0_table[opcode];
        if (entry.handler == NULL) {
            // Unhandled — NOP and consume 1 word
            if (gp0_cmd_count <= 20 || gp0_cmd_count % 5000 == 0)
                LOG_GPU_WARN("Unhandled GP0 opcode 0x%02x (cmd 0x%08x) #%u", opcode, word, gp0_cmd_count);
            gpu_push_cmd_word(gpu, word);
            return;
        }

        gpu->gp0_words_remaining = entry.words;
        gpu->gp0_command_method  = entry.handler;
    }

    gpu_push_cmd_word(gpu, word);
    gpu->gp0_words_remaining--;

    if (gpu->gp0_words_remaining == 0) {
        if (gpu->gp0_command_method)
            gpu->gp0_command_method(gpu);
        if (gpu->gp0_mode == GP0_MODE_COMMAND)
            gpu_clear_cmd_buf(gpu);
    }
}

// ---------------------------------------------------------------------------
// Public GP0 entry point
// ---------------------------------------------------------------------------
void gpu_gp0(Gpu* gpu, uint32_t command) {
    // Drain FIFO when full before enqueueing
    if (gpu->gp0_fifo_count >= 16) {
        while (gpu->gp0_fifo_count > 0) {
            uint32_t w = gpu->gp0_fifo[gpu->gp0_fifo_head];
            gpu->gp0_fifo_head = (uint8_t)((gpu->gp0_fifo_head + 1) & 0x0F);
            gpu->gp0_fifo_count--;
            gpu_gp0_handle_word(gpu, w);
        }
    }
    gpu->gp0_fifo[gpu->gp0_fifo_tail] = command;
    gpu->gp0_fifo_tail = (uint8_t)((gpu->gp0_fifo_tail + 1) & 0x0F);
    gpu->gp0_fifo_count++;

    while (gpu->gp0_fifo_count > 0) {
        uint32_t w = gpu->gp0_fifo[gpu->gp0_fifo_head];
        gpu->gp0_fifo_head = (uint8_t)((gpu->gp0_fifo_head + 1) & 0x0F);
        gpu->gp0_fifo_count--;
        gpu_gp0_handle_word(gpu, w);
    }
}
