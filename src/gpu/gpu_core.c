/**
 * gpu_core.c
 * Core GPU initialization and state management
 * Modular implementation based on DuckStation architecture
 */

#include "gpu.h"  // Main GPU header (includes gpu_core.h with legacy types)
#include "interconnect.h"
#include "vram.h"
#include "log.h"
#include <string.h>

// ============================================================================
// Forward Declarations (old handlers still in gpu.c for now)
// ============================================================================

// These will be moved to gpu_commands.c in Phase 2
extern void gpu_gp0_handle_word(Gpu* gpu, uint32_t command);
extern void gpu_gp1_handle_command(Gpu* gpu, uint32_t command);

// ============================================================================
// Initialization Functions
// ============================================================================

/**
 * Initialize GPU with full system reset (clears VRAM)
 */
void gpu_init(GPU* gpu, Interconnect* inter) {
    LOG_GPU_INFO("╔═══════════════════════════════════════════════════════════════╗");
    LOG_GPU_INFO("║ NEW MODULAR GPU SYSTEM ACTIVATED - src/gpu/gpu_core.c       ║");
    LOG_GPU_INFO("╚═══════════════════════════════════════════════════════════════╝");
    LOG_GPU_DEBUG("GPU full initialization (with VRAM) - NEW MODULAR CODE");
    
    // Clear entire GPU structure
    memset(gpu, 0, sizeof(GPU));
    
    // Initialize VRAM mutex FIRST (before any VRAM access)
    mutex_init(&gpu->vram_mutex);
    
    // Initialize VRAM
    vram_init(&gpu->vram);
    
    // Set interconnect pointer
    gpu->inter = inter;
    
    // Initialize GPUSTAT to power-on defaults
    gpu->GPUSTAT.bits = 0x14802000;  // Ready for DMA and commands
    
    // Set old field compatibility values (will be migrated to GPUSTAT in Phase 2)
    gpu->page_base_x = 0;
    gpu->page_base_y = 0;
    gpu->semi_transparency = 0;
    gpu->texture_depth = T4Bit;
    gpu->dithering = false;
    gpu->draw_to_display = false;
    gpu->force_set_mask_bit = false;
    gpu->preserve_masked_pixels = false;
    gpu->field = Top;
    gpu->texture_disable = false;
    gpu->rectangle_texture_x_flip = false;
    gpu->rectangle_texture_y_flip = false;
    
    // Horizontal/Vertical resolution
    gpu->hres_raw.hr1 = 0;
    gpu->hres_raw.hr2 = 0;
    gpu->vres = Y240Lines;
    gpu->vmode = Ntsc;
    gpu->display_depth = D15Bits;
    gpu->interlaced = true;
    gpu->display_disabled = false;  // Enable display by default to allow VBlank triggering
    gpu->interrupt = false;
    gpu->dma_setting = GPU_DMA_Off;
    
    // Texture window state
    gpu->texture_window_x_mask = 0;
    gpu->texture_window_y_mask = 0;
    gpu->texture_window_x_offset = 0;
    gpu->texture_window_y_offset = 0;
    
    // Drawing area state
    gpu->drawing_area_left = 0;
    gpu->drawing_area_top = 0;
    gpu->drawing_area_right = 0;
    gpu->drawing_area_bottom = 0;
    gpu->drawing_x_offset = 0;
    gpu->drawing_y_offset = 0;
    
    // Display configuration
    gpu->display_vram_x_start = 0;
    gpu->display_vram_y_start = 0;
    gpu->display_horiz_start = 0x200;
    gpu->display_horiz_end = 0xC00;
    gpu->display_line_start = 0x10;
    gpu->display_line_end = 0x100;
    gpu->display_width_hint = 320;
    gpu->display_height_hint = 240;
    
    // GP0 command state
    gpu->gp0_command_buffer.count = 0;
    gpu->gp0_words_remaining = 0;
    gpu->gp0_current_opcode = 0xFF;
    gpu->gp0_mode = GP0_MODE_COMMAND;
    gpu->gp0_command_method = NULL;
    
    // === DUCKSTATION ARCHITECTURE: BlitterState initialization ===
    gpu->blitter_state = GPU_BLITTER_IDLE;
    gpu->blit_remaining_words = 0;
    
    // GP0 FIFO
    gpu->gp0_fifo_head = 0;
    gpu->gp0_fifo_tail = 0;
    gpu->gp0_fifo_count = 0;
    
    // CRTC interlaced field state (DuckStation-style)
    gpu->crtc_state.interlaced_field = 0;           // Drawing field (0=odd, 1=even)
    gpu->crtc_state.interlaced_display_field = 0;   // Display field
    
    // VRAM transfer state
    gpu->vram_load_x = 0;
    gpu->vram_load_y = 0;
    gpu->vram_load_w = 0;
    gpu->vram_load_h = 0;
    gpu->vram_load_count = 0;
    
    // Initialize renderer texture window
    renderer_set_texture_window(&gpu->renderer, 0, 0, 0, 0);
    
    LOG_GPU_DEBUG("GPU initialized (state reset, VRAM cleared)");
}

/**
 * Soft reset GPU (does NOT clear VRAM)
 * Equivalent to GP1(0x00) command
 */
void gpu_soft_reset(GPU* gpu) {
    LOG_GPU_INFO("*** GPU SOFT RESET - NEW MODULAR CODE (gpu_core.c) ***");
    LOG_GPU_DEBUG("GPU soft reset (VRAM preserved)");
    
    // Reset GPUSTAT to defaults (but keep some state)
    gpu->GPUSTAT.bits = 0x14802000;
    
    // Reset old compatibility fields
    gpu->interrupt = false;
    gpu->page_base_x = 0;
    gpu->page_base_y = 0;
    gpu->semi_transparency = 0;
    gpu->texture_depth = T4Bit;
    gpu->dithering = false;
    gpu->draw_to_display = false;
    gpu->force_set_mask_bit = false;
    gpu->preserve_masked_pixels = false;
    gpu->field = Top;
    gpu->texture_disable = false;
    gpu->rectangle_texture_x_flip = false;
    gpu->rectangle_texture_y_flip = false;
    
    // Reset resolution
    gpu->hres_raw.hr1 = 0;
    gpu->hres_raw.hr2 = 0;
    gpu->vres = Y240Lines;
    gpu->vmode = Ntsc;
    gpu->display_depth = D15Bits;
    gpu->interlaced = true;
    gpu->display_disabled = true;
    
    // Reset CRTC interlaced fields
    gpu->crtc_state.interlaced_field = 0;
    gpu->crtc_state.interlaced_display_field = 0;
    
    gpu->dma_setting = GPU_DMA_Off;
    
    // Reset texture window
    gpu->texture_window_x_mask = 0;
    gpu->texture_window_y_mask = 0;
    gpu->texture_window_x_offset = 0;
    gpu->texture_window_y_offset = 0;
    renderer_set_texture_window(&gpu->renderer, 0, 0, 0, 0);
    
    // Reset drawing area
    gpu->drawing_area_left = 0;
    gpu->drawing_area_top = 0;
    gpu->drawing_area_right = 0;
    gpu->drawing_area_bottom = 0;
    gpu->drawing_x_offset = 0;
    gpu->drawing_y_offset = 0;
    
    // Reset display config
    gpu->display_vram_x_start = 0;
    gpu->display_vram_y_start = 0;
    gpu->display_horiz_start = 0x200;
    gpu->display_horiz_end = 0xC00;
    gpu->display_line_start = 0x10;
    gpu->display_line_end = 0x100;
    gpu->display_width_hint = 320;
    gpu->display_height_hint = 240;
    
    // Reset command state
    gpu->gp0_command_buffer.count = 0;
    gpu->gp0_words_remaining = 0;
    gpu->gp0_current_opcode = 0xFF;
    gpu->gp0_mode = GP0_MODE_COMMAND;
    gpu->gp0_command_method = NULL;
    
    // Reset VRAM transfer state
    gpu->vram_load_x = 0;
    gpu->vram_load_y = 0;
    gpu->vram_load_w = 0;
    gpu->vram_load_h = 0;
    gpu->vram_load_count = 0;
    
    // VRAM and interconnect pointer remain unchanged
    
    LOG_GPU_DEBUG("GPU soft reset complete (VRAM preserved)");
}

/**
 * Legacy function for backward compatibility
 * Calls gpu_init() under the hood
 */
void gpu_init_full(struct GPU* gpu, struct Interconnect* inter) {
    gpu_init((GPU*)gpu, (Interconnect*)inter);
}

// ============================================================================
// GPU Read Functions
// ============================================================================

/**
 * Read GPUSTAT register (status/control information)
 */
uint32_t gpu_read_status(GPU* gpu) {
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

/**
 * Read GPUREAD port (returns pixel data during VRAM-to-CPU transfers)
 */
uint32_t gpu_read_data(GPU* gpu) {
    if (gpu->gp0_mode == GP0_MODE_IMAGE_STORE) {
        if (gpu->gp0_words_remaining == 0) {
            LOG_GPU_WARN("GPUREAD: Read attempted but no words remaining in Image Store transfer.");
            return 0xFFFFFFFF;
        }

        // CRITICAL: Lock mutex before VRAM access to prevent race with GPU thread
        mutex_lock(&gpu->vram_mutex);
        
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
        
        // Unlock mutex before updating state
        mutex_unlock(&gpu->vram_mutex);
        
        gpu->gp0_words_remaining--;

        // Log progress
        static uint32_t gpuread_count = 0;
        gpuread_count++;
        if (gpuread_count <= 10 || gpu->gp0_words_remaining == 0) {
            LOG_GPU_DEBUG("[GPUREAD] VRAM-to-CPU: 0x%08x (Rem: %u)", word, gpu->gp0_words_remaining);
        }

        if (gpu->gp0_words_remaining == 0) {
            LOG_GPU_INFO("GP0(0xC0): VRAM-to-CPU transfer COMPLETE");
            gpu->gp0_mode = GP0_MODE_COMMAND;
        }

        return word;
    }

    // Fallback for non-transfer reads
    static uint32_t dummy_gpu_read = 0xDEADBEEF;
    dummy_gpu_read++;
    LOG_GPU_DEBUG("[GPUREAD] Read (No Transfer): 0x%08x", dummy_gpu_read);
    return dummy_gpu_read;
}

/**
 * VBlank event handler
 * Note: VBlank IRQ is generated by Timer0, not the GPU itself
 */
void gpu_trigger_vblank_irq(GPU* gpu) {
    // Per PSX specs, VBlank IRQ0 is generated by Timer0, not the GPU
    // This function can be used for VBlank-related timing/state updates
    LOG_GPU_DEBUG("[GPU] VBlank event (IRQ0 handled by Timer0)");
    (void)gpu;  // Suppress unused parameter warning
}

// ============================================================================
// DMA Functions (DuckStation-compatible)
// ============================================================================

/**
 * Write a single word via DMA to GP0
 * Used for linked list and block transfers
 */
void gpu_dma_write(GPU* gpu, uint32_t value) {
    LOG_GPU_TRACE("[GPU DMA] Write: 0x%08x", value);
    gpu_gp0(gpu, value);
}

/**
 * Called at the end of a DMA write transfer
 * Currently a no-op, but can be used for transfer completion handling
 */
void gpu_end_dma_write(GPU* gpu) {
    LOG_GPU_TRACE("[GPU DMA] End write transfer");
    (void)gpu;  // Suppress unused parameter warning
}

/**
 * Read words via DMA from GPU
 * Used for VRAM-to-CPU transfers (GP0(0xC0))
 */
void gpu_dma_read(GPU* gpu, uint32_t* words, uint32_t word_count) {
    LOG_GPU_TRACE("[GPU DMA] Read %u words", word_count);
    
    for (uint32_t i = 0; i < word_count; i++) {
        words[i] = gpu_read_data(gpu);
    }
}

/**
 * Check if GPU can accept DMA writes (GP0 FIFO not full)
 */
bool gpu_dma_can_write(const GPU* gpu) {
    // GPUSTAT bit 26: GP0 ready for commands/data
    return (gpu->GPUSTAT.bits & (1u << 26)) != 0;
}

/**
 * Check if GPU can provide DMA reads (has data ready)
 */
bool gpu_dma_can_read(const GPU* gpu) {
    // For VRAM reads, check if we're in GP0 read mode with data remaining
    if (gpu->gp0_mode == GP0_MODE_IMAGE_LOAD && gpu->gp0_words_remaining > 0) {
        return true;
    }
    return false;
}
