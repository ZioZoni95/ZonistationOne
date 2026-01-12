/**
 * gpu_vram.h
 * GPU VRAM Operations Interface
 * 
 * Based on DuckStation's VRAM handling architecture
 * 
 * This module handles:
 *   - VRAM to VRAM copy (GP0 0x80)
 *   - CPU/DMA to VRAM transfer (GP0 0xA0) - Image Load
 *   - VRAM to CPU/DMA transfer (GP0 0xC0) - Image Store
 *   - Fill VRAM rectangle (GP0 0x02)
 *   - Mask bit handling
 */
#ifndef GPU_VRAM_H
#define GPU_VRAM_H

#include "gpu_types.h"
#include <stdint.h>
#include <stdbool.h>

// Forward declaration
typedef struct GPU GPU;

// ============================================================================
// VRAM Transfer Commands
// ============================================================================

/**
 * GP0(0x02): Fill VRAM Rectangle
 * Fills a rectangular region in VRAM with a solid color
 * 
 * Command format:
 *   Word 0: Color (RGB)
 *   Word 1: Top-left position (X, Y)
 *   Word 2: Width, Height
 */
void gp0_fill_vram_rectangle(GPU* gpu);

/**
 * GP0(0x80): Copy Rectangle (VRAM to VRAM)
 * Copies a rectangular region from one VRAM location to another
 * Handles overlapping regions correctly
 * 
 * Command format:
 *   Word 0: Command
 *   Word 1: Source position (X, Y)
 *   Word 2: Destination position (X, Y)
 *   Word 3: Width, Height
 */
void gp0_vram_to_vram_copy(GPU* gpu);

/**
 * GP0(0xA0): CPU/DMA to VRAM Transfer - Setup Phase
 * Initiates a data transfer from CPU/DMA to VRAM
 * 
 * Command format:
 *   Word 0: Command 0xA0
 *   Word 1: Destination position (X, Y)
 *   Word 2: Width, Height
 *   Data follows: N halfwords of pixel data
 * 
 * This sets up the transfer state and switches to GP0_MODE_IMAGE_LOAD
 * Subsequent GP0 writes will be treated as pixel data
 */
void gp0_cpu_to_vram_setup(GPU* gpu);

/**
 * Process one data word during CPU to VRAM transfer
 * Called repeatedly while in GP0_MODE_IMAGE_LOAD
 * 
 * @param gpu   GPU state
 * @param data  32-bit word containing 2 pixels (16-bit each)
 */
void gp0_cpu_to_vram_data(GPU* gpu, uint32_t data);

/**
 * GP0(0xC0): VRAM to CPU/DMA Transfer - Setup Phase
 * Initiates a data transfer from VRAM to CPU/DMA
 * 
 * Command format:
 *   Word 0: Command 0xC0
 *   Word 1: Source position (X, Y)
 *   Word 2: Width, Height
 * 
 * This sets up the transfer state and switches to GP0_MODE_IMAGE_STORE
 * Subsequent GPUREAD operations will return pixel data
 */
void gp0_vram_to_cpu_setup(GPU* gpu);

/**
 * Read one data word during VRAM to CPU transfer
 * Called when GPUREAD is accessed while in GP0_MODE_IMAGE_STORE
 * 
 * @param gpu GPU state
 * @return 32-bit word containing 2 pixels (16-bit each)
 */
uint32_t gp0_vram_to_cpu_read(GPU* gpu);

// ============================================================================
// Helper Functions
// ============================================================================

/**
 * Write a pixel to VRAM with mask bit handling
 * Applies force_set_mask_bit and preserve_masked_pixels settings
 * 
 * @param gpu   GPU state
 * @param x     X coordinate (0-1023)
 * @param y     Y coordinate (0-511)
 * @param color 16-bit color value
 */
void vram_write_masked(GPU* gpu, uint16_t x, uint16_t y, uint16_t color);

/**
 * Fill a rectangular region in VRAM
 * Used internally by fill and clear operations
 * 
 * @param gpu    GPU state
 * @param x      Top-left X
 * @param y      Top-left Y
 * @param width  Width in pixels
 * @param height Height in pixels
 * @param color  16-bit fill color
 */
void vram_fill_rect(GPU* gpu, uint16_t x, uint16_t y, 
                   uint16_t width, uint16_t height, uint16_t color);

/**
 * Copy a rectangular region within VRAM
 * Handles overlapping regions by choosing copy direction
 * 
 * @param gpu    GPU state
 * @param src_x  Source X
 * @param src_y  Source Y
 * @param dst_x  Destination X
 * @param dst_y  Destination Y
 * @param width  Width in pixels
 * @param height Height in pixels
 */
void vram_copy_rect(GPU* gpu, uint16_t src_x, uint16_t src_y,
                   uint16_t dst_x, uint16_t dst_y,
                   uint16_t width, uint16_t height);

#endif // GPU_VRAM_H
