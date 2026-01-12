/**
 * gpu_display.h
 * GPU Display/CRTC Control Interface
 * 
 * Based on DuckStation's CRTC and display management
 * 
 * This module handles:
 *   - GP1 display configuration commands
 *   - Display mode and resolution settings
 *   - CRTC (Cathode Ray Tube Controller) state
 *   - Video timing (NTSC/PAL)
 *   - Display area configuration
 */
#ifndef GPU_DISPLAY_H
#define GPU_DISPLAY_H

#include "gpu_types.h"
#include <stdint.h>
#include <stdbool.h>

// Forward declaration
typedef struct GPU GPU;

// ============================================================================
// GP1 Display Control Commands
// ============================================================================

/**
 * GP1(0x00): Reset GPU
 * Resets GPU to initial state
 * - Clears command buffer
 * - Resets display configuration
 * - Resets drawing settings
 * 
 * @param gpu   GPU state
 * @param value Command parameter (typically 0)
 */
void gp1_reset(GPU* gpu, uint32_t value);

/**
 * GP1(0x01): Reset Command Buffer
 * Clears any pending commands and resets FIFO
 * 
 * @param gpu   GPU state
 * @param value Command parameter (unused)
 */
void gp1_reset_command_buffer(GPU* gpu, uint32_t value);

/**
 * GP1(0x02): Acknowledge GPU Interrupt
 * Clears the GPU interrupt flag (GPUSTAT bit 24)
 * 
 * @param gpu   GPU state
 * @param value Command parameter (unused)
 */
void gp1_acknowledge_irq(GPU* gpu, uint32_t value);

/**
 * GP1(0x03): Display Enable/Disable
 * Controls whether the display output is active
 * 
 * @param gpu   GPU state
 * @param value Bit 0: 0=Enable, 1=Disable
 */
void gp1_display_enable(GPU* gpu, uint32_t value);

/**
 * GP1(0x04): DMA Direction
 * Sets the DMA data transfer direction
 * 
 * @param gpu   GPU state
 * @param value Bits 0-1: 0=Off, 1=FIFO, 2=CPU->GP0, 3=VRAM->CPU
 */
void gp1_dma_direction(GPU* gpu, uint32_t value);

/**
 * GP1(0x05): Display Area Start in VRAM
 * Sets the top-left corner of the display area within VRAM
 * 
 * @param gpu   GPU state
 * @param value Bits 0-9: X coordinate, Bits 10-18: Y coordinate
 */
void gp1_display_vram_start(GPU* gpu, uint32_t value);

/**
 * GP1(0x06): Horizontal Display Range
 * Sets the horizontal timing (start/end of active display)
 * 
 * @param gpu   GPU state
 * @param value Bits 0-11: H start, Bits 12-23: H end (in dotclock units)
 */
void gp1_display_horizontal_range(GPU* gpu, uint32_t value);

/**
 * GP1(0x07): Vertical Display Range
 * Sets the vertical timing (start/end of active display)
 * 
 * @param gpu   GPU state
 * @param value Bits 0-9: V start, Bits 10-19: V end (in scanlines)
 */
void gp1_display_vertical_range(GPU* gpu, uint32_t value);

/**
 * GP1(0x08): Display Mode
 * Configures display resolution, color depth, and video mode
 * 
 * @param gpu   GPU state
 * @param value Bits 0-1: Horizontal res 1
 *              Bit 2: Vertical res (0=240, 1=480)
 *              Bit 3: Video mode (0=NTSC, 1=PAL)
 *              Bit 4: Display depth (0=15bit, 1=24bit)
 *              Bit 5: Interlaced (0=off, 1=on)
 *              Bit 6: Horizontal res 2
 *              Bit 7: Reverse flag (unsupported)
 */
void gp1_display_mode(GPU* gpu, uint32_t value);

// ============================================================================
// Helper Functions
// ============================================================================

/**
 * Calculate effective display dimensions
 * Based on horizontal resolution bits and interlace settings
 * 
 * @param gpu GPU state
 * @return Width and height as packed uint32_t (width in low 16 bits, height in high 16)
 */
uint32_t gpu_get_display_dimensions(const GPU* gpu);

/**
 * Update CRTC configuration
 * Recalculates display timing based on video mode and settings
 * Called when display mode changes
 * 
 * @param gpu GPU state
 */
void gpu_update_crtc_config(GPU* gpu);

/**
 * Check if display is currently disabled
 * 
 * @param gpu GPU state
 * @return true if display output is disabled
 */
bool gpu_is_display_disabled(const GPU* gpu);

/**
 * Check if interlaced mode is enabled
 * 
 * @param gpu GPU state
 * @return true if interlaced display is active
 */
bool gpu_is_interlaced(const GPU* gpu);

#endif // GPU_DISPLAY_H
