/**
 * gpu.c
 * Implementation of the PlayStation GPU emulation.
 * 
 * MODULAR REFACTORING STATUS (Phase 1 Complete):
 * ✅ Phase 1: Initialization moved to src/gpu/gpu_core.c
 * 🔄 Phase 2: Command handlers (GP0/GP1) - still here, will migrate to gpu_commands.c
 * ⏳ Phase 3: Rendering functions - will migrate to gpu_rendering.c
 * ⏳ Phase 4: VRAM operations - will migrate to gpu_vram.c
 * ⏳ Phase 5: Timing/CRTC - will migrate to gpu_timing.c
 * 
 * This file contains legacy command processing until full migration complete.
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
static void gpu_update_display_mapping(Gpu* gpu);
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
static void gp0_copy_rectangle(Gpu* gpu);
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

// --- Forward Declarations for Helpers ---
static void draw_rectangle(Gpu* gpu, int16_t x, int16_t y, uint16_t w, uint16_t h, RendererColor col, bool textured, bool raw_texture, RendererTexCoord* tex, uint16_t clut, uint16_t tpage);
static void vram_write_masked(Gpu* gpu, uint32_t offset, uint16_t pixel);
static void gpu_debug_dump_region(Gpu* gpu);


// --- Helper Functions ---

/**
 * @brief Clears the GP0 command buffer.
 * @param gpu Pointer to the Gpu instance.
 */
void clear_gp0_command_buffer(Gpu* gpu) {
    gpu->gp0_command_buffer.count = 0;
    // No need to zero the buffer content itself
}

/**
 * @brief Pushes a word onto the GP0 command buffer.
 * Handles potential buffer overflow.
 * @param gpu Pointer to the Gpu instance.
 * @param word The 32-bit command word to push.
 */
void push_gp0_command_word(Gpu* gpu, uint32_t word) {
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
    LOG_GPU_DEBUG("GPU: Reset Command Buffer (GP1 Cmd 0x01)");
    (void)value; // value is unused for this command
    clear_gp0_command_buffer(gpu);
    gpu->gp0_words_remaining = 0;
    gpu->gp0_mode = GP0_MODE_COMMAND; // Reset mode
    gpu->gp0_current_opcode = 0xFF; // Reset opcode tracking
    gpu->gp0_command_method = NULL; // Reset handler pointer
        // Clear internal hardware FIFO
        gpu->gp0_fifo_head = 0; gpu->gp0_fifo_tail = 0; gpu->gp0_fifo_count = 0;
}

/** GP1(0x02): Acknowledge GPU Interrupt */
static void gp1_acknowledge_irq(Gpu* gpu, uint32_t value) {
    LOG_GPU_DEBUG("GPU: Acknowledge IRQ (GP1 Cmd 0x02)");
     (void)value; // value is unused for this command
     gpu->interrupt = false; // Clear the interrupt flag (STAT[24])
}

/** GP1(0x03): Display Enable */
static void gp1_display_enable(Gpu* gpu, uint32_t value) {
    // Bit 0: 0 = Enable Display, 1 = Disable Display
    gpu->display_disabled = (value & 1);
    LOG_GPU_DEBUG("GPU: Display Enable = %s (GP1 Cmd 0x03)", gpu->display_disabled ? "Disabled" : "Enabled");
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
    LOG_GPU_DEBUG("GPU: Display VRAM Start X=%u Y=%u (GP1 Cmd 0x05)",
        gpu->display_vram_x_start, gpu->display_vram_y_start);
    gpu_update_display_mapping(gpu);
}

/** GP1(0x06): Display Horizontal sync and display range */
static void gp1_display_horizontal_range(Gpu* gpu, uint32_t value) {
    // Bits 0-11: Hsync Start coordinate (dotclock units)
    // Bits 12-23: Hsync End coordinate (dotclock units)
    gpu->display_horiz_start = (uint16_t)(value & 0xFFF);
    gpu->display_horiz_end = (uint16_t)((value >> 12) & 0xFFF);
    LOG_GPU_DEBUG("GPU: Display H-Range Start=%u End=%u (GP1 Cmd 0x06)",
        gpu->display_horiz_start, gpu->display_horiz_end);
    gpu_update_display_mapping(gpu);
}

/** GP1(0x07): Display Vertical sync and display range */
static void gp1_display_vertical_range(Gpu* gpu, uint32_t value) {
    // Bits 0-9: Vsync Start coordinate (scanline units)
    // Bits 10-19: Vsync End coordinate (scanline units)
    gpu->display_line_start = (uint16_t)(value & 0x3FF);
    gpu->display_line_end = (uint16_t)((value >> 10) & 0x3FF);
    LOG_GPU_DEBUG("GPU: Display V-Range Start=%u End=%u (GP1 Cmd 0x07)",
        gpu->display_line_start, gpu->display_line_end);
    gpu_update_display_mapping(gpu);
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
    
    // Calculate effective width/height based on resolution settings
    // This is a simplified calculation. Real hardware is more complex.
    uint16_t width = 256;
    switch (gpu->hres_raw.hr1 | (gpu->hres_raw.hr2 << 2)) {
        case 0: width = 256; break;
        case 1: width = 320; break; // Most common
        case 2: width = 512; break;
        case 3: width = 640; break;
        case 4: width = 368; break; // Rare
        default: width = 256; break; // Should not happen
    }
    
    // If interlaced is enabled, treat the display height as 480 lines even when vres bit is 0 (240p mode).
    uint16_t height = (gpu->interlaced || gpu->vres == Y480Lines) ? 480 : 240;
    
    // Update renderer screen scale
    // Note: This affects how coordinates are mapped to NDC.
    // If the game draws to a 320x240 area, but we set scale to 1024x512,
    // the drawing will be small. We should probably set scale to the *drawing area* size
    // or keep it at VRAM size and let the viewport handle scaling?
    // The guide suggests mapping VRAM coordinates directly.
    // If we map 0..1024 to -1..1, then a 320-wide drawing will be small.
    // BUT, the vertex shader expects VRAM coordinates.
    // If we want 0..320 to map to -1..1, we should pass 320 as width.
    
    // Store the base width/height and refresh mapping.
        gpu->display_width_hint = width;
        gpu->display_height_hint = height;
    gpu_update_display_mapping(gpu);

    LOG_GPU_DEBUG("GPU: Display Mode set (GP1 Cmd 0x08) -> Res: %ux%u\n", width, height);
}


// --- GP0 Command Handler Definitions ---

/** GP0(0x00): No Operation */
static void gp0_nop(Gpu* gpu) {
    (void)gpu; // Does nothing
}

/** GP0(0x01): Clear Cache (Texture Cache Invalidation) */
static void gp0_clear_cache(Gpu* gpu) {
    LOG_GPU_DEBUG("GP0(0x01): Clear Cache (no-op)");
    (void)gpu;
}

/** GP0(0x02): Fill Rectangle in VRAM */
static void gp0_fill_rectangle(Gpu* gpu) {
    if (gpu->gp0_command_buffer.count < 3) {
        LOG_GPU_ERROR("GP0(0x02) Error: Expected 3 words, got %u\n", gpu->gp0_command_buffer.count);
        return;
    }

    uint32_t color_val = gpu->gp0_command_buffer.buffer[0];
    uint32_t pos_val = gpu->gp0_command_buffer.buffer[1];
    uint32_t dim_val = gpu->gp0_command_buffer.buffer[2];

    RendererColor col = {
        .r = (GLubyte)(color_val & 0xFF),
        .g = (GLubyte)((color_val >> 8) & 0xFF),
        .b = (GLubyte)((color_val >> 16) & 0xFF)
    };

    int16_t x = (int16_t)(pos_val & 0xFFFF);
    int16_t y = (int16_t)(pos_val >> 16);
    uint16_t w = (uint16_t)(dim_val & 0xFFFF);
    uint16_t h = (uint16_t)(dim_val >> 16);

    // Align to 16 pixels as per hardware behavior (lower 4 bits ignored)
    x = x & ~0xF;
    w = (w + 0xF) & ~0xF;

    // GP0(0x02) ignores the drawing offset.
    // The renderer adds the offset, so we must subtract it to get the correct absolute position.
    int16_t adj_x = x - gpu->drawing_x_offset;
    int16_t adj_y = y - gpu->drawing_y_offset;

    LOG_GPU_DEBUG("GP0(0x02): Fill Rect (%d,%d) %dx%d Color=%06x", x, y, w, h, color_val & 0xFFFFFF);

    draw_rectangle(gpu, adj_x, adj_y, w, h, col, false, false, NULL, 0, 0);

    // Update VRAM (CPU-side) to ensure textures are correct and mask bits are cleared
    // Convert 24-bit RGB to 15-bit BGR (Bit 15 = 0)
    uint16_t r5 = (col.r >> 3) & 0x1F;
    uint16_t g5 = (col.g >> 3) & 0x1F;
    uint16_t b5 = (col.b >> 3) & 0x1F;
    uint16_t pixel = r5 | (g5 << 5) | (b5 << 10); 
    // Bit 15 is 0 (Mask cleared)

    for (int iy = 0; iy < h; iy++) {
        int vram_y = (y + iy) & 0x1FF;
        for (int ix = 0; ix < w; ix++) {
            int vram_x = (x + ix) & 0x3FF;
            uint32_t offset = (uint32_t)vram_y * VRAM_WIDTH * VRAM_BPP + (uint32_t)vram_x * VRAM_BPP;
            vram_write_masked(gpu, offset, pixel);
        }
    }
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
    // Log draw mode / TPage base for diagnosis
    LOG_GPU_INFO("GP0(0xE1): Draw Mode set page_base=(%u,%u) texture_depth=%d semi_trans=%u draw_to_display=%u tex_disable=%u flip=(%u,%u)",
                 gpu->page_base_x, gpu->page_base_y, (int)gpu->texture_depth, gpu->semi_transparency,
                 (int)gpu->draw_to_display, (int)gpu->texture_disable,
                 (int)gpu->rectangle_texture_x_flip, (int)gpu->rectangle_texture_y_flip);
}

/** GP0(0xE2): Set Texture Window */
static void gp0_texture_window(Gpu* gpu) {
     uint32_t value = gpu->gp0_command_buffer.buffer[0];
     gpu->texture_window_x_mask    = (uint8_t)(value & 0x1F);
     gpu->texture_window_y_mask    = (uint8_t)((value >> 5) & 0x1F);
     gpu->texture_window_x_offset  = (uint8_t)((value >> 10) & 0x1F);
     gpu->texture_window_y_offset  = (uint8_t)((value >> 15) & 0x1F);
     
     renderer_set_texture_window(&gpu->renderer, 
        gpu->texture_window_x_mask, gpu->texture_window_y_mask,
        gpu->texture_window_x_offset, gpu->texture_window_y_offset);
        
     LOG_GPU_DEBUG("GP0(0xE2): Texture Window -> Mask(%u,%u) Offset(%u,%u)", 
        gpu->texture_window_x_mask, gpu->texture_window_y_mask,
        gpu->texture_window_x_offset, gpu->texture_window_y_offset);
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
    gpu_update_display_mapping(gpu);
}

/** GP0(0xE6): Set Mask Bit Setting */
static void gp0_mask_bit_setting(Gpu* gpu) {
     uint32_t value = gpu->gp0_command_buffer.buffer[0];
     gpu->force_set_mask_bit = (value & 1);        // Affects drawing
     gpu->preserve_masked_pixels = ((value >> 1) & 1); // Affects drawing
     LOG_GPU_DEBUG("GP0(0xE6): Mask Bit Setting = Force:%d Preserve:%d", gpu->force_set_mask_bit, gpu->preserve_masked_pixels);
}

/** Recompute renderer screen scale and draw offset based on display area and drawing offset. */
static void gpu_update_display_mapping(Gpu* gpu) {
    // Use full VRAM dimensions for renderer screen scale so that the
    // renderer always maps VRAM coordinates 0..1023 / 0..511 directly to
    // the window. This preserves the visible VRAM border (black area)
    // and matches the classic PlayStation look where the display area is
    // a region inside the full VRAM view.
    renderer_set_screen_scale(&gpu->renderer, VRAM_WIDTH, VRAM_HEIGHT);

    // Draw offset maps VRAM coordinates to screen coordinates.
    // The offset is applied as-is from GP0(0xE5), without subtracting display VRAM start.
    // The display area (defined by GP1 0x05) is handled by the drawing area bounds.
    renderer_set_draw_offset(&gpu->renderer, gpu->drawing_x_offset, gpu->drawing_y_offset);
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

    static int log_limiter = 0;
    if (log_limiter < 20) {
        // Debug VRAM content for this draw call
        uint16_t clut_x = (clut & 0x3F) * 16;
        uint16_t clut_y = (clut >> 6) & 0x1FF;
        uint16_t clut_val = vram_load16(&gpu->vram, (uint32_t)clut_y * VRAM_WIDTH * VRAM_BPP + clut_x * VRAM_BPP);

        uint16_t page_x = (texpage & 0xF) * 64;
        uint16_t page_y = ((texpage >> 4) & 1) * 256;
        uint16_t tex_u = t[0].u;
        uint16_t tex_v = t[0].v;
        // Assuming 4-bit for BIOS font
        uint16_t tex_addr_x = page_x + (tex_u / 4);
        uint16_t tex_addr_y = page_y + tex_v;
        uint16_t tex_val = vram_load16(&gpu->vram, (uint32_t)tex_addr_y * VRAM_WIDTH * VRAM_BPP + tex_addr_x * VRAM_BPP);

         LOG_GPU_INFO("GP0(0x2C): V0(%d,%d) UV(%d,%d) CLUT=%04x TPage=%04x Color=%02x%02x%02x | VRAM Peek: CLUT[%d,%d]=%04x Tex[%d,%d]=%04x DisplayStart=(%u,%u) DispHint=(%u,%u) RendererScale=(%ux%u)", 
               p[0].x, p[0].y, t[0].u, t[0].v, clut, texpage, c0.r, c0.g, c0.b,
             clut_x, clut_y, clut_val, tex_addr_x, tex_addr_y, tex_val,
             gpu->display_vram_x_start, gpu->display_vram_y_start,
             gpu->display_width_hint, gpu->display_height_hint,
             (uint32_t)gpu->renderer.screen_width, (uint32_t)gpu->renderer.screen_height);
        log_limiter++;
    }

    // Sync VRAM to GPU texture before textured draw
        renderer_upload_vram(&gpu->renderer, (const uint16_t*)gpu->vram.data);

        bool raw_texture = ((gpu->gp0_command_buffer.buffer[0] & 0x01000000) != 0) || ((gpu->gp0_command_buffer.buffer[0] >> 24) & 1);
        renderer_set_raw_texture_mode(&gpu->renderer, raw_texture);
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
static void draw_rectangle(Gpu* gpu, int16_t x, int16_t y, uint16_t w, uint16_t h, RendererColor col, bool textured, bool raw_texture, RendererTexCoord* tex, uint16_t clut, uint16_t tpage) {
    RendererPosition p[4];
    RendererColor c[4] = {col, col, col, col};
    RendererTexCoord t[4];
    
    // Rectangle vertices: top-left, top-right, bottom-left, bottom-right
    p[0].x = x;       p[0].y = y;
    p[1].x = x + w;   p[1].y = y;
    p[2].x = x;       p[2].y = y + h;
    p[3].x = x + w;   p[3].y = y + h;
    
    bool use_texture = textured && tex;

    if (use_texture) {
        // Ensure VRAM texture is up to date before textured draws.
        renderer_upload_vram(&gpu->renderer, (const uint16_t*)gpu->vram.data);
        // UV coordinates are texture coordinates (0-255 within texture page)
        // They map to the rectangle dimensions but don't add the dimensions themselves
        // DuckStation: texcoord + offset within primitive (for rectangles, offset increments per pixel)
        t[0].u = tex->u;           t[0].v = tex->v;
        t[1].u = tex->u + (w-1);   t[1].v = tex->v;
        t[2].u = tex->u;           t[2].v = tex->v + (h-1);
        t[3].u = tex->u + (w-1);   t[3].v = tex->v + (h-1);
        renderer_set_raw_texture_mode(&gpu->renderer, raw_texture);
        renderer_set_texture_mode(&gpu->renderer, true);
        renderer_push_quad(&gpu->renderer, p, c, t, clut, tpage);
    } else {
        renderer_set_raw_texture_mode(&gpu->renderer, false);
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
    
    draw_rectangle(gpu, x, y, w, h, col, false, false, NULL, 0, 0);
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
    
    static int log_limiter_rect = 0;
    if (log_limiter_rect < 20) {
        LOG_GPU_DEBUG("GP0(0x64): RectTex at (%d,%d) %dx%d UV(%d,%d) CLUT=%04x TPage=%04x", 
           x, y, w, h, tex.u, tex.v, clut, tpage);
        log_limiter_rect++;
    }

    uint8_t opcode = (uint8_t)(cmd >> 24);
    bool raw_texture = ((cmd & 0x01000000) != 0) || (opcode & 1); // Raw variants (0x65/67) set LSB
    draw_rectangle(gpu, x, y, w, h, col, true, raw_texture, &tex, clut, tpage);
}

/** GP0(0x68): Monochrome Rectangle 1x1 (single pixel) */
static void gp0_rect_1x1_opaque(Gpu* gpu) {
    if (gpu->gp0_command_buffer.count < 2) return;
    uint32_t cmd = gpu->gp0_command_buffer.buffer[0];
    uint32_t vtx = gpu->gp0_command_buffer.buffer[1];
    
    RendererColor col = { .r = (GLubyte)(cmd & 0xFF), .g = (GLubyte)((cmd >> 8) & 0xFF), .b = (GLubyte)((cmd >> 16) & 0xFF) };
    int16_t x = (int16_t)(vtx & 0xFFFF);
    int16_t y = (int16_t)(vtx >> 16);
    
    draw_rectangle(gpu, x, y, 1, 1, col, false, false, NULL, 0, 0);
}

/** GP0(0x70): Monochrome Rectangle 8x8 */
static void gp0_rect_8x8_opaque(Gpu* gpu) {
    if (gpu->gp0_command_buffer.count < 2) return;
    uint32_t cmd = gpu->gp0_command_buffer.buffer[0];
    uint32_t vtx = gpu->gp0_command_buffer.buffer[1];
    
    RendererColor col = { .r = (GLubyte)(cmd & 0xFF), .g = (GLubyte)((cmd >> 8) & 0xFF), .b = (GLubyte)((cmd >> 16) & 0xFF) };
    int16_t x = (int16_t)(vtx & 0xFFFF);
    int16_t y = (int16_t)(vtx >> 16);
    
    draw_rectangle(gpu, x, y, 8, 8, col, false, false, NULL, 0, 0);
}

/** GP0(0x78): Monochrome Rectangle 16x16 */
static void gp0_rect_16x16_opaque(Gpu* gpu) {
    if (gpu->gp0_command_buffer.count < 2) return;
    uint32_t cmd = gpu->gp0_command_buffer.buffer[0];
    uint32_t vtx = gpu->gp0_command_buffer.buffer[1];
    
    RendererColor col = { .r = (GLubyte)(cmd & 0xFF), .g = (GLubyte)((cmd >> 8) & 0xFF), .b = (GLubyte)((cmd >> 16) & 0xFF) };
    int16_t x = (int16_t)(vtx & 0xFFFF);
    int16_t y = (int16_t)(vtx >> 16);
    
    draw_rectangle(gpu, x, y, 16, 16, col, false, false, NULL, 0, 0);
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
    
    uint8_t opcode = (uint8_t)(cmd >> 24);
    bool raw_texture = ((cmd & 0x01000000) != 0) || (opcode & 1);
    draw_rectangle(gpu, x, y, 1, 1, col, true, raw_texture, &tex, clut, tpage);
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
    
    uint8_t opcode = (uint8_t)(cmd >> 24);
    bool raw_texture = ((cmd & 0x01000000) != 0) || (opcode & 1);
    draw_rectangle(gpu, x, y, 8, 8, col, true, raw_texture, &tex, clut, tpage);
}

/** GP0(0x7C): Textured Rectangle 16x16 */
static void gp0_rect_tex_16x16_opaque(Gpu* gpu) {
    LOG_GPU_INFO(">>> gp0_rect_tex_16x16_opaque() CALLED! buffer count=%d\n", gpu->gp0_command_buffer.count);
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
    
    uint8_t opcode = (uint8_t)(cmd >> 24);
    bool raw_texture = ((cmd & 0x01000000) != 0) || (opcode & 1);
    
    // Debug: Log first few textured rectangles to verify parameters
    static int debug_count = 0;
    if (debug_count < 10) {
        LOG_GPU_INFO("[GPU] GP0(0x%02X) Textured Rect 16x16: Cmd=0x%08X Vtx=0x%08X UV_CLUT=0x%08X -> Pos(%d,%d) UV(%d,%d) CLUT=0x%04X TPage=0x%04X (page_base=%d,depth=%d)\n",
                      opcode, cmd, vtx, uv_clut, x, y, tex.u, tex.v, clut, tpage, gpu->page_base_x, gpu->texture_depth);
        debug_count++;
    }
    
    draw_rectangle(gpu, x, y, 16, 16, col, true, raw_texture, &tex, clut, tpage);
}

/** GP0(0x80): Copy Rectangle (VRAM to VRAM) */
static void gp0_copy_rectangle(Gpu* gpu) {
    if (gpu->gp0_command_buffer.count < 4) {
        LOG_GPU_ERROR("GP0(0x80) Error: Expected 4 words, got %u\n", gpu->gp0_command_buffer.count);
        return;
    }

    uint32_t src_val = gpu->gp0_command_buffer.buffer[1];
    uint32_t dst_val = gpu->gp0_command_buffer.buffer[2];
    uint32_t dim_val = gpu->gp0_command_buffer.buffer[3];

    uint16_t src_x = (uint16_t)(src_val & 0x3FF);
    uint16_t src_y = (uint16_t)((src_val >> 16) & 0x1FF);
    uint16_t dst_x = (uint16_t)(dst_val & 0x3FF);
    uint16_t dst_y = (uint16_t)((dst_val >> 16) & 0x1FF);
    uint16_t w = (uint16_t)(dim_val & 0x3FF);
    uint16_t h = (uint16_t)((dim_val >> 16) & 0x1FF);

    // Handle 0 size as max size (common PS1 behavior for some commands)
    if (w == 0) w = 1024;
    if (h == 0) h = 512;

    LOG_GPU_DEBUG("GP0(0x80): Copy Rect (%u,%u) -> (%u,%u) Size=(%ux%u)", src_x, src_y, dst_x, dst_y, w, h);

    // Determine copy direction to handle overlaps
    int16_t step_x = 1, step_y = 1;
    int16_t start_x = 0, start_y = 0;
    int16_t end_x = w, end_y = h;

    if (dst_y > src_y) { step_y = -1; start_y = h - 1; end_y = -1; }
    if (dst_x > src_x) { step_x = -1; start_x = w - 1; end_x = -1; }

    for (int16_t y = start_y; y != end_y; y += step_y) {
        for (int16_t x = start_x; x != end_x; x += step_x) {
            uint16_t sx = (src_x + x) & 0x3FF; // Wrap mask 1024
            uint16_t sy = (src_y + y) & 0x1FF; // Wrap mask 512
            uint16_t dx = (dst_x + x) & 0x3FF;
            uint16_t dy = (dst_y + y) & 0x1FF;

            uint32_t src_offset = (uint32_t)sy * VRAM_WIDTH * VRAM_BPP + (uint32_t)sx * VRAM_BPP;
            uint32_t dst_offset = (uint32_t)dy * VRAM_WIDTH * VRAM_BPP + (uint32_t)dx * VRAM_BPP;

            uint16_t pixel = vram_load16(&gpu->vram, src_offset);
            
            // Apply Mask Bit Setting (Force Set)
            if (gpu->force_set_mask_bit) {
                pixel |= 0x8000;
            }
            
            // Apply Mask Bit Setting (Check Mask)
            if (gpu->preserve_masked_pixels) {
                uint16_t dst_pixel = vram_load16(&gpu->vram, dst_offset);
                if (dst_pixel & 0x8000) {
                    continue; // Skip writing this pixel
                }
            }

            vram_store16(&gpu->vram, dst_offset, pixel);
        }
    }

    // Update the OpenGL texture to reflect the changes
    renderer_upload_vram(&gpu->renderer, (const uint16_t*)gpu->vram.data);
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

    LOG_GPU_INFO("*** GP0(0xA0): VRAM UPLOAD START -> Dest(%u,%u) Size(%ux%u) = %u words [FONT/TEXTURE DATA?] ***",
           gpu->vram_load_x, gpu->vram_load_y, gpu->vram_load_w, gpu->vram_load_h, words_to_load);

    if (words_to_load == 0 || ((uint64_t)words_to_load * 4) > VRAM_SIZE) { // Basic sanity check
        LOG_GPU_WARN("Warning: Invalid image load size %u words requested.\n", words_to_load);
        gpu->gp0_words_remaining = 0; gpu->gp0_mode = GP0_MODE_COMMAND; return; }

    gpu->gp0_words_remaining = words_to_load;
    gpu->gp0_mode = GP0_MODE_IMAGE_LOAD;
    gpu->vram_load_count = 0; // Reset pixel counter for this transfer
    
    LOG_GPU_INFO("GP0(0xA0): Switched to IMAGE_LOAD mode, words_remaining=%u", gpu->gp0_words_remaining);
}

// Add completion logging at the end of VRAM uploads

/** GP0(0xC0): Copy Rectangle (VRAM to CPU/DMA) */
static void gp0_image_store(Gpu* gpu) {
    if (gpu->gp0_command_buffer.count < 3) {
        LOG_GPU_ERROR("GP0(0xC0) Error: Expected 3 words, got %u\n", gpu->gp0_command_buffer.count);
        return;
    }

    // Extract parameters
    uint32_t val1 = gpu->gp0_command_buffer.buffer[1];
    uint32_t val2 = gpu->gp0_command_buffer.buffer[2];

    uint16_t x = (uint16_t)(val1 & 0x3FF);
    uint16_t y = (uint16_t)((val1 >> 16) & 0x1FF);
    uint16_t w = (uint16_t)(val2 & 0xFFFF);
    uint16_t h = (uint16_t)(val2 >> 16);

    // Clamp to VRAM bounds
    if (x >= VRAM_WIDTH) x = VRAM_WIDTH - 1;
    if (y >= VRAM_HEIGHT) y = VRAM_HEIGHT - 1;
    if (w == 0 || w > VRAM_WIDTH) w = VRAM_WIDTH;
    if (h == 0 || h > VRAM_HEIGHT) h = VRAM_HEIGHT;

    // Store region in GPU struct
    gpu->vram_load_x = x;
    gpu->vram_load_y = y;
    gpu->vram_load_w = w;
    gpu->vram_load_h = h;

    // Compute total pixels and words
    uint32_t total_pixels = (uint32_t)w * h;
    uint32_t words_to_transfer = (total_pixels + 1) / 2;

    // Set up transfer state
    gpu->gp0_words_remaining = words_to_transfer;
    gpu->vram_load_count = 0; // Reset pixel counter
    gpu->gp0_mode = GP0_MODE_IMAGE_STORE;

    LOG_GPU_INFO("GP0(0xC0): VRAM-to-CPU transfer START (%u,%u) Size=(%ux%u) = %u words", x, y, w, h, words_to_transfer);
    LOG_GPU_DEBUG("GP0(0xC0): Image Store region set (%u,%u) Size=(%ux%u), Words=%u", x, y, w, h, words_to_transfer);
}


// --- Main GPU Public Functions ---

// NOTE: gpu_init_full() and gpu_soft_reset() moved to src/gpu/gpu_core.c
// These implementations have been removed to prevent duplicate definitions

/** Helper to write to VRAM with mask bit handling */
static void vram_write_masked(Gpu* gpu, uint32_t offset, uint16_t pixel) {
    if (gpu->force_set_mask_bit) {
        pixel |= 0x8000;
    }
    
    // TEMPORARY FIX: Disable mask check to see if it fixes font upload
    /*
    if (gpu->preserve_masked_pixels) {
        uint16_t dst_pixel = vram_load16(&gpu->vram, offset);
        if (dst_pixel & 0x8000) {
            return; // Skip writing this pixel
        }
    }
    */
    
    vram_store16(&gpu->vram, offset, pixel);
    
    // Debug: Verify write
    uint16_t verify = vram_load16(&gpu->vram, offset);
    if (pixel != verify) {
        LOG_GPU_WARN("VRAM Write verification failed at offset 0x%x: wrote 0x%04x, read back 0x%04x", 
            offset, pixel, verify);
    }
}

/** Internal: process a single GP0 word (previous gpu_gp0 body moved here) */
void gpu_gp0_handle_word(Gpu* gpu, uint32_t command) {
    // Rate-limit GP0 command logging
    static uint32_t gp0_cmd_count = 0;
    gp0_cmd_count++;
    if (gp0_cmd_count <= 20 || gp0_cmd_count % 1000 == 0) {
        LOG_GPU_DEBUG("[GP0] Command: 0x%08x (Opcode: 0x%02x) Mode=%d #%u", command, (command >> 24) & 0xFF, gpu->gp0_mode, gp0_cmd_count);
    }
    // Handle IMAGE_LOAD state first
    if (gpu->gp0_mode == GP0_MODE_IMAGE_LOAD) {
        // Log first few data words
        static int imgload_data_count = 0;
        if (imgload_data_count < 5) {
            LOG_GPU_DEBUG("[IMAGE_LOAD] Receiving data word: 0x%08x (remaining=%u)", command, gpu->gp0_words_remaining);
            imgload_data_count++;
        }
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
                 vram_write_masked(gpu, offset, pixel1);
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
                 vram_write_masked(gpu, offset, pixel2);
             } // Else: Pixel write out of VRAM bounds (optional warning)
        }
        gpu->vram_load_count += 2; // Increment count by 2 pixels
        gpu->gp0_words_remaining--; // Decrement remaining data words
        if (gpu->gp0_words_remaining == 0) { // Check if transfer complete
            gpu->gp0_mode = GP0_MODE_COMMAND; // Switch back to command mode
            // Sample VRAM to verify upload worked
            uint16_t sample_val = vram_load16(&gpu->vram, (uint32_t)gpu->vram_load_y * VRAM_WIDTH * VRAM_BPP + (uint32_t)gpu->vram_load_x * VRAM_BPP);
            LOG_GPU_INFO("*** GP0(0xA0): VRAM UPLOAD COMPLETE -> Region(%u,%u) Size(%ux%u) | Sample[0,0]=0x%04x ***", 
                gpu->vram_load_x, gpu->vram_load_y, gpu->vram_load_w, gpu->vram_load_h, sample_val);
            renderer_upload_vram(&gpu->renderer, (const uint16_t*)gpu->vram.data);
        }
        return; // Done processing this data word
    }

    // Handle COMMAND mode
    // Check if this looks like a NEW command opcode (DMA packet boundary detection)
    // We only force if: words_remaining > 1 (not just waiting for last word) AND top 3 bits indicate command type
    uint8_t opcode_check = (uint8_t)(command >> 24);
    uint8_t cmd_type = (opcode_check >> 5) & 0x7; // Top 3 bits
    bool is_render_cmd = (cmd_type >= 1 && cmd_type <= 3); // 001=polygon, 010=line, 011=rect
    bool is_env_cmd = (cmd_type == 7 && opcode_check >= 0xE0); // 111=environment
    bool looks_like_new_cmd = (gpu->gp0_words_remaining > 2) && (is_render_cmd || is_env_cmd);
    
    if (gpu->gp0_words_remaining == 0 || looks_like_new_cmd) {
        if (looks_like_new_cmd) {
            LOG_GPU_WARN("GPU: Forcing new command 0x%02X while %d words remaining for 0x%02X (DMA packet boundary)\n", 
                        opcode_check, gpu->gp0_words_remaining, gpu->gp0_current_opcode);
        }
        // Start of a new command
        uint8_t opcode = opcode_check;
        uint32_t expected_len = 0; void (*handler)(Gpu*) = NULL;
        gpu->gp0_current_opcode = opcode; clear_gp0_command_buffer(gpu);
        gpu->gp0_words_remaining = 0; // Reset

        static int opcode_log_count = 0;
        if (opcode_log_count < 300 && (opcode >= 0x60 && opcode <= 0x7F)) {
            LOG_GPU_INFO(">>> GPU DISPATCH: opcode=0x%02X command=0x%08X\n", opcode, command);
            opcode_log_count++;
        }

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
            case 0x78: expected_len = 3; handler = gp0_rect_tex_16x16_opaque; break; // TEXTURED 16x16
            case 0x7A: expected_len = 3; handler = gp0_rect_tex_16x16_opaque; break; // TEXTURED 16x16 Semi-trans
            case 0x7C: expected_len = 2; handler = gp0_rect_16x16_opaque; break; // NON-TEXTURED 16x16
            case 0x7D: expected_len = 2; handler = gp0_rect_16x16_opaque; break; // NON-TEXTURED 16x16 Raw
            case 0x7E: expected_len = 2; handler = gp0_rect_16x16_opaque; break; // NON-TEXTURED 16x16 Semi-trans
            case 0x7F: expected_len = 2; handler = gp0_rect_16x16_opaque; break; // NON-TEXTURED 16x16 Semi-trans raw
            case 0x80: expected_len = 4; handler = gp0_copy_rectangle; break;
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

    // Debug: Log 0x78 command word buffering
    if (gpu->gp0_current_opcode == 0x78) {
        LOG_GPU_INFO(">>> 0x78: Buffered word, buffer_count=%d words_remaining=%d\n", 
                     gpu->gp0_command_buffer.count, gpu->gp0_words_remaining);
    }

    // If all words for the command received, execute the handler
    if (gpu->gp0_words_remaining == 0) {
         if (gpu->gp0_command_method != NULL) {
             if (gpu->gp0_current_opcode == 0x78) {
                 LOG_GPU_INFO(">>> About to call handler for GP0(0x78), buffer count=%d\n", gpu->gp0_command_buffer.count);
             }
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

/** Public APi: enqueue GP0 word into hardware FIFO and process available words. */
void gpu_gp0(Gpu* gpu, uint32_t command) {
    uint8_t opcode = (uint8_t)(command >> 24);
    static int gp0_log_count = 0;
    if (gp0_log_count < 300 && (opcode >= 0x60 && opcode <= 0x7F)) {
        LOG_GPU_INFO("### gpu_gp0() ENTRY: opcode=0x%02X command=0x%08X words_remaining=%d current_opcode=0x%02X\n", 
                     opcode, command, gpu->gp0_words_remaining, gpu->gp0_current_opcode);
        gp0_log_count++;
    }
    
    // If FIFO full, try to drain a bit before enqueueing (avoid dropping words)
    if (gpu->gp0_fifo_count >= 16) {
        LOG_GPU_WARN("GP0 FIFO full: draining before enqueue (was full)");
        while (gpu->gp0_fifo_count > 0) {
            uint32_t w = gpu->gp0_fifo[gpu->gp0_fifo_head];
            gpu->gp0_fifo_head = (uint8_t)((gpu->gp0_fifo_head + 1) & 0x0F);
            gpu->gp0_fifo_count--;
            gpu_gp0_handle_word(gpu, w);
            // If a command transitions to IMAGE_LOAD mode, the handler will process data words
        }
    }

    // Enqueue the incoming word (space should now be available)
    gpu->gp0_fifo[gpu->gp0_fifo_tail] = command;
    gpu->gp0_fifo_tail = (uint8_t)((gpu->gp0_fifo_tail + 1) & 0x0F);
    gpu->gp0_fifo_count++;

    // Process as many words as possible from FIFO
    while (gpu->gp0_fifo_count > 0) {
        uint32_t w = gpu->gp0_fifo[gpu->gp0_fifo_head];
        gpu->gp0_fifo_head = (uint8_t)((gpu->gp0_fifo_head + 1) & 0x0F);
        gpu->gp0_fifo_count--;
        gpu_gp0_handle_word(gpu, w);
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
    // --- STAT Ready bits: reflect GP0 FIFO availability ---
    // STAT[26] - Ready to receive command word (1 = ready)
    if (gpu->gp0_fifo_count < 16) r |= (1 << 26);
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
            return 0xFFFFFFFF;
        }

        uint32_t word = 0;
        uint16_t pixel1 = 0, pixel2 = 0;
        uint32_t idx = gpu->vram_load_count;
        uint16_t x1 = gpu->vram_load_x + (uint16_t)(idx % gpu->vram_load_w);
        uint16_t y1 = gpu->vram_load_y + (uint16_t)(idx / gpu->vram_load_w);
        x1 &= 0x3FF; y1 &= 0x1FF;
        uint32_t offset1 = (uint32_t)y1 * VRAM_WIDTH * VRAM_BPP + (uint32_t)x1 * VRAM_BPP;
        pixel1 = vram_load16(&gpu->vram, offset1);
        gpu->vram_load_count++;

        // Pixel 2
        if (gpu->vram_load_count < ((uint32_t)gpu->vram_load_w * gpu->vram_load_h)) {
            idx = gpu->vram_load_count;
            uint16_t x2 = gpu->vram_load_x + (uint16_t)(idx % gpu->vram_load_w);
            uint16_t y2 = gpu->vram_load_y + (uint16_t)(idx / gpu->vram_load_w);
            x2 &= 0x3FF; y2 &= 0x1FF;
            uint32_t offset2 = (uint32_t)y2 * VRAM_WIDTH * VRAM_BPP + (uint32_t)x2 * VRAM_BPP;
            pixel2 = vram_load16(&gpu->vram, offset2);
            gpu->vram_load_count++;
        } else {
            pixel2 = 0; // Pad if odd
        }

        word = (uint32_t)pixel1 | ((uint32_t)pixel2 << 16);
        gpu->gp0_words_remaining--;

        // Log progress
        static uint32_t gpuread_count = 0;
        gpuread_count++;
        if (gpuread_count <= 10 || gpu->gp0_words_remaining == 0) {
            LOG_GPU_DEBUG("[GPUREAD] VRAM-to-CPU: 0x%08x (Rem: %u)", word, gpu->gp0_words_remaining);
        }

        if (gpu->gp0_words_remaining == 0) {
            LOG_GPU_INFO("GP0(0xC0): VRAM-to-CPU transfer COMPLETE");
            /* Dump a small region around the transferred area for debug */
            gpu_debug_dump_region(gpu);
            gpu->gp0_mode = GP0_MODE_COMMAND;
        }

        return word;
    }

    // Fallback for non-transfer reads
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

/**
 * Debug helper: dump a small region of VRAM around the last load/store area.
 * Limits output to a few rows/columns to avoid huge logs.
 */
static void gpu_debug_dump_region(Gpu* gpu) {
    uint16_t x = gpu->vram_load_x;
    uint16_t y = gpu->vram_load_y;
    uint16_t w = gpu->vram_load_w;
    uint16_t h = gpu->vram_load_h;
    if (w == 0 || h == 0) {
        LOG_GPU_DEBUG("gpu_debug_dump_region: empty region (w=0/h=0)");
        return;
    }

    uint16_t dump_w = (w > 16) ? 16 : w;
    uint16_t dump_h = (h > 4) ? 4 : h;

    LOG_GPU_INFO("GPU VRAM DUMP Region (%u,%u) Size=(%ux%u) - showing %ux%u", x, y, w, h, dump_w, dump_h);

    for (uint16_t row = 0; row < dump_h; ++row) {
        char line[512];
        int pos = 0;
        pos += snprintf(line + pos, sizeof(line) - pos, "VRAM[%u,%u]:", x, (uint16_t)(y + row));
        for (uint16_t col = 0; col < dump_w; ++col) {
            uint16_t xx = (x + col) & 0x3FF;
            uint16_t yy = (y + row) & 0x1FF;
            uint32_t offset = (uint32_t)yy * VRAM_WIDTH * VRAM_BPP + (uint32_t)xx * VRAM_BPP;
            uint16_t val = vram_load16(&gpu->vram, offset);
            pos += snprintf(line + pos, sizeof(line) - pos, " %04x", val);
            if (pos > (int)sizeof(line) - 64) break; // safety
        }
        LOG_GPU_INFO("%s", line);
    }
}