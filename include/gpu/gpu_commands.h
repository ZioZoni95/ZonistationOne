/**
 * gpu_commands.h
 * GPU Command Parsing Layer - GP0/GP1 command dispatch and handling
 * 
 * Based on DuckStation's gpu_commands.cpp architecture
 * Handles:
 *   - GP0 command dispatch table (256 entries)
 *   - GP1 register commands
 *   - FIFO management
 *   - DMA packet boundary detection (CRITICAL for text rendering)
 *   - Command buffering and word accumulation
 */
#ifndef GPU_COMMANDS_H
#define GPU_COMMANDS_H

#include "gpu_types.h"
#include <stdint.h>
#include <stdbool.h>

// Forward declaration - full definition in gpu_core.h
struct GPU;
typedef struct GPU GPU;

// ============================================================================
// GP0 Command Handler Type
// ============================================================================

/**
 * GP0 command handler function pointer type.
 * Each handler processes a complete multi-word command from the command buffer.
 */
typedef void (*GP0Handler)(GPU* gpu);

// ============================================================================
// Command Dispatch Functions
// ============================================================================

/**
 * Initialize GP0 command handler table.
 * Must be called once during GPU initialization.
 * Builds the 256-entry dispatch table with correct handler mappings.
 * 
 * CRITICAL: Includes fix for 0x78 (textured 16x16) vs 0x7C (non-textured 16x16)
 */
void gpu_commands_init_table(void);

/**
 * Main GP0 command dispatcher.
 * Handles:
 *   - New command detection
 *   - DMA packet boundary detection (CRITICAL)
 *   - Multi-word command accumulation
 *   - Handler invocation when complete
 * 
 * @param gpu GPU state
 * @param command 32-bit GP0 command/data word
 */
void gp0_dispatch_command(GPU* gpu, uint32_t command);

/**
 * Main GP1 command dispatcher.
 * Handles GPU control commands (display, DMA, reset, etc.)
 * 
 * @param gpu GPU state
 * @param command 32-bit GP1 command (only lower 24 bits used)
 */
void gp1_dispatch_command(GPU* gpu, uint32_t command);

// ============================================================================
// Command Buffer Management
// ============================================================================

/**
 * Clear the GP0 command buffer.
 * Resets word count to 0.
 */
void gp0_clear_command_buffer(GPU* gpu);

/**
 * Push a word onto the GP0 command buffer.
 * Checks for overflow and aborts on error.
 * 
 * @param gpu GPU state
 * @param word 32-bit command/data word
 */
void gp0_push_command_word(GPU* gpu, uint32_t word);

// ============================================================================
// FIFO Management (Hardware 16-Entry FIFO)
// ============================================================================

/**
 * Push a word to the hardware GP0 FIFO.
 * 
 * @param gpu GPU state
 * @param word 32-bit word to enqueue
 */
void gp0_fifo_push(GPU* gpu, uint32_t word);

/**
 * Pop a word from the hardware GP0 FIFO.
 * 
 * @param gpu GPU state
 * @return Next word from FIFO
 */
uint32_t gp0_fifo_pop(GPU* gpu);

/**
 * Check if FIFO is empty.
 * 
 * @param gpu GPU state
 * @return true if empty
 */
bool gp0_fifo_is_empty(const GPU* gpu);

/**
 * Check if FIFO is full.
 * 
 * @param gpu GPU state
 * @return true if full
 */
bool gp0_fifo_is_full(const GPU* gpu);

/**
 * Get number of words in FIFO.
 * 
 * @param gpu GPU state
 * @return Word count (0-16)
 */
uint32_t gp0_fifo_get_count(const GPU* gpu);

// ============================================================================
// GP0 Command Handler Declarations (Internal, exposed for testing)
// ============================================================================

// These are typically static in the .c file, but exposed here for unit testing

// --- No-op / Cache Commands ---
void gp0_nop(GPU* gpu);
void gp0_clear_cache(GPU* gpu);
void gp0_interrupt_request(GPU* gpu);

// --- Drawing Commands (will call gpu_rendering.c functions) ---
void gp0_fill_rectangle(GPU* gpu);

// Polygons
void gp0_polygon_handler(GPU* gpu);        // Dispatches to specific polygon type
void gp0_quad_mono_opaque(GPU* gpu);
void gp0_quad_texture_blend_opaque(GPU* gpu);
void gp0_quad_shaded_opaque(GPU* gpu);
void gp0_triangle_shaded_opaque(GPU* gpu);

// Lines
void gp0_line_handler(GPU* gpu);           // Dispatches to specific line type

// Rectangles
void gp0_rectangle_handler(GPU* gpu);      // Dispatches to specific rectangle type
void gp0_rect_variable_opaque(GPU* gpu);
void gp0_rect_variable_semi_trans(GPU* gpu);
void gp0_rect_tex_variable_opaque(GPU* gpu);
void gp0_rect_1x1_opaque(GPU* gpu);
void gp0_rect_8x8_opaque(GPU* gpu);
void gp0_rect_16x16_opaque(GPU* gpu);       // Non-textured 16x16 (2 words)
void gp0_rect_tex_1x1_opaque(GPU* gpu);
void gp0_rect_tex_8x8_opaque(GPU* gpu);
void gp0_rect_tex_16x16_opaque(GPU* gpu);   // Textured 16x16 (3 words) - CRITICAL for fonts

// --- VRAM Commands (will call gpu_vram.c functions) ---
void gp0_copy_rectangle(GPU* gpu);
void gp0_image_load(GPU* gpu);
void gp0_image_store(GPU* gpu);

// --- Environment Commands ---
void gp0_draw_mode(GPU* gpu);
void gp0_texture_window(GPU* gpu);
void gp0_drawing_area_top_left(GPU* gpu);
void gp0_drawing_area_bottom_right(GPU* gpu);
void gp0_drawing_offset(GPU* gpu);
void gp0_mask_bit_setting(GPU* gpu);

// --- GP1 Command Handlers ---
void gp1_reset(GPU* gpu, uint32_t value);
void gp1_reset_command_buffer(GPU* gpu, uint32_t value);
void gp1_acknowledge_irq(GPU* gpu, uint32_t value);
void gp1_display_enable(GPU* gpu, uint32_t value);
void gp1_dma_direction(GPU* gpu, uint32_t value);
void gp1_display_vram_start(GPU* gpu, uint32_t value);
void gp1_display_horizontal_range(GPU* gpu, uint32_t value);
void gp1_display_vertical_range(GPU* gpu, uint32_t value);
void gp1_display_mode(GPU* gpu, uint32_t value);

// --- Main Entry Points (called from Interconnect) ---
void gpu_gp0(GPU* gpu, uint32_t command);
void gpu_gp1(GPU* gpu, uint32_t command);

// Helper function exposed for command processing
void gpu_update_display_mapping(GPU* gpu);

#endif // GPU_COMMANDS_H
