/**
 * gpu_vram.c
 * GPU VRAM Operations Implementation
 * 
 * Based on DuckStation's VRAM handling with adaptations for our architecture
 */

#include "gpu/gpu_vram.h"
#include "gpu/gpu_core.h"
#include "renderer.h"
#include "vram.h"
#include "log.h"
#include <string.h>

// ============================================================================
// Helper: Write Pixel with Mask Bit Handling
// ============================================================================

void vram_write_masked(GPU* gpu, uint16_t x, uint16_t y, uint16_t color) {
    // Wrap coordinates to VRAM bounds
    x &= 0x3FF; // 1024 pixels wide
    y &= 0x1FF; // 512 pixels high
    
    uint32_t offset = (uint32_t)y * VRAM_WIDTH * VRAM_BPP + (uint32_t)x * VRAM_BPP;
    
    // NOTE: Caller MUST hold vram_mutex before calling this function
    
    // Check if we should preserve masked pixels
    if (gpu->preserve_masked_pixels) {
        uint16_t existing = vram_load16(&gpu->vram, offset);
        if (existing & 0x8000) { // Mask bit set
            return; // Don't overwrite
        }
    }
    
    // Apply force set mask bit
    if (gpu->force_set_mask_bit) {
        color |= 0x8000;
    }
    
    vram_store16(&gpu->vram, offset, color);
}

// ============================================================================
// Fill VRAM Rectangle
// ============================================================================

void vram_fill_rect(GPU* gpu, uint16_t x, uint16_t y, 
                   uint16_t width, uint16_t height, uint16_t color) {
    // Wrap coordinates
    x &= 0x3FF;
    y &= 0x1FF;
    
    // Clamp dimensions
    if (width == 0 || width > VRAM_WIDTH) width = VRAM_WIDTH;
    if (height == 0 || height > VRAM_HEIGHT) height = VRAM_HEIGHT;
    
    LOG_GPU_DEBUG("VRAM Fill: (%u,%u) %ux%u Color=0x%04X", x, y, width, height, color);
    
    // Lock VRAM once for entire fill operation
    mutex_lock(&gpu->vram_mutex);
    
    for (uint16_t row = 0; row < height; row++) {
        for (uint16_t col = 0; col < width; col++) {
            uint16_t px = (x + col) & 0x3FF;
            uint16_t py = (y + row) & 0x1FF;
            vram_write_masked(gpu, px, py, color);
        }
    }
    
    mutex_unlock(&gpu->vram_mutex);
}

void gp0_fill_vram_rectangle(GPU* gpu) {
    if (gpu->gp0_command_buffer.count < 3) {
        LOG_GPU_ERROR("GP0(0x02) Fill Rectangle: Expected 3 words, got %u", 
                      gpu->gp0_command_buffer.count);
        return;
    }
    
    uint32_t color_word = gpu->gp0_command_buffer.buffer[0];
    uint32_t top_left = gpu->gp0_command_buffer.buffer[1];
    uint32_t dimensions = gpu->gp0_command_buffer.buffer[2];
    
    // Extract RGB color and convert to 16-bit
    uint8_t r = (color_word & 0xFF);
    uint8_t g = (color_word >> 8) & 0xFF;
    uint8_t b = (color_word >> 16) & 0xFF;
    uint16_t color = ((r >> 3) << 0) | ((g >> 3) << 5) | ((b >> 3) << 10);
    
    uint16_t x = (uint16_t)(top_left & 0x3FF);
    uint16_t y = (uint16_t)((top_left >> 16) & 0x1FF);
    uint16_t w = (uint16_t)(dimensions & 0x3FF);
    uint16_t h = (uint16_t)((dimensions >> 16) & 0x1FF);
    
    // Handle 0 dimensions (wraps to maximum)
    if (w == 0) w = 1024;
    if (h == 0) h = 512;
    
    LOG_GPU_INFO("GP0(0x02): Fill VRAM (%u,%u) %ux%u Color=0x%04X (RGB %u,%u,%u)",
                 x, y, w, h, color, r, g, b);
    
    vram_fill_rect(gpu, x, y, w, h, color);
    
    // Update OpenGL texture (lock before reading VRAM buffer)
    mutex_lock(&gpu->vram_mutex);
    renderer_upload_vram(&gpu->renderer, (const uint16_t*)gpu->vram.data);
    mutex_unlock(&gpu->vram_mutex);
}

// ============================================================================
// VRAM to VRAM Copy
// ============================================================================

void vram_copy_rect(GPU* gpu, uint16_t src_x, uint16_t src_y,
                   uint16_t dst_x, uint16_t dst_y,
                   uint16_t width, uint16_t height) {
    // Wrap coordinates
    src_x &= 0x3FF;
    src_y &= 0x1FF;
    dst_x &= 0x3FF;
    dst_y &= 0x1FF;
    
    // Determine copy direction to handle overlaps correctly
    // DuckStation approach: copy backwards if destination is after source
    int16_t step_x = 1, step_y = 1;
    int16_t start_x = 0, start_y = 0;
    int16_t end_x = width, end_y = height;
    
    if (dst_y > src_y) {
        step_y = -1;
        start_y = height - 1;
        end_y = -1;
    }
    
    if (dst_x > src_x) {
        step_x = -1;
        start_x = width - 1;
        end_x = -1;
    }
    
    LOG_GPU_DEBUG("VRAM Copy: (%u,%u)->(%u,%u) %ux%u Step=(%d,%d)",
                  src_x, src_y, dst_x, dst_y, width, height, step_x, step_y);
    
    // Lock VRAM once for entire copy operation
    mutex_lock(&gpu->vram_mutex);
    
    for (int16_t y = start_y; y != end_y; y += step_y) {
        for (int16_t x = start_x; x != end_x; x += step_x) {
            uint16_t sx = (src_x + x) & 0x3FF;
            uint16_t sy = (src_y + y) & 0x1FF;
            uint16_t dx = (dst_x + x) & 0x3FF;
            uint16_t dy = (dst_y + y) & 0x1FF;
            
            uint32_t src_offset = (uint32_t)sy * VRAM_WIDTH * VRAM_BPP + (uint32_t)sx * VRAM_BPP;
            uint16_t pixel = vram_load16(&gpu->vram, src_offset);
            
            vram_write_masked(gpu, dx, dy, pixel);
        }
    }
    
    mutex_unlock(&gpu->vram_mutex);
}

void gp0_vram_to_vram_copy(GPU* gpu) {
    if (gpu->gp0_command_buffer.count < 4) {
        LOG_GPU_ERROR("GP0(0x80) VRAM Copy: Expected 4 words, got %u",
                      gpu->gp0_command_buffer.count);
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
    
    // Handle 0 size as max size
    if (w == 0) w = 1024;
    if (h == 0) h = 512;
    
    LOG_GPU_INFO("GP0(0x80): VRAM Copy (%u,%u)->(%u,%u) %ux%u",
                 src_x, src_y, dst_x, dst_y, w, h);
    
    vram_copy_rect(gpu, src_x, src_y, dst_x, dst_y, w, h);
    
    // Update OpenGL texture (lock before reading VRAM buffer)
    mutex_lock(&gpu->vram_mutex);
    renderer_upload_vram(&gpu->renderer, (const uint16_t*)gpu->vram.data);
    mutex_unlock(&gpu->vram_mutex);
}

// ============================================================================
// CPU to VRAM Transfer (Image Load)
// ============================================================================

void gp0_cpu_to_vram_setup(GPU* gpu) {
    if (gpu->gp0_command_buffer.count < 3) {
        LOG_GPU_ERROR("GP0(0xA0) Image Load: Expected 3 words, got %u",
                      gpu->gp0_command_buffer.count);
        return;
    }
    
    uint32_t dest_coord = gpu->gp0_command_buffer.buffer[1];
    uint32_t dimensions = gpu->gp0_command_buffer.buffer[2];
    
    uint16_t x = (uint16_t)(dest_coord & 0x3FF);
    uint16_t y = (uint16_t)((dest_coord >> 16) & 0x1FF);
    uint16_t w = (uint16_t)(dimensions & 0x3FF);
    uint16_t h = (uint16_t)((dimensions >> 16) & 0x1FF);
    
    // Handle 0 dimensions (DuckStation: 0 means maximum)
    if (w == 0) w = 1024;
    if (h == 0) h = 512;
    
    // Clamp to VRAM size
    if (w > VRAM_WIDTH) w = VRAM_WIDTH;
    if (h > VRAM_HEIGHT) h = VRAM_HEIGHT;
    
    uint32_t total_pixels = (uint32_t)w * h;
    uint32_t total_pixels_rounded = (total_pixels + 1) & ~1; // Round up to even
    uint32_t words_to_load = total_pixels_rounded / 2; // 2 pixels per word
    
    // Sanity check
    if (words_to_load == 0 || ((uint64_t)words_to_load * 4) > VRAM_SIZE) {
        LOG_GPU_WARN("GP0(0xA0): Invalid transfer size %u words", words_to_load);
        gpu->gp0_words_remaining = 0;
        gpu->gp0_mode = GP0_MODE_COMMAND;
        return;
    }
    
    // Store transfer state
    gpu->vram_transfer.x = x;
    gpu->vram_transfer.y = y;
    gpu->vram_transfer.width = w;
    gpu->vram_transfer.height = h;
    gpu->vram_transfer.count = 0;
    gpu->vram_transfer.pixel_count = total_pixels;
    
    // === DUCKSTATION ARCHITECTURE: Switch to WritingVRAM blitter state ===
    // This prevents commands from being dispatched during pixel data transfer
    gpu->blitter_state = GPU_BLITTER_WRITING_VRAM;
    gpu->blit_remaining_words = words_to_load;
    
    // Legacy mode tracking (for compatibility)
    gpu->gp0_mode = GP0_MODE_IMAGE_LOAD;
    gpu->gp0_words_remaining = words_to_load;
    
    LOG_GPU_INFO("GP0(0xA0): CPU->VRAM Transfer Start Dest=(%d,%d) Size=%dx%d (%d words) [BlitterState=WRITING_VRAM]", 
                 x, y, w, h, words_to_load);
}

void gp0_cpu_to_vram_data(GPU* gpu, uint32_t data) {
    // Extract two 16-bit pixels from the 32-bit word
    uint16_t pixel0 = (uint16_t)(data & 0xFFFF);
    uint16_t pixel1 = (uint16_t)(data >> 16);
    
    // Calculate current position in the transfer
    uint32_t index = gpu->vram_transfer.count;
    uint16_t w = gpu->vram_transfer.width;
    uint16_t base_x = gpu->vram_transfer.x;
    uint16_t base_y = gpu->vram_transfer.y;
    
    // Write first pixel
    if (index < gpu->vram_transfer.pixel_count) {
        uint16_t x = base_x + (index % w);
        uint16_t y = base_y + (index / w);
        vram_write_masked(gpu, x, y, pixel0);
        gpu->vram_transfer.count++;
    }
    
    // Write second pixel
    index = gpu->vram_transfer.count;
    if (index < gpu->vram_transfer.pixel_count) {
        uint16_t x = base_x + (index % w);
        uint16_t y = base_y + (index / w);
        vram_write_masked(gpu, x, y, pixel1);
        gpu->vram_transfer.count++;
    }
    
    // Check if transfer complete
    gpu->gp0_words_remaining--;
    if (gpu->gp0_words_remaining == 0) {
        LOG_GPU_INFO("GP0(0xA0): CPU->VRAM Transfer Complete (%u pixels written)",
                     gpu->vram_transfer.count);
        
        gpu->gp0_mode = GP0_MODE_COMMAND;
        
        // Update OpenGL texture (lock before reading VRAM buffer)
        mutex_lock(&gpu->vram_mutex);
        renderer_upload_vram(&gpu->renderer, (const uint16_t*)gpu->vram.data);
        mutex_unlock(&gpu->vram_mutex);
    }
}

// ============================================================================
// VRAM to CPU Transfer (Image Store)
// ============================================================================

void gp0_vram_to_cpu_setup(GPU* gpu) {
    if (gpu->gp0_command_buffer.count < 3) {
        LOG_GPU_ERROR("GP0(0xC0) Image Store: Expected 3 words, got %u",
                      gpu->gp0_command_buffer.count);
        return;
    }
    
    uint32_t src_coord = gpu->gp0_command_buffer.buffer[1];
    uint32_t dimensions = gpu->gp0_command_buffer.buffer[2];
    
    uint16_t x = (uint16_t)(src_coord & 0x3FF);
    uint16_t y = (uint16_t)((src_coord >> 16) & 0x1FF);
    uint16_t w = (uint16_t)(dimensions & 0xFFFF);
    uint16_t h = (uint16_t)(dimensions >> 16);
    
    // Clamp to VRAM bounds
    if (x >= VRAM_WIDTH) x = VRAM_WIDTH - 1;
    if (y >= VRAM_HEIGHT) y = VRAM_HEIGHT - 1;
    if (w == 0 || w > VRAM_WIDTH) w = VRAM_WIDTH;
    if (h == 0 || h > VRAM_HEIGHT) h = VRAM_HEIGHT;
    
    uint32_t total_pixels = (uint32_t)w * h;
    uint32_t words_to_transfer = (total_pixels + 1) / 2;
    
    LOG_GPU_INFO("GP0(0xC0): VRAM->CPU Transfer Start Src=(%u,%u) Size=%ux%u (%u words)",
                 x, y, w, h, words_to_transfer);
    
    // Store transfer state in new vram_transfer structure
    gpu->vram_transfer.x = x;
    gpu->vram_transfer.y = y;
    gpu->vram_transfer.width = w;
    gpu->vram_transfer.height = h;
    gpu->vram_transfer.count = 0;
    gpu->vram_transfer.pixel_count = total_pixels;
    
    // ALSO update old vram_load_* fields for compatibility with gpu_read_data()
    gpu->vram_load_x = x;
    gpu->vram_load_y = y;
    gpu->vram_load_w = w;
    gpu->vram_load_h = h;
    gpu->vram_load_count = 0;
    
    // === DUCKSTATION ARCHITECTURE: Switch to ReadingVRAM blitter state ===
    gpu->blitter_state = GPU_BLITTER_READING_VRAM;
    gpu->blit_remaining_words = words_to_transfer;
    
    // Legacy mode tracking (for compatibility)
    gpu->gp0_mode = GP0_MODE_IMAGE_STORE;
    gpu->gp0_words_remaining = words_to_transfer;
    
    LOG_GPU_INFO("GP0(0xC0): Switched to IMAGE_STORE mode [BlitterState=READING_VRAM], %u words available", words_to_transfer);
}

uint32_t gp0_vram_to_cpu_read(GPU* gpu) {
    uint16_t pixel0 = 0, pixel1 = 0;
    
    // Calculate current position
    uint32_t index = gpu->vram_transfer.count;
    uint16_t w = gpu->vram_transfer.width;
    uint16_t base_x = gpu->vram_transfer.x;
    uint16_t base_y = gpu->vram_transfer.y;
    
    // Lock VRAM mutex before reading pixels
    mutex_lock(&gpu->vram_mutex);
    
    // Read first pixel
    if (index < gpu->vram_transfer.pixel_count) {
        uint16_t x = (base_x + (index % w)) & 0x3FF;
        uint16_t y = (base_y + (index / w)) & 0x1FF;
        uint32_t offset = (uint32_t)y * VRAM_WIDTH * VRAM_BPP + (uint32_t)x * VRAM_BPP;
        pixel0 = vram_load16(&gpu->vram, offset);
        gpu->vram_transfer.count++;
    }
    
    // Read second pixel
    index = gpu->vram_transfer.count;
    if (index < gpu->vram_transfer.pixel_count) {
        uint16_t x = (base_x + (index % w)) & 0x3FF;
        uint16_t y = (base_y + (index / w)) & 0x1FF;
        uint32_t offset = (uint32_t)y * VRAM_WIDTH * VRAM_BPP + (uint32_t)x * VRAM_BPP;
        pixel1 = vram_load16(&gpu->vram, offset);
        gpu->vram_transfer.count++;
    }
    
    mutex_unlock(&gpu->vram_mutex);
    
    // Check if transfer complete
    gpu->gp0_words_remaining--;
    if (gpu->gp0_words_remaining == 0) {
        LOG_GPU_INFO("GP0(0xC0): VRAM->CPU Transfer Complete (%u pixels read)",
                     gpu->vram_transfer.count);
        gpu->gp0_mode = GP0_MODE_COMMAND;
    }
    
    return ((uint32_t)pixel1 << 16) | pixel0;
}
