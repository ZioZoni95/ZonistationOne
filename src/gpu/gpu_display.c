/**
 * gpu_display.c
 * GPU Display/CRTC Control Implementation
 * 
 * Based on DuckStation's display management with adaptations
 */

#include "gpu/gpu_display.h"
#include "gpu/gpu_core.h"
#include "gpu/gpu_commands.h"
#include "gpu/gpu_rendering.h"
#include "renderer.h"
#include "log.h"

// ============================================================================
// GP1 Command Implementations
// ============================================================================

void gp1_reset(GPU* gpu, uint32_t value) {
    (void)value;
    
    LOG_GPU_INFO("GP1(0x00): GPU Reset");
    
    // Reset command processing state
    gp0_clear_command_buffer(gpu);
    gpu->gp0_words_remaining = 0;
    gpu->gp0_mode = GP0_MODE_COMMAND;
    gpu->gp0_current_opcode = 0xFF;
    gpu->gp0_command_method = NULL;
    
    // Clear hardware FIFO
    gpu->gp0_fifo_head = 0;
    gpu->gp0_fifo_tail = 0;
    gpu->gp0_fifo_count = 0;
    
    // Reset GPUSTAT bits (mimics hardware behavior)
    gpu->texture_disable = false;
    gpu->dithering = false;
    gpu->draw_to_display = false;
    gpu->force_set_mask_bit = false;
    gpu->preserve_masked_pixels = false;
    gpu->interlaced = false;
    gpu->display_disabled = false;  // Enable display by default for BIOS
    gpu->interrupt = false;
    
    // Reset texture settings
    gpu->page_base_x = 0;
    gpu->page_base_y = 0;
    gpu->semi_transparency = 0;
    gpu->texture_depth = GPU_TEXTURE_MODE_PALETTE_4BIT;
    gpu->rectangle_texture_x_flip = false;
    gpu->rectangle_texture_y_flip = false;
    
    // Reset drawing area
    gpu->drawing_area_left = 0;
    gpu->drawing_area_top = 0;
    gpu->drawing_area_right = 0;
    gpu->drawing_area_bottom = 0;
    
    // Reset drawing offset
    gpu->drawing_x_offset = 0;
    gpu->drawing_y_offset = 0;
    
    // Reset texture window
    gpu->texture_window_x_mask = 0;
    gpu->texture_window_y_mask = 0;
    gpu->texture_window_x_offset = 0;
    gpu->texture_window_y_offset = 0;
    
    // Reset display configuration
    gpu->display_vram_x_start = 0;
    gpu->display_vram_y_start = 0;
    gpu->display_horiz_start = 0x200;
    gpu->display_horiz_end = 0xC00;
    gpu->display_line_start = 0x10;
    gpu->display_line_end = 0x100;
    
    // Reset video mode
    gpu->hres_raw.hr1 = 0;
    gpu->hres_raw.hr2 = 0;
    gpu->vres = GPU_VERTICAL_RES_240;
    gpu->vmode = GPU_VIDEO_MODE_NTSC;
    gpu->display_depth = GPU_DISPLAY_DEPTH_15BIT;
    
    // Reset DMA direction
    gpu->dma_setting = GPU_DMA_OFF;
    
    LOG_GPU_DEBUG("GP1(0x00): GPU Reset Complete");
}

void gp1_reset_command_buffer(GPU* gpu, uint32_t value) {
    (void)value;
    
    LOG_GPU_DEBUG("GP1(0x01): Reset Command Buffer");
    
    gp0_clear_command_buffer(gpu);
    gpu->gp0_words_remaining = 0;
    gpu->gp0_mode = GP0_MODE_COMMAND;
    gpu->gp0_current_opcode = 0xFF;
    gpu->gp0_command_method = NULL;
    
    // Clear hardware FIFO
    gpu->gp0_fifo_head = 0;
    gpu->gp0_fifo_tail = 0;
    gpu->gp0_fifo_count = 0;
}

void gp1_acknowledge_irq(GPU* gpu, uint32_t value) {
    (void)value;
    
    LOG_GPU_DEBUG("GP1(0x02): Acknowledge IRQ");
    
    gpu->interrupt = false; // Clear GPUSTAT bit 24
}

void gp1_display_enable(GPU* gpu, uint32_t value) {
    // Bit 0: 0 = Enable Display, 1 = Disable Display
    gpu->display_disabled = (value & 1);
    
    LOG_GPU_DEBUG("GP1(0x03): Display %s", 
                  gpu->display_disabled ? "Disabled" : "Enabled");
}

void gp1_dma_direction(GPU* gpu, uint32_t value) {
    // Bits 0-1 select DMA direction mode
    switch (value & 3) {
        case 0: 
            gpu->dma_setting = GPU_DMA_OFF; 
            break;
        case 1: 
            gpu->dma_setting = GPU_DMA_FIFO; 
            break;
        case 2: 
            gpu->dma_setting = GPU_DMA_CPU_TO_GP0; 
            break;
        case 3: 
            gpu->dma_setting = GPU_DMA_VRAM_TO_CPU; 
            break;
    }
    
    LOG_GPU_DEBUG("GP1(0x04): DMA Direction = %d", gpu->dma_setting);
}

void gp1_display_vram_start(GPU* gpu, uint32_t value) {
    // Bits 0-9: X start coordinate in VRAM (LSB ignored, must be even)
    // Bits 10-18: Y start coordinate in VRAM
    gpu->display_vram_x_start = (uint16_t)(value & 0x3FE);
    gpu->display_vram_y_start = (uint16_t)((value >> 10) & 0x1FF);
    
    LOG_GPU_DEBUG("GP1(0x05): Display VRAM Start X=%u Y=%u",
                  gpu->display_vram_x_start, gpu->display_vram_y_start);
    
    // Update display mapping for renderer
    gpu_update_display_mapping(gpu);
}

void gp1_display_horizontal_range(GPU* gpu, uint32_t value) {
    // Bits 0-11: Horizontal start (dotclock units)
    // Bits 12-23: Horizontal end (dotclock units)
    gpu->display_horiz_start = (uint16_t)(value & 0xFFF);
    gpu->display_horiz_end = (uint16_t)((value >> 12) & 0xFFF);
    
    LOG_GPU_DEBUG("GP1(0x06): Display H-Range Start=%u End=%u",
                  gpu->display_horiz_start, gpu->display_horiz_end);
    
    gpu_update_display_mapping(gpu);
}

void gp1_display_vertical_range(GPU* gpu, uint32_t value) {
    // Bits 0-9: Vertical start (scanline units)
    // Bits 10-19: Vertical end (scanline units)
    gpu->display_line_start = (uint16_t)(value & 0x3FF);
    gpu->display_line_end = (uint16_t)((value >> 10) & 0x3FF);
    
    LOG_GPU_DEBUG("GP1(0x07): Display V-Range Start=%u End=%u",
                  gpu->display_line_start, gpu->display_line_end);
    
    gpu_update_display_mapping(gpu);
}

void gp1_display_mode(GPU* gpu, uint32_t value) {
    // Bits 0-1: Horizontal Resolution 1 (GPUSTAT[17:16])
    gpu->hres_raw.hr1 = (uint8_t)(value & 3);
    
    // Bit 2: Vertical Resolution (GPUSTAT[19])
    // 0 = 240 lines, 1 = 480 lines
    gpu->vres = ((value >> 2) & 1) ? GPU_VERTICAL_RES_480 : GPU_VERTICAL_RES_240;
    
    // Bit 3: Video Mode (GPUSTAT[20])
    // 0 = NTSC (60Hz), 1 = PAL (50Hz)
    gpu->vmode = ((value >> 3) & 1) ? GPU_VIDEO_MODE_PAL : GPU_VIDEO_MODE_NTSC;
    
    // Bit 4: Display Area Color Depth (GPUSTAT[21])
    // 0 = 15-bit, 1 = 24-bit
    gpu->display_depth = ((value >> 4) & 1) ? GPU_DISPLAY_DEPTH_24BIT : GPU_DISPLAY_DEPTH_15BIT;
    
    // Bit 5: Interlaced output (GPUSTAT[22])
    // 0 = off/progressive, 1 = on
    gpu->interlaced = ((value >> 5) & 1);
    
    // Bit 6: Horizontal Resolution 2 (GPUSTAT[18])
    gpu->hres_raw.hr2 = (uint8_t)((value >> 6) & 1);
    
    // Bit 7: Reverse flag (unsupported, usually 0)
    if ((value >> 7) & 1) {
        LOG_GPU_WARN("GP1(0x08): Unsupported Reverse flag bit set");
    }
    
    // Calculate effective display dimensions
    uint32_t dims = gpu_get_display_dimensions(gpu);
    uint16_t width = (uint16_t)(dims & 0xFFFF);
    uint16_t height = (uint16_t)(dims >> 16);
    
    LOG_GPU_INFO("GP1(0x08): Display Mode hr1=%u hr2=%u vres=%s vmode=%s depth=%ubit interlace=%d -> %ux%u",
                 gpu->hres_raw.hr1, gpu->hres_raw.hr2,
                 (gpu->vres == GPU_VERTICAL_RES_480) ? "480" : "240",
                 (gpu->vmode == GPU_VIDEO_MODE_PAL) ? "PAL" : "NTSC",
                 (gpu->display_depth == GPU_DISPLAY_DEPTH_24BIT) ? 24 : 15,
                 gpu->interlaced, width, height);
    
    // Store hint for renderer
    gpu->display_width_hint = width;
    gpu->display_height_hint = height;
    
    // Update CRTC timing
    gpu_update_crtc_config(gpu);
    
    // Update renderer display mapping
    gpu_update_display_mapping(gpu);
}

// ============================================================================
// Helper Functions
// ============================================================================

uint32_t gpu_get_display_dimensions(const GPU* gpu) {
    // Calculate horizontal resolution from hr1 and hr2 bits
    // DuckStation mapping: hr1 (bits 17:16) and hr2 (bit 18)
    // Combined value determines actual width
    uint8_t hres_combined = gpu->hres_raw.hr1 | (gpu->hres_raw.hr2 << 2);
    
    uint16_t width;
    switch (hres_combined) {
        case 0: width = 256; break;
        case 1: width = 320; break; // Most common
        case 2: width = 512; break;
        case 3: width = 640; break;
        case 4: width = 368; break; // Rare
        default: width = 256; break;
    }
    
    // Calculate vertical resolution
    // If interlaced is enabled OR vres bit is set, use 480 lines
    uint16_t height = (gpu->interlaced || gpu->vres == GPU_VERTICAL_RES_480) ? 480 : 240;
    
    return ((uint32_t)height << 16) | width;
}

void gpu_update_crtc_config(GPU* gpu) {
    // DuckStation: This would update timing event intervals based on video mode
    // For our simplified emulator, we just log the configuration
    
    const char* video_mode_str = (gpu->vmode == GPU_VIDEO_MODE_PAL) ? "PAL" : "NTSC";
    uint32_t dims = gpu_get_display_dimensions(gpu);
    uint16_t width = (uint16_t)(dims & 0xFFFF);
    uint16_t height = (uint16_t)(dims >> 16);
    
    LOG_GPU_DEBUG("CRTC Config Updated: %s %ux%u %s", 
                  video_mode_str, width, height,
                  gpu->interlaced ? "Interlaced" : "Progressive");
    
    // In DuckStation, this would:
    // - Calculate ticks per scanline based on NTSC/PAL
    // - Update CRTC tick event timing
    // - Recalculate display area bounds
    // 
    // For our emulator, we keep it simple since we don't 
    // emulate timing at that level of precision
}

bool gpu_is_display_disabled(const GPU* gpu) {
    return gpu->display_disabled;
}

bool gpu_is_interlaced(const GPU* gpu) {
    return gpu->interlaced;
}
