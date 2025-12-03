/**
 * gpu.c
 * Implementation of the PlayStation GPU emulation.
 * Handles GPU state, command processing (GP0/GP1), VRAM access, and rendering calls.
 */
#include "gpu.h"
#include <stdio.h>
#include <stdlib.h> // For exit()
#include <string.h> // For memset
#include "renderer.h"
#include "log.h"
#include "interconnect.h"
// vram.h is implicitly included via gpu.h

// Logging: Only use LOG_ERROR/LOG_GPU_INFO as per new log system. No per-draw logs.

// Example: Replace LOG_GPU_INFO or LOG_GPU_DEBUG for frequent register accesses and commands with LOG_GPU_TRACE or wrap in a higher debug level check.
#ifdef LOG_GPU_TRACE
#define LOG_GPU_TRACE_ENABLED 1
#else
#define LOG_GPU_TRACE_ENABLED 0
#endif

// --- Forward Declarations for GP0 Handlers (Internal linkage) ---
static void gp0_nop(Gpu* gpu);
static void gp0_clear_cache(Gpu* gpu);
static void gp0_fill_rectangle(Gpu* gpu);
static void gp0_draw_mode(Gpu* gpu);
static void gp0_texture_window(Gpu* gpu);
static void gp0_drawing_area_top_left(Gpu* gpu);
static void gp0_drawing_area_bottom_right(Gpu* gpu);
static void gp0_drawing_offset(Gpu* gpu);
static void gp0_mask_bit_setting(Gpu* gpu);
static void gp0_quad_mono_opaque(Gpu* gpu);
static void gp0_quad_texture_blend_opaque(Gpu* gpu);
static void gp0_quad_shaded_opaque(Gpu* gpu);
static void gp0_triangle_shaded_opaque(Gpu* gpu);
static void gp0_rect_variable_opaque(Gpu* gpu);
static void gp0_rect_variable_semi_trans(Gpu* gpu);
static void gp0_rect_tex_variable_opaque(Gpu* gpu);
static void gp0_rect_1x1_opaque(Gpu* gpu);
static void gp0_rect_8x8_opaque(Gpu* gpu);
static void gp0_rect_16x16_opaque(Gpu* gpu);
static void gp0_rect_tex_1x1_opaque(Gpu* gpu);
static void gp0_rect_tex_8x8_opaque(Gpu* gpu);
static void gp0_rect_tex_16x16_opaque(Gpu* gpu);
static void gp0_image_load(Gpu* gpu);
static void gp0_image_store(Gpu* gpu);

// --- Forward Declarations for GP1 Handlers (Internal linkage) ---
static void gp1_reset(Gpu* gpu, uint32_t value);
static void gp1_reset_command_buffer(Gpu* gpu, uint32_t value);
static void gp1_acknowledge_irq(Gpu* gpu, uint32_t value);
static void gp1_display_enable(Gpu* gpu, uint32_t value);
static void gp1_dma_direction(Gpu* gpu, uint32_t value);
static void gp1_display_vram_start(Gpu* gpu, uint32_t value);
static void gp1_display_horizontal_range(Gpu* gpu, uint32_t value);
static void gp1_display_vertical_range(Gpu* gpu, uint32_t value);
static void gp1_display_mode(Gpu* gpu, uint32_t value);


// --- Helper Functions ---

/**
 * @brief Clears the GP0 command buffer.
 * @param gpu Pointer to the Gpu instance.
 */
static void clear_gp0_command_buffer(Gpu* gpu) {
    gpu->gp0_command_buffer.count = 0;
    // No need to zero the buffer content itself
}

/**
 * @brief Pushes a word onto the GP0 command buffer.
 * Handles potential buffer overflow.
 * @param gpu Pointer to the Gpu instance.
 * @param word The 32-bit command word to push.
 */
static void push_gp0_command_word(Gpu* gpu, uint32_t word) {
    if (gpu->gp0_command_buffer.count >= MAX_GPU_COMMAND_WORDS) {
        LOG_GPU_ERROR("FATAL: GP0 Command Buffer Overflow! Opcode: 0x%02x", gpu->gp0_current_opcode);
        // Consider triggering a CPU exception or other error handling
        exit(EXIT_FAILURE); // Exit for now, as this indicates a major issue
    }
    gpu->gp0_command_buffer.buffer[gpu->gp0_command_buffer.count] = word;
    gpu->gp0_command_buffer.count++;
}

// --- GP1 Handler Function Definitions ---

/** GP1(0x00): Soft Reset */
static void gp1_reset(Gpu* gpu, uint32_t value) {
    LOG_GPU_DEBUG("GPU: Soft Reset (GP1 Cmd 0x00)\n");
    (void)value; // value is unused for this command
    // Only reset GPU state, do NOT clear VRAM
    gpu_soft_reset(gpu);
}

/** GP1(0x01): Reset Command Buffer */
static void gp1_reset_command_buffer(Gpu* gpu, uint32_t value) {
    LOG_GPU_INFO("GPU: Reset Command Buffer (GP1 Cmd 0x01)\n");
    (void)value; // value is unused for this command
    clear_gp0_command_buffer(gpu);
    gpu->gp0_words_remaining = 0;
    gpu->gp0_mode = GP0_MODE_COMMAND; // Reset mode
    gpu->gp0_current_opcode = 0xFF; // Reset opcode tracking
    gpu->gp0_command_method = NULL; // Reset handler pointer
    // TODO: Should also clear the internal hardware FIFO if/when implemented.
}

/** GP1(0x02): Acknowledge GPU Interrupt */
static void gp1_acknowledge_irq(Gpu* gpu, uint32_t value) {
    LOG_GPU_INFO("GPU: Acknowledge IRQ (GP1 Cmd 0x02)\n");
     (void)value; // value is unused for this command
     gpu->interrupt = false; // Clear the interrupt flag (STAT[24])
}

/** GP1(0x03): Display Enable */
static void gp1_display_enable(Gpu* gpu, uint32_t value) {
    // Bit 0: 0 = Enable Display, 1 = Disable Display
    gpu->display_disabled = (value & 1);
    LOG_GPU_INFO("GPU: Display Enable = %s (GP1 Cmd 0x03)\n", gpu->display_disabled ? "Disabled" : "Enabled");
}

/** GP1(0x04): DMA Direction / Request settings */
static void gp1_dma_direction(Gpu* gpu, uint32_t value) {
    // Bits 0-1 select mode
    switch (value & 3) {
        case 0: gpu->dma_setting = GPU_DMA_Off; break;
        case 1: gpu->dma_setting = GPU_DMA_Fifo; break;
        case 2: gpu->dma_setting = GPU_DMA_CpuToGp0; break;
        case 3: gpu->dma_setting = GPU_DMA_VRamToCpu; break;
    }
    LOG_GPU_DEBUG("GPU: DMA Direction = %d (GP1 Cmd 0x04)\n", gpu->dma_setting);
}

/** GP1(0x05): Start of Display area in VRAM */
static void gp1_display_vram_start(Gpu* gpu, uint32_t value) {
    // Bits 0-9: X start coordinate in VRAM (1024 width) - LSB ignored (must be even)
    // Bits 10-18: Y start coordinate in VRAM (512 height)
    gpu->display_vram_x_start = (uint16_t)(value & 0x3FE);
    gpu->display_vram_y_start = (uint16_t)((value >> 10) & 0x1FF);
    LOG_GPU_INFO("GPU: Display VRAM Start X=%u Y=%u (GP1 Cmd 0x05)\n",
        gpu->display_vram_x_start, gpu->display_vram_y_start);
}

/** GP1(0x06): Display Horizontal sync and display range */
static void gp1_display_horizontal_range(Gpu* gpu, uint32_t value) {
    // Bits 0-11: Hsync Start coordinate (dotclock units)
    // Bits 12-23: Hsync End coordinate (dotclock units)
    gpu->display_horiz_start = (uint16_t)(value & 0xFFF);
    gpu->display_horiz_end = (uint16_t)((value >> 12) & 0xFFF);
    LOG_GPU_INFO("GPU: Display H-Range Start=%u End=%u (GP1 Cmd 0x06)\n",
        gpu->display_horiz_start, gpu->display_horiz_end);
}

/** GP1(0x07): Display Vertical sync and display range */
static void gp1_display_vertical_range(Gpu* gpu, uint32_t value) {
    // Bits 0-9: Vsync Start coordinate (scanline units)
    // Bits 10-19: Vsync End coordinate (scanline units)
    gpu->display_line_start = (uint16_t)(value & 0x3FF);
    gpu->display_line_end = (uint16_t)((value >> 10) & 0x3FF);
    LOG_GPU_INFO("GPU: Display V-Range Start=%u End=%u (GP1 Cmd 0x07)\n",
        gpu->display_line_start, gpu->display_line_end);
}

/** GP1(0x08): Display Mode */
static void gp1_display_mode(Gpu* gpu, uint32_t value) {
    // Bits 0-1: Horizontal Resolution 1 (hr1 -> STAT[17:16])
    // Bit 6:    Horizontal Resolution 2 (hr2 -> STAT[18])
    gpu->hres_raw.hr1 = (uint8_t)(value & 3);
    gpu->hres_raw.hr2 = (uint8_t)((value >> 6) & 1);
    // Bit 2: Vertical Resolution (0=240, 1=480) -> STAT[19]
    gpu->vres = ((value >> 2) & 1) ? Y480Lines : Y240Lines;
    // Bit 3: Video Mode (0=NTSC, 1=PAL) -> STAT[20]
    gpu->vmode = ((value >> 3) & 1) ? Pal : Ntsc;
    // Bit 4: Display Area Color Depth (0=15bpp, 1=24bpp) -> STAT[21]
    gpu->display_depth = ((value >> 4) & 1) ? D24Bits : D15Bits;
    // Bit 5: Interlaced output (0=off/progressive, 1=on) -> STAT[22]
    gpu->interlaced = ((value >> 5) & 1);
    // Bit 7: Unsupported "Reverseflag"
    if ((value >> 7) & 1) {
        LOG_GPU_WARN("Warning: GPU GP1(0x08) set unsupported Reverseflag bit\n");
    }
    LOG_GPU_DEBUG("GPU: Display Mode set (GP1 Cmd 0x08)\n");
}


// --- GP0 Command Handler Definitions ---

/** GP0(0x00): No Operation */
static void gp0_nop(Gpu* gpu) {
    (void)gpu; // Does nothing
}

/** GP0(0x01): Clear Cache (Texture Cache Invalidation) */
static void gp0_clear_cache(Gpu* gpu) {
    LOG_GPU_INFO("GP0(0x01): Clear Cache (Ignoring - No texture cache implemented)\n");
    (void)gpu;
}

/** GP0(0x02): Fill Rectangle in VRAM */
static void gp0_fill_rectangle(Gpu* gpu) {
    // Minimal stub: Pretend to fill, do nothing, but don't hang
    LOG_GPU_INFO("GP0(0x02): Fill Rectangle (Stubbed, no-op)\n");
    (void)gpu;
}

/** GP0(0xE1): Set Draw Mode */
static void gp0_draw_mode(Gpu* gpu) {
    uint32_t value = gpu->gp0_command_buffer.buffer[0];
    gpu->page_base_x = (uint8_t)(value & 0xF);
    gpu->page_base_y = (uint8_t)((value >> 4) & 1);
    gpu->semi_transparency = (uint8_t)((value >> 5) & 3);
    switch ((value >> 7) & 3) {
        case 0: gpu->texture_depth = T4Bit; break;
        case 1: gpu->texture_depth = T8Bit; break;
        case 2: gpu->texture_depth = T15Bit; break;
        default: LOG_GPU_WARN("Warn: GP0(E1) Unknown texture depth %d\n", (value >> 7) & 3); break;
    }
    gpu->dithering = ((value >> 9) & 1);
    gpu->draw_to_display = ((value >> 10) & 1);
    gpu->texture_disable = ((value >> 11) & 1);       // Affects textured primitives
    gpu->rectangle_texture_x_flip = ((value >> 12) & 1); // Affects texture sampling
    gpu->rectangle_texture_y_flip = ((value >> 13) & 1); // Affects texture sampling
    // printf("GP0(0xE1): Draw Mode set\n"); // Optional debug
}

/** GP0(0xE2): Set Texture Window */
static void gp0_texture_window(Gpu* gpu) {
     uint32_t value = gpu->gp0_command_buffer.buffer[0];
     gpu->texture_window_x_mask    = (uint8_t)(value & 0x1F);
     gpu->texture_window_y_mask    = (uint8_t)((value >> 5) & 0x1F);
     gpu->texture_window_x_offset  = (uint8_t)((value >> 10) & 0x1F);
     gpu->texture_window_y_offset  = (uint8_t)((value >> 15) & 0x1F);
     // printf("GP0(0xE2): Texture Window set\n"); // Optional debug
}

/** GP0(0xE3): Set Drawing Area Top Left */
static void gp0_drawing_area_top_left(Gpu* gpu) {
    uint32_t value = gpu->gp0_command_buffer.buffer[0];
    gpu->drawing_area_left = (uint16_t)(value & 0x3FF);
    gpu->drawing_area_top  = (uint16_t)((value >> 10) & 0x3FF);
    // printf("GP0(0xE3): Draw Area TL set = (%u,%u)\n", gpu->drawing_area_left, gpu->drawing_area_top);
}

/** GP0(0xE4): Set Drawing Area Bottom Right */
static void gp0_drawing_area_bottom_right(Gpu* gpu) {
    uint32_t value = gpu->gp0_command_buffer.buffer[0];
    gpu->drawing_area_right = (uint16_t)(value & 0x3FF);
    gpu->drawing_area_bottom= (uint16_t)((value >> 10) & 0x3FF);
    // printf("GP0(0xE4): Draw Area BR set = (%u,%u)\n", gpu->drawing_area_right, gpu->drawing_area_bottom);
}

/** GP0(0xE5): Set Drawing Offset */
static void gp0_drawing_offset(Gpu* gpu) {
    uint32_t value = gpu->gp0_command_buffer.buffer[0];
    uint16_t x_raw = (uint16_t)(value & 0x7FF);
    uint16_t y_raw = (uint16_t)((value >> 11) & 0x7FF);
    // Sign extend 11-bit values
    int16_t offset_x = (int16_t)(x_raw << 5) >> 5;
    int16_t offset_y = (int16_t)(y_raw << 5) >> 5;
    gpu->drawing_x_offset = offset_x;
    gpu->drawing_y_offset = offset_y;
    // printf("GP0(0xE5): Draw Offset set = (%d,%d)\n", offset_x, offset_y);
    renderer_set_draw_offset(&gpu->renderer, offset_x, offset_y); // Update renderer uniform
    // --- TEMPORARY HACK from guide ---
    // printf("GP0(0xE5): Triggering display (temporary hack)\n");
    renderer_display(&gpu->renderer); // Force draw & display swap
    // -----------------------------------------
}

/** GP0(0xE6): Set Mask Bit Setting */
static void gp0_mask_bit_setting(Gpu* gpu) {
     uint32_t value = gpu->gp0_command_buffer.buffer[0];
     gpu->force_set_mask_bit = (value & 1);        // Affects drawing
     gpu->preserve_masked_pixels = ((value >> 1) & 1); // Affects drawing
     // printf("GP0(0xE6): Mask Bit Setting = Force:%d Preserve:%d\n", gpu->force_set_mask_bit, gpu->preserve_masked_pixels);
}

/** GP0(0x28): Monochrome Opaque Quad */
static void gp0_quad_mono_opaque(Gpu* gpu) {
    if (gpu->gp0_command_buffer.count < 5) {
         LOG_ERROR("GP0(0x28) Error: Expected 5 words, got %u\n", gpu->gp0_command_buffer.count); return; }
    RendererColor c = { .r=(GLubyte)(gpu->gp0_command_buffer.buffer[0]&0xFF), .g=(GLubyte)((gpu->gp0_command_buffer.buffer[0]>>8)&0xFF), .b=(GLubyte)((gpu->gp0_command_buffer.buffer[0]>>16)&0xFF) };
    RendererColor colors[4] = {c, c, c, c};
    RendererPosition positions[4];
    for(int i=0; i<4; ++i){ uint32_t v=gpu->gp0_command_buffer.buffer[i+1]; positions[i].x=(GLshort)(int16_t)(v&0xFFFF); positions[i].y=(GLshort)(int16_t)(v>>16); }
    // printf("GP0(0x28): Mono Quad ...\n");
    renderer_set_texture_mode(&gpu->renderer, false);
    renderer_push_quad(&gpu->renderer, positions, colors, NULL, 0, 0);
}

/** GP0(0x2C): Textured Opaque Quadrilateral with Blend */
static void gp0_quad_texture_blend_opaque(Gpu* gpu) {
    if (gpu->gp0_command_buffer.count < 9) {
         LOG_ERROR("GP0(0x2C) Error: Expected 9 words, got %u\n", gpu->gp0_command_buffer.count); return; }
    RendererPosition p[4]; RendererTexCoord t[4]; uint16_t clut, texpage;
    
    // Vertex 0
    p[0] = (RendererPosition){ .x=(GLshort)(int16_t)(gpu->gp0_command_buffer.buffer[1]&0xFFFF), .y=(GLshort)(int16_t)(gpu->gp0_command_buffer.buffer[1]>>16) };
    t[0] = (RendererTexCoord){ .u=(GLshort)(gpu->gp0_command_buffer.buffer[2]&0xFF), .v=(GLshort)((gpu->gp0_command_buffer.buffer[2]>>8)&0xFF) };
    clut = (uint16_t)(gpu->gp0_command_buffer.buffer[2] >> 16);

    // Vertex 1
    p[1] = (RendererPosition){ .x=(GLshort)(int16_t)(gpu->gp0_command_buffer.buffer[3]&0xFFFF), .y=(GLshort)(int16_t)(gpu->gp0_command_buffer.buffer[3]>>16) };
    t[1] = (RendererTexCoord){ .u=(GLshort)(gpu->gp0_command_buffer.buffer[4]&0xFF), .v=(GLshort)((gpu->gp0_command_buffer.buffer[4]>>8)&0xFF) };
    texpage = (uint16_t)(gpu->gp0_command_buffer.buffer[4] >> 16);

    // Vertex 2
    p[2] = (RendererPosition){ .x=(GLshort)(int16_t)(gpu->gp0_command_buffer.buffer[5]&0xFFFF), .y=(GLshort)(int16_t)(gpu->gp0_command_buffer.buffer[5]>>16) };
    t[2] = (RendererTexCoord){ .u=(GLshort)(gpu->gp0_command_buffer.buffer[6]&0xFF), .v=(GLshort)((gpu->gp0_command_buffer.buffer[6]>>8)&0xFF) };

    // Vertex 3
    p[3] = (RendererPosition){ .x=(GLshort)(int16_t)(gpu->gp0_command_buffer.buffer[7]&0xFFFF), .y=(GLshort)(int16_t)(gpu->gp0_command_buffer.buffer[7]>>16) };
    t[3] = (RendererTexCoord){ .u=(GLshort)(gpu->gp0_command_buffer.buffer[8]&0xFF), .v=(GLshort)((gpu->gp0_command_buffer.buffer[8]>>8)&0xFF) };

    // Note: We pass raw UVs (0-255) and let the shader handle the page offset and CLUT lookup.
    // We do NOT add tpx/tpy here anymore.

    RendererColor c0 = { .r=(GLubyte)(gpu->gp0_command_buffer.buffer[0]&0xFF), .g=(GLubyte)((gpu->gp0_command_buffer.buffer[0]>>8)&0xFF), .b=(GLubyte)((gpu->gp0_command_buffer.buffer[0]>>16)&0xFF) };
    RendererColor c[4] = {c0, c0, c0, c0};

    renderer_set_texture_mode(&gpu->renderer, true);
    renderer_push_quad(&gpu->renderer, p, c, t, clut, texpage);
}

/** GP0(0x38): Shaded Opaque Quad */
static void gp0_quad_shaded_opaque(Gpu* gpu) {
    if (gpu->gp0_command_buffer.count < 8) {
         LOG_ERROR("GP0(0x38) Error: Expected 8 words, got %u\n", gpu->gp0_command_buffer.count); return; }
    RendererColor c[4]; RendererPosition p[4];
    for (int i = 0; i < 4; ++i) {
        uint32_t cw=gpu->gp0_command_buffer.buffer[i*2]; uint32_t vw=gpu->gp0_command_buffer.buffer[i*2+1];
        c[i].r=(GLubyte)(cw&0xFF); c[i].g=(GLubyte)((cw>>8)&0xFF); c[i].b=(GLubyte)((cw>>16)&0xFF);
        p[i].x=(GLshort)(int16_t)(vw&0xFFFF); p[i].y=(GLshort)(int16_t)(vw>>16); }
    // printf("GP0(0x38): Shaded Quad ...\n");
    renderer_set_texture_mode(&gpu->renderer, false);
    renderer_push_quad(&gpu->renderer, p, c, NULL, 0, 0);
}

/** GP0(0x30): Shaded Opaque Triangle */
static void gp0_triangle_shaded_opaque(Gpu* gpu) {
    if (gpu->gp0_command_buffer.count < 6) {
         LOG_ERROR("GP0(0x30) Error: Expected 6 words, got %u\n", gpu->gp0_command_buffer.count); return; }
    RendererColor c[3]; RendererPosition p[3];
    for (int i = 0; i < 3; ++i) {
        uint32_t cw=gpu->gp0_command_buffer.buffer[i*2]; uint32_t vw=gpu->gp0_command_buffer.buffer[i*2+1];
        c[i].r=(GLubyte)(cw&0xFF); c[i].g=(GLubyte)((cw>>8)&0xFF); c[i].b=(GLubyte)((cw>>16)&0xFF);
        p[i].x=(GLshort)(int16_t)(vw&0xFFFF); p[i].y=(GLshort)(int16_t)(vw>>16); }
    // printf("GP0(0x30): Shaded Triangle ...\n");
    renderer_set_texture_mode(&gpu->renderer, false);
    renderer_push_triangle(&gpu->renderer, p, c, NULL, 0, 0);
}

/** Helper: Draw a rectangle as a quad */
static void draw_rectangle(Gpu* gpu, int16_t x, int16_t y, uint16_t w, uint16_t h, RendererColor col, bool textured, RendererTexCoord* tex, uint16_t clut, uint16_t tpage) {
    RendererPosition p[4];
    RendererColor c[4] = {col, col, col, col};
    RendererTexCoord t[4];
    
    // Rectangle vertices: top-left, top-right, bottom-left, bottom-right
    p[0].x = x;       p[0].y = y;
    p[1].x = x + w;   p[1].y = y;
    p[2].x = x;       p[2].y = y + h;
    p[3].x = x + w;   p[3].y = y + h;
    
    if (textured && tex) {
        t[0].u = tex->u;       t[0].v = tex->v;
        t[1].u = tex->u + w;   t[1].v = tex->v;
        t[2].u = tex->u;       t[2].v = tex->v + h;
        t[3].u = tex->u + w;   t[3].v = tex->v + h;
        renderer_set_texture_mode(&gpu->renderer, true);
        renderer_push_quad(&gpu->renderer, p, c, t, clut, tpage);
    } else {
        renderer_set_texture_mode(&gpu->renderer, false);
        renderer_push_quad(&gpu->renderer, p, c, NULL, 0, 0);
    }
}

/** GP0(0x60): Monochrome Rectangle (variable size) */
static void gp0_rect_variable_opaque(Gpu* gpu) {
    if (gpu->gp0_command_buffer.count < 3) return;
    uint32_t cmd = gpu->gp0_command_buffer.buffer[0];
    uint32_t vtx = gpu->gp0_command_buffer.buffer[1];
    uint32_t dim = gpu->gp0_command_buffer.buffer[2];
    
    RendererColor col = { .r = (GLubyte)(cmd & 0xFF), .g = (GLubyte)((cmd >> 8) & 0xFF), .b = (GLubyte)((cmd >> 16) & 0xFF) };
    int16_t x = (int16_t)(vtx & 0xFFFF);
    int16_t y = (int16_t)(vtx >> 16);
    uint16_t w = (uint16_t)(dim & 0xFFFF);
    uint16_t h = (uint16_t)(dim >> 16);
    if (w == 0) w = 1; if (h == 0) h = 1;
    
    draw_rectangle(gpu, x, y, w, h, col, false, NULL, 0, 0);
}

/** GP0(0x62): Semi-Transparent Monochrome Rectangle (variable size) */
static void gp0_rect_variable_semi_trans(Gpu* gpu) {
    // For now, treat same as opaque
    gp0_rect_variable_opaque(gpu);
}

/** GP0(0x64): Textured Rectangle (variable size, blending) */
static void gp0_rect_tex_variable_opaque(Gpu* gpu) {
    if (gpu->gp0_command_buffer.count < 4) return;
    uint32_t cmd = gpu->gp0_command_buffer.buffer[0];
    uint32_t vtx = gpu->gp0_command_buffer.buffer[1];
    uint32_t uv_clut = gpu->gp0_command_buffer.buffer[2];
    uint32_t dim = gpu->gp0_command_buffer.buffer[3];
    
    RendererColor col = { .r = (GLubyte)(cmd & 0xFF), .g = (GLubyte)((cmd >> 8) & 0xFF), .b = (GLubyte)((cmd >> 16) & 0xFF) };
    int16_t x = (int16_t)(vtx & 0xFFFF);
    int16_t y = (int16_t)(vtx >> 16);
    RendererTexCoord tex = { .u = (GLshort)(uv_clut & 0xFF), .v = (GLshort)((uv_clut >> 8) & 0xFF) };
    uint16_t clut = (uint16_t)(uv_clut >> 16);
    uint16_t w = (uint16_t)(dim & 0xFFFF);
    uint16_t h = (uint16_t)(dim >> 16);
    if (w == 0) w = 1; if (h == 0) h = 1;
    
    // Use current texpage from GPU state
    uint16_t tpage = (uint16_t)((gpu->page_base_x) | (gpu->page_base_y << 4) | (gpu->texture_depth << 7));
    draw_rectangle(gpu, x, y, w, h, col, true, &tex, clut, tpage);
}

/** GP0(0x68): Monochrome Rectangle 1x1 (single pixel) */
static void gp0_rect_1x1_opaque(Gpu* gpu) {
    if (gpu->gp0_command_buffer.count < 2) return;
    uint32_t cmd = gpu->gp0_command_buffer.buffer[0];
    uint32_t vtx = gpu->gp0_command_buffer.buffer[1];
    
    RendererColor col = { .r = (GLubyte)(cmd & 0xFF), .g = (GLubyte)((cmd >> 8) & 0xFF), .b = (GLubyte)((cmd >> 16) & 0xFF) };
    int16_t x = (int16_t)(vtx & 0xFFFF);
    int16_t y = (int16_t)(vtx >> 16);
    
    draw_rectangle(gpu, x, y, 1, 1, col, false, NULL, 0, 0);
}

/** GP0(0x70): Monochrome Rectangle 8x8 */
static void gp0_rect_8x8_opaque(Gpu* gpu) {
    if (gpu->gp0_command_buffer.count < 2) return;
    uint32_t cmd = gpu->gp0_command_buffer.buffer[0];
    uint32_t vtx = gpu->gp0_command_buffer.buffer[1];
    
    RendererColor col = { .r = (GLubyte)(cmd & 0xFF), .g = (GLubyte)((cmd >> 8) & 0xFF), .b = (GLubyte)((cmd >> 16) & 0xFF) };
    int16_t x = (int16_t)(vtx & 0xFFFF);
    int16_t y = (int16_t)(vtx >> 16);
    
    draw_rectangle(gpu, x, y, 8, 8, col, false, NULL, 0, 0);
}

/** GP0(0x78): Monochrome Rectangle 16x16 */
static void gp0_rect_16x16_opaque(Gpu* gpu) {
    if (gpu->gp0_command_buffer.count < 2) return;
    uint32_t cmd = gpu->gp0_command_buffer.buffer[0];
    uint32_t vtx = gpu->gp0_command_buffer.buffer[1];
    
    RendererColor col = { .r = (GLubyte)(cmd & 0xFF), .g = (GLubyte)((cmd >> 8) & 0xFF), .b = (GLubyte)((cmd >> 16) & 0xFF) };
    int16_t x = (int16_t)(vtx & 0xFFFF);
    int16_t y = (int16_t)(vtx >> 16);
    
    draw_rectangle(gpu, x, y, 16, 16, col, false, NULL, 0, 0);
}

/** GP0(0x6C): Textured Rectangle 1x1 */
static void gp0_rect_tex_1x1_opaque(Gpu* gpu) {
    if (gpu->gp0_command_buffer.count < 3) return;
    uint32_t cmd = gpu->gp0_command_buffer.buffer[0];
    uint32_t vtx = gpu->gp0_command_buffer.buffer[1];
    uint32_t uv_clut = gpu->gp0_command_buffer.buffer[2];
    
    RendererColor col = { .r = (GLubyte)(cmd & 0xFF), .g = (GLubyte)((cmd >> 8) & 0xFF), .b = (GLubyte)((cmd >> 16) & 0xFF) };
    int16_t x = (int16_t)(vtx & 0xFFFF);
    int16_t y = (int16_t)(vtx >> 16);
    RendererTexCoord tex = { .u = (GLshort)(uv_clut & 0xFF), .v = (GLshort)((uv_clut >> 8) & 0xFF) };
    uint16_t clut = (uint16_t)(uv_clut >> 16);
    uint16_t tpage = (uint16_t)((gpu->page_base_x) | (gpu->page_base_y << 4) | (gpu->texture_depth << 7));
    
    draw_rectangle(gpu, x, y, 1, 1, col, true, &tex, clut, tpage);
}

/** GP0(0x74): Textured Rectangle 8x8 */
static void gp0_rect_tex_8x8_opaque(Gpu* gpu) {
    if (gpu->gp0_command_buffer.count < 3) return;
    uint32_t cmd = gpu->gp0_command_buffer.buffer[0];
    uint32_t vtx = gpu->gp0_command_buffer.buffer[1];
    uint32_t uv_clut = gpu->gp0_command_buffer.buffer[2];
    
    RendererColor col = { .r = (GLubyte)(cmd & 0xFF), .g = (GLubyte)((cmd >> 8) & 0xFF), .b = (GLubyte)((cmd >> 16) & 0xFF) };
    int16_t x = (int16_t)(vtx & 0xFFFF);
    int16_t y = (int16_t)(vtx >> 16);
    RendererTexCoord tex = { .u = (GLshort)(uv_clut & 0xFF), .v = (GLshort)((uv_clut >> 8) & 0xFF) };
    uint16_t clut = (uint16_t)(uv_clut >> 16);
    uint16_t tpage = (uint16_t)((gpu->page_base_x) | (gpu->page_base_y << 4) | (gpu->texture_depth << 7));
    
    draw_rectangle(gpu, x, y, 8, 8, col, true, &tex, clut, tpage);
}

/** GP0(0x7C): Textured Rectangle 16x16 */
static void gp0_rect_tex_16x16_opaque(Gpu* gpu) {
    if (gpu->gp0_command_buffer.count < 3) return;
    uint32_t cmd = gpu->gp0_command_buffer.buffer[0];
    uint32_t vtx = gpu->gp0_command_buffer.buffer[1];
    uint32_t uv_clut = gpu->gp0_command_buffer.buffer[2];
    
    RendererColor col = { .r = (GLubyte)(cmd & 0xFF), .g = (GLubyte)((cmd >> 8) & 0xFF), .b = (GLubyte)((cmd >> 16) & 0xFF) };
    int16_t x = (int16_t)(vtx & 0xFFFF);
    int16_t y = (int16_t)(vtx >> 16);
    RendererTexCoord tex = { .u = (GLshort)(uv_clut & 0xFF), .v = (GLshort)((uv_clut >> 8) & 0xFF) };
    uint16_t clut = (uint16_t)(uv_clut >> 16);
    uint16_t tpage = (uint16_t)((gpu->page_base_x) | (gpu->page_base_y << 4) | (gpu->texture_depth << 7));
    
    draw_rectangle(gpu, x, y, 16, 16, col, true, &tex, clut, tpage);
}

/** GP0(0xA0): Copy Rectangle (CPU/DMA to VRAM) - Setup Phase */
static void gp0_image_load(Gpu* gpu) {
     if (gpu->gp0_command_buffer.count < 3) {
         LOG_ERROR("GP0(0xA0) Error: Expected 3 words, got %u\n", gpu->gp0_command_buffer.count); return; }
    uint32_t dest_coord = gpu->gp0_command_buffer.buffer[1];
    uint32_t dimensions = gpu->gp0_command_buffer.buffer[2];
    gpu->vram_load_x = (uint16_t)(dest_coord & 0x3FF); // X coord is 10 bits
    gpu->vram_load_y = (uint16_t)((dest_coord >> 16) & 0x1FF); // Y coord is 9 bits
    gpu->vram_load_w = (uint16_t)(dimensions & 0x3FF); // Width is 10 bits
    gpu->vram_load_h = (uint16_t)((dimensions >> 16) & 0x1FF); // Height is 9 bits

    // Width and height seem to be stored as W-1, H-1 in some docs, but maybe not always?
    // Let's assume they are direct values for now. Clamp values for safety.
    if (gpu->vram_load_w == 0) gpu->vram_load_w = 1024; // Nocash says 0 means 1024?
    if (gpu->vram_load_h == 0) gpu->vram_load_h = 512;  // Nocash says 0 means 512?
    gpu->vram_load_w = (gpu->vram_load_w > VRAM_WIDTH) ? VRAM_WIDTH : gpu->vram_load_w;
    gpu->vram_load_h = (gpu->vram_load_h > VRAM_HEIGHT) ? VRAM_HEIGHT : gpu->vram_load_h;


    uint32_t image_size_pixels = (uint32_t)gpu->vram_load_w * (uint32_t)gpu->vram_load_h;
    uint32_t image_size_pixels_rounded = (image_size_pixels + 1) & ~1; // Round up for pairs
    uint32_t words_to_load = image_size_pixels_rounded / 2;            // Each word contains 2 pixels

    LOG_GPU_INFO("GP0(0xA0): Setup Image Load to VRAM (%u,%u) Size=(%ux%u) -> Expecting %u words\n",
           gpu->vram_load_x, gpu->vram_load_y, gpu->vram_load_w, gpu->vram_load_h, words_to_load);

    if (words_to_load == 0 || ((uint64_t)words_to_load * 4) > VRAM_SIZE) { // Basic sanity check
        LOG_GPU_WARN("Warning: Invalid image load size %u words requested.\n", words_to_load);
        gpu->gp0_words_remaining = 0; gpu->gp0_mode = GP0_MODE_COMMAND; return; }

    gpu->gp0_words_remaining = words_to_load;
    gpu->gp0_mode = GP0_MODE_IMAGE_LOAD;
    gpu->vram_load_count = 0; // Reset pixel counter for this transfer
}

/** GP0(0xC0): Copy Rectangle (VRAM to CPU/DMA) */
static void gp0_image_store(Gpu* gpu) {
    if (gpu->gp0_command_buffer.count < 3) {
        LOG_GPU_ERROR("GP0(0xC0) Error: Expected 3 words, got %u\n", gpu->gp0_command_buffer.count);
        return;
    }

    uint32_t val1 = gpu->gp0_command_buffer.buffer[1];
    uint32_t val2 = gpu->gp0_command_buffer.buffer[2];

    gpu->vram_load_x = (uint16_t)(val1 & 0x3FF);
    gpu->vram_load_y = (uint16_t)((val1 >> 16) & 0x1FF);
    gpu->vram_load_w = (uint16_t)(val2 & 0xFFFF);
    gpu->vram_load_h = (uint16_t)(val2 >> 16);

    // Width and Height must be non-zero
    if (gpu->vram_load_w == 0) gpu->vram_load_w = 1;
    if (gpu->vram_load_h == 0) gpu->vram_load_h = 1;

    // Calculate total number of 32-bit words to transfer
    // Each word contains 2 pixels (16-bit each)
    // Total pixels = w * h
    // Total words = (total pixels + 1) / 2  (Round up if odd number of pixels)
    uint32_t total_pixels = (uint32_t)gpu->vram_load_w * gpu->vram_load_h;
    gpu->gp0_words_remaining = (total_pixels + 1) / 2;

    gpu->vram_load_count = 0; // Reset pixel counter
    gpu->gp0_mode = GP0_MODE_IMAGE_STORE;

    LOG_GPU_INFO("GP0(0xC0): Image Store Started. Rect=(%u,%u) Size=(%ux%u), Words=%u\n",
                 gpu->vram_load_x, gpu->vram_load_y, gpu->vram_load_w, gpu->vram_load_h, gpu->gp0_words_remaining);
}


// --- Main GPU Public Functions ---

/**
 * @brief Initializes the GPU state, including VRAM and default register values.
 * This is a full initialization (power-on or hard reset).
 */
void gpu_init_full(Gpu* gpu, Interconnect* inter) {
    LOG_GPU_INFO("GPU full initialization (with VRAM)");
    LOG_GPU_INFO("GPU Initializing (full)...\n");
    vram_init(&gpu->vram); // Init VRAM only on full reset
    // Initialize all Gpu struct members to power-on/GP1 Reset defaults
    gpu->interrupt = false; gpu->page_base_x = 0; gpu->page_base_y = 0;
    gpu->semi_transparency = 0; gpu->texture_depth = T4Bit;
    gpu->texture_window_x_mask = 0; gpu->texture_window_y_mask = 0;
    gpu->texture_window_x_offset = 0; gpu->texture_window_y_offset = 0;
    gpu->dithering = false; gpu->draw_to_display = false;
    gpu->texture_disable = false; gpu->rectangle_texture_x_flip = false;
    gpu->rectangle_texture_y_flip = false; gpu->drawing_area_left = 0;
    gpu->drawing_area_top = 0; gpu->drawing_area_right = 0;
    gpu->drawing_area_bottom = 0; gpu->drawing_x_offset = 0;
    gpu->drawing_y_offset = 0; gpu->force_set_mask_bit = false;
    gpu->preserve_masked_pixels = false; gpu->dma_setting = GPU_DMA_Off;
    gpu->display_disabled = true; gpu->display_vram_x_start = 0;
    gpu->display_vram_y_start = 0; gpu->hres_raw = (HorizontalResRaw){0, 0};
    gpu->vres = Y240Lines; gpu->vmode = Ntsc; gpu->interlaced = true;
    gpu->display_depth = D15Bits; gpu->display_horiz_start = 0x200;
    gpu->display_horiz_end = 0xc00; gpu->display_line_start = 0x10;
    gpu->display_line_end = 0x100; gpu->field = Top;
    clear_gp0_command_buffer(gpu); gpu->gp0_words_remaining = 0;
    gpu->gp0_mode = GP0_MODE_COMMAND;
    gpu->gp0_command_method = NULL;
    gpu->vram_load_x = 0; gpu->vram_load_y = 0; gpu->vram_load_w = 0;
    gpu->vram_load_h = 0; gpu->vram_load_count = 0;
    gpu->inter = inter;
    LOG_GPU_INFO("GPU Initialized (State reset, VRAM initialized).\n");
}

/**
 * @brief Soft reset of the GPU state (does NOT clear VRAM).
 * Used for GP1(0x00) Soft Reset command.
 */
void gpu_soft_reset(Gpu* gpu) {
    LOG_GPU_DEBUG("GPU soft reset (no VRAM)");
    LOG_GPU_DEBUG("GPU Soft Reset (no VRAM clear)...\n");
    // All state reset EXCEPT VRAM
    gpu->interrupt = false; gpu->page_base_x = 0; gpu->page_base_y = 0;
    gpu->semi_transparency = 0; gpu->texture_depth = T4Bit;
    gpu->texture_window_x_mask = 0; gpu->texture_window_y_mask = 0;
    gpu->texture_window_x_offset = 0; gpu->texture_window_y_offset = 0;
    gpu->dithering = false; gpu->draw_to_display = false;
    gpu->texture_disable = false; gpu->rectangle_texture_x_flip = false;
    gpu->rectangle_texture_y_flip = false; gpu->drawing_area_left = 0;
    gpu->drawing_area_top = 0; gpu->drawing_area_right = 0;
    gpu->drawing_area_bottom = 0; gpu->drawing_x_offset = 0;
    gpu->drawing_y_offset = 0; gpu->force_set_mask_bit = false;
    gpu->preserve_masked_pixels = false; gpu->dma_setting = GPU_DMA_Off;
    gpu->display_disabled = true; gpu->display_vram_x_start = 0;
    gpu->display_vram_y_start = 0; gpu->hres_raw = (HorizontalResRaw){0, 0};
    gpu->vres = Y240Lines; gpu->vmode = Ntsc; gpu->interlaced = true;
    gpu->display_depth = D15Bits; gpu->display_horiz_start = 0x200;
    gpu->display_horiz_end = 0xc00; gpu->display_line_start = 0x10;
    gpu->display_line_end = 0x100; gpu->field = Top;
    clear_gp0_command_buffer(gpu); gpu->gp0_words_remaining = 0;
    gpu->gp0_mode = GP0_MODE_COMMAND;
    gpu->gp0_command_method = NULL;
    gpu->vram_load_x = 0; gpu->vram_load_y = 0; gpu->vram_load_w = 0;
    gpu->vram_load_h = 0; gpu->vram_load_count = 0;
    // gpu->inter remains unchanged
    LOG_GPU_DEBUG("GPU Soft Reset complete (VRAM preserved).\n");
}

/** Processes commands/data sent to GP0 port */
void gpu_gp0(Gpu* gpu, uint32_t command) {
    LOG_GPU_DEBUG("[GP0] Command: 0x%08x (Opcode: 0x%02x)", command, (command >> 24) & 0xFF);
    // Handle IMAGE_LOAD state first
    if (gpu->gp0_mode == GP0_MODE_IMAGE_LOAD) {
        uint16_t pixel1 = (uint16_t)(command & 0xFFFF);
        uint16_t pixel2 = (uint16_t)(command >> 16);
        uint32_t idx = gpu->vram_load_count; // Base index for pixel 1
        // Check if pixel 1 is within the logical height*width boundary
        if (idx < ((uint32_t)gpu->vram_load_w * gpu->vram_load_h)) {
             uint16_t x = gpu->vram_load_x + (uint16_t)(idx % gpu->vram_load_w);
             uint16_t y = gpu->vram_load_y + (uint16_t)(idx / gpu->vram_load_w);
             // Check against physical VRAM boundaries
             if (y < VRAM_HEIGHT && x < VRAM_WIDTH) {
                 uint32_t offset = (uint32_t)y * VRAM_WIDTH * VRAM_BPP + (uint32_t)x * VRAM_BPP;
                 vram_store16(&gpu->vram, offset, pixel1);
             } // Else: Pixel write out of VRAM bounds (optional warning)
        }
        idx++; // Index for pixel 2
        // Check if pixel 2 is within the logical height*width boundary
        if (idx < ((uint32_t)gpu->vram_load_w * gpu->vram_load_h)) {
            uint16_t x = gpu->vram_load_x + (uint16_t)(idx % gpu->vram_load_w);
            uint16_t y = gpu->vram_load_y + (uint16_t)(idx / gpu->vram_load_w);
            // Check against physical VRAM boundaries
             if (y < VRAM_HEIGHT && x < VRAM_WIDTH) {
                 uint32_t offset = (uint32_t)y * VRAM_WIDTH * VRAM_BPP + (uint32_t)x * VRAM_BPP;
                 vram_store16(&gpu->vram, offset, pixel2);
             } // Else: Pixel write out of VRAM bounds (optional warning)
        }
        gpu->vram_load_count += 2; // Increment count by 2 pixels
        gpu->gp0_words_remaining--; // Decrement remaining data words
        if (gpu->gp0_words_remaining == 0) { // Check if transfer complete
            gpu->gp0_mode = GP0_MODE_COMMAND; // Switch back to command mode
            // printf("GPU Img Load Finished.\n"); // Optional debug
            renderer_upload_vram(&gpu->renderer, (const uint16_t*)gpu->vram.data);
        }
        return; // Done processing this data word
    }

    // Handle COMMAND mode
    if (gpu->gp0_words_remaining == 0) {
        // Start of a new command
        uint8_t opcode = (uint8_t)(command >> 24);
        uint32_t expected_len = 0; void (*handler)(Gpu*) = NULL;
        gpu->gp0_current_opcode = opcode; clear_gp0_command_buffer(gpu);

        // Determine expected length and handler based on opcode
        switch (opcode) {
            case 0x00: expected_len = 1; handler = gp0_nop; break;
            case 0x01: expected_len = 1; handler = gp0_clear_cache; break;
            case 0x02: expected_len = 3; handler = gp0_fill_rectangle; break;
            case 0x28: expected_len = 5; handler = gp0_quad_mono_opaque; break;
            case 0x2C: expected_len = 9; handler = gp0_quad_texture_blend_opaque; break;
            case 0x30: expected_len = 6; handler = gp0_triangle_shaded_opaque; break;
            case 0x38: expected_len = 8; handler = gp0_quad_shaded_opaque; break;
            // Rectangle commands (0x60-0x7F)
            case 0x60: expected_len = 3; handler = gp0_rect_variable_opaque; break;
            case 0x62: expected_len = 3; handler = gp0_rect_variable_semi_trans; break;
            case 0x64: expected_len = 4; handler = gp0_rect_tex_variable_opaque; break;
            case 0x65: expected_len = 4; handler = gp0_rect_tex_variable_opaque; break; // Raw texture
            case 0x66: expected_len = 4; handler = gp0_rect_tex_variable_opaque; break; // Semi-trans
            case 0x67: expected_len = 4; handler = gp0_rect_tex_variable_opaque; break; // Semi-trans raw
            case 0x68: expected_len = 2; handler = gp0_rect_1x1_opaque; break;
            case 0x6A: expected_len = 2; handler = gp0_rect_1x1_opaque; break; // Semi-trans
            case 0x6C: expected_len = 3; handler = gp0_rect_tex_1x1_opaque; break;
            case 0x6D: expected_len = 3; handler = gp0_rect_tex_1x1_opaque; break; // Raw
            case 0x6E: expected_len = 3; handler = gp0_rect_tex_1x1_opaque; break; // Semi-trans
            case 0x6F: expected_len = 3; handler = gp0_rect_tex_1x1_opaque; break; // Semi-trans raw
            case 0x70: expected_len = 2; handler = gp0_rect_8x8_opaque; break;
            case 0x72: expected_len = 2; handler = gp0_rect_8x8_opaque; break; // Semi-trans
            case 0x74: expected_len = 3; handler = gp0_rect_tex_8x8_opaque; break;
            case 0x75: expected_len = 3; handler = gp0_rect_tex_8x8_opaque; break; // Raw
            case 0x76: expected_len = 3; handler = gp0_rect_tex_8x8_opaque; break; // Semi-trans
            case 0x77: expected_len = 3; handler = gp0_rect_tex_8x8_opaque; break; // Semi-trans raw
            case 0x78: expected_len = 2; handler = gp0_rect_16x16_opaque; break;
            case 0x7A: expected_len = 2; handler = gp0_rect_16x16_opaque; break; // Semi-trans
            case 0x7C: expected_len = 3; handler = gp0_rect_tex_16x16_opaque; break;
            case 0x7D: expected_len = 3; handler = gp0_rect_tex_16x16_opaque; break; // Raw
            case 0x7E: expected_len = 3; handler = gp0_rect_tex_16x16_opaque; break; // Semi-trans
            case 0x7F: expected_len = 3; handler = gp0_rect_tex_16x16_opaque; break; // Semi-trans raw
            case 0xA0: expected_len = 3; handler = gp0_image_load; break; // Sets up IMAGE_LOAD mode
            case 0xC0: expected_len = 3; handler = gp0_image_store; break;
            case 0xE1: expected_len = 1; handler = gp0_draw_mode; break;
            case 0xE2: expected_len = 1; handler = gp0_texture_window; break;
            case 0xE3: expected_len = 1; handler = gp0_drawing_area_top_left; break;
            case 0xE4: expected_len = 1; handler = gp0_drawing_area_bottom_right; break;
            case 0xE5: expected_len = 1; handler = gp0_drawing_offset; break;
            case 0xE6: expected_len = 1; handler = gp0_mask_bit_setting; break;
            default:
                LOG_ERROR("GPU Error: Unhandled GP0 Opcode 0x%02x (Cmd 0x%08x) [STUBBED]", opcode, command);
                expected_len = 1; handler = gp0_nop; gpu->gp0_current_opcode = 0xFF; break; }

        // Sanity check length
        if (expected_len == 0 || expected_len > MAX_GPU_COMMAND_WORDS) {
             LOG_ERROR("GPU Error: Cmd 0x%02x invalid length %u\n", opcode, expected_len);
             expected_len = 1; handler = gp0_nop; gpu->gp0_current_opcode = 0xFF; }

        gpu->gp0_words_remaining = expected_len;
        gpu->gp0_command_method = handler;
    }

    // Buffer the current command word
    push_gp0_command_word(gpu, command);
    gpu->gp0_words_remaining--;

    // If all words for the command received, execute the handler
    if (gpu->gp0_words_remaining == 0) {
         if (gpu->gp0_command_method != NULL) {
             (gpu->gp0_command_method)(gpu); // Call the stored function pointer
         } else {
             LOG_ERROR("GPU Error: NULL handler for GP0 opcode 0x%02x\n", gpu->gp0_current_opcode);
         }
         // If we didn't just finish setting up IMAGE_LOAD mode, reset for next command
         if (gpu->gp0_mode == GP0_MODE_COMMAND) {
             clear_gp0_command_buffer(gpu);
             gpu->gp0_current_opcode = 0xFF; // Ready for next command
         }
    }
}

/** Processes commands sent to GP1 port */
void gpu_gp1(Gpu* gpu, uint32_t command) {
    LOG_GPU_DEBUG("[GP1] Command: 0x%08x (Opcode: 0x%02x)", command, (command >> 24) & 0xFF);
    uint32_t opcode = (command >> 24) & 0xFF;
    switch (opcode) {
        case 0x00: gp1_reset(gpu, command); break;
        case 0x01: gp1_reset_command_buffer(gpu, command); break;
        case 0x02: gp1_acknowledge_irq(gpu, command); break;
        case 0x03: gp1_display_enable(gpu, command); break;
        case 0x04: gp1_dma_direction(gpu, command); break;
        case 0x05: gp1_display_vram_start(gpu, command); break;
        case 0x06: gp1_display_horizontal_range(gpu, command); break;
        case 0x07: gp1_display_vertical_range(gpu, command); break;
        case 0x08: gp1_display_mode(gpu, command); break;
        // Add cases for 0x09 (Get GPU Info), 0x10-0x1F (GPU Info responses) if needed
        default:
            LOG_ERROR("Error: Unhandled GP1 command: Opcode 0x%02x, Value 0x%08x [STUBBED]", opcode, command);
            break;
    }
}

/** Reads the GPU Status Register (GPUSTAT) */
uint32_t gpu_read_status(Gpu* gpu) {
    static bool toggle_irq = false;
    uint32_t r = 0;
    r |= (uint32_t)gpu->page_base_x << 0;
    r |= (uint32_t)gpu->page_base_y << 4;
    r |= (uint32_t)gpu->semi_transparency << 5;
    r |= (uint32_t)gpu->texture_depth << 7;
    r |= (uint32_t)gpu->dithering << 9;
    r |= (uint32_t)gpu->draw_to_display << 10;
    r |= (uint32_t)gpu->force_set_mask_bit << 11;
    r |= (uint32_t)gpu->preserve_masked_pixels << 12;
    r |= (uint32_t)gpu->field << 13;
    r |= (uint32_t)gpu->texture_disable << 15;
    uint32_t hres_raw_val = ((uint32_t)gpu->hres_raw.hr2 << 2) | (uint32_t)gpu->hres_raw.hr1;
    r |= ((hres_raw_val >> 0) & 1) << 16;
    r |= ((hres_raw_val >> 1) & 1) << 17;
    r |= ((hres_raw_val >> 2) & 1) << 18;
    r |= (0 << 19);
    r |= ((uint32_t)gpu->vmode << 20);
    r |= ((uint32_t)gpu->display_depth << 21);
    r |= ((uint32_t)gpu->interlaced << 22);
    r |= ((uint32_t)gpu->display_disabled << 23);
    r |= ((uint32_t)(toggle_irq ? 1 : 0) << 24);
    toggle_irq = !toggle_irq;
    // --- Always set ready bits as per PSX-Spex/nocash ---
    r |= (1 << 26); // STAT[26] - Ready to receive command word
    r |= (1 << 27); // STAT[27] - Ready to send VRAM to CPU
    r |= (1 << 28); // STAT[28] - Ready to receive DMA block
    // --- DMA Request Bit (STAT[25]) ---
    // Set if DMA direction is not Off
    if (gpu->dma_setting != GPU_DMA_Off) {
        r |= (1 << 25);
    }
    // --- DMA Direction Bits (STAT[30:29]) ---
    r |= ((uint32_t)gpu->dma_setting << 29);
    // Bit 31: Odd/Even line signal (needs timing) - Placeholder 0
    bool vblank = (r & (1 << 23)) != 0;
    LOG_GPU_DEBUG("[GPUSTAT] Read: 0x%08x (VBlank=%d)", r, vblank);
    return r;
}

/** Reads data from the GPUREAD port (e.g., after Image Store command) */
uint32_t gpu_read_data(Gpu* gpu) {
    if (gpu->gp0_mode == GP0_MODE_IMAGE_STORE) {
        if (gpu->gp0_words_remaining == 0) {
            LOG_GPU_WARN("GPUREAD: Read attempted but no words remaining in Image Store transfer.\n");
            return 0xFFFFFFFF; // Or 0, undefined behavior
        }

        uint32_t word = 0;
        uint16_t pixel1 = 0;
        uint16_t pixel2 = 0;

        // Read Pixel 1
        uint32_t idx = gpu->vram_load_count;
        uint16_t x = gpu->vram_load_x + (uint16_t)(idx % gpu->vram_load_w);
        uint16_t y = gpu->vram_load_y + (uint16_t)(idx / gpu->vram_load_w);
        
        // Handle VRAM wrapping (1024x512)
        x &= 0x3FF;
        y &= 0x1FF;

        uint32_t offset = (uint32_t)y * VRAM_WIDTH * VRAM_BPP + (uint32_t)x * VRAM_BPP;
        pixel1 = vram_load16(&gpu->vram, offset);
        gpu->vram_load_count++;

        // Read Pixel 2 (if available)
        if (gpu->vram_load_count < ((uint32_t)gpu->vram_load_w * gpu->vram_load_h)) {
            idx = gpu->vram_load_count;
            x = gpu->vram_load_x + (uint16_t)(idx % gpu->vram_load_w);
            y = gpu->vram_load_y + (uint16_t)(idx / gpu->vram_load_w);
            
            x &= 0x3FF;
            y &= 0x1FF;

            offset = (uint32_t)y * VRAM_WIDTH * VRAM_BPP + (uint32_t)x * VRAM_BPP;
            pixel2 = vram_load16(&gpu->vram, offset);
            gpu->vram_load_count++;
        } else {
            pixel2 = 0; // Padding if odd number of pixels
        }

        word = (uint32_t)pixel1 | ((uint32_t)pixel2 << 16);
        gpu->gp0_words_remaining--;

        LOG_GPU_DEBUG("[GPUREAD] Image Store Read: 0x%08x (Rem: %u)\n", word, gpu->gp0_words_remaining);

        if (gpu->gp0_words_remaining == 0) {
            LOG_GPU_INFO("GP0(0xC0): Image Store Finished.\n");
            gpu->gp0_mode = GP0_MODE_COMMAND;
        }

        return word;
    }

    // Fallback for non-transfer reads (usually returns 0 or last status)
    // For now, keep the dummy behavior or return 0
    static uint32_t dummy_gpu_read = 0xDEADBEEF;
    dummy_gpu_read++;
    LOG_GPU_DEBUG("[GPUREAD] Read (No Transfer): 0x%08x\n", dummy_gpu_read);
    return dummy_gpu_read;
}

void gpu_trigger_vblank_irq(Gpu* gpu) {
    // Only set the VBlank bit in GPUSTAT here. Do NOT request IRQ0 directly.
    // Per PSX-Spex/nocash, VBlank IRQ0 must be generated by Timer0, not the GPU.
    // If you want to simulate VBlank timing, coordinate with Timer0 logic.
    // (You may want to set a vblank flag or call a callback here if needed for rendering.)
    // Example: gpu->in_vblank = true;
    // (No call to interconnect_request_irq here)
    LOG_GPU_DEBUG("[GPU] VBlank event (no IRQ0 requested, handled by Timer0)");
}