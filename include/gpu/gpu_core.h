/**
 * gpu_core.h
 * Main GPU state structure and public API
 * Modular architecture based on DuckStation
 */
#ifndef GPU_CORE_H
#define GPU_CORE_H

#include "gpu_types.h"
#include "../renderer.h"
#include "../vram.h"
#include "../threading.h"
#include <stdint.h>
#include <stdbool.h>

// Forward declarations
struct Interconnect;
typedef struct Interconnect Interconnect;

// Forward declaration for command handler function pointer
typedef struct GPU GPU;
typedef void (*GP0CommandHandler)(GPU*);

// ============================================================================
// Main GPU State Structure
// ============================================================================

typedef struct GPU {
    // --- Core Status Register (DuckStation-style bitfield) ---
    GPUSTAT GPUSTAT;  // All GPUSTAT bits in one 32-bit union
    
    // --- Drawing Configuration ---
    GPUDrawingArea drawing_area;      // Clipped drawing rectangle
    GPUDrawingOffset drawing_offset;  // Signed XY offset for primitives
    GPUTextureWindow texture_window;  // Texture window mask/offset
    bool drawing_area_changed;        // Flag: drawing area needs update
    
    // --- CRTC (Display Controller) State ---
    CRTCState crtc_state;
    
    // --- GP0 Command Processing (DuckStation architecture) ---
    GPUBlitterState blitter_state;        // Current blitter operation state
    uint32_t blit_remaining_words;        // Words remaining for VRAM transfer
    GP0CommandBuffer gp0_command_buffer;  // Current command parameters
    uint32_t gp0_words_remaining;         // Words left for current command
    uint8_t gp0_current_opcode;           // Current command opcode
    GP0Mode gp0_mode;                     // Command/ImageLoad/ImageStore mode (legacy)
    GP0CommandHandler gp0_command_method; // Active command handler
    
    // --- Hardware GP0 FIFO (16 words / 64 bytes) ---
    uint32_t gp0_fifo[16];   // Hardware command FIFO
    uint8_t gp0_fifo_head;   // Read index
    uint8_t gp0_fifo_tail;   // Write index
    uint8_t gp0_fifo_count;  // Number of words in FIFO
    
    // --- VRAM Transfer State ---
    VRAMTransferState vram_transfer;  // Active VRAM load/store state
    
    // --- Additional Drawing State (from old code) ---
    bool rectangle_texture_x_flip;  // Texture flip X (GP0(E1)[12])
    bool rectangle_texture_y_flip;  // Texture flip Y (GP0(E1)[13])
    
    // --- Display Configuration (GP1(05), GP1(06), GP1(07)) ---
    uint16_t display_vram_x_start;  // X coordinate in VRAM for display area
    uint16_t display_vram_y_start;  // Y coordinate in VRAM for display area
    uint16_t display_horiz_start;   // Horizontal start timing relative to HSYNC
    uint16_t display_horiz_end;     // Horizontal end timing relative to HSYNC
    uint16_t display_line_start;    // Vertical start timing relative to VSYNC
    uint16_t display_line_end;      // Vertical end timing relative to VSYNC
    uint16_t display_width_hint;    // Derived width from resolution
    uint16_t display_height_hint;   // Derived height from resolution
    bool display_disabled;          // GPUSTAT[23] - Display enable/disable
    
    // --- Old GPU compatibility fields (to be refactored) ---
    // GPUSTAT fields accessed directly in old code
    uint8_t page_base_x;            // GPUSTAT[3:0] - Texture page X
    uint8_t page_base_y;            // GPUSTAT[4] - Texture page Y
    uint8_t semi_transparency;      // GPUSTAT[6:5] - Semi-transparency mode
    GPUTextureMode texture_depth;   // GPUSTAT[8:7] - Texture color depth
    bool dithering;                 // GPUSTAT[9] - Dithering enable
    bool draw_to_display;           // GPUSTAT[10] - Draw to display area
    bool force_set_mask_bit;        // GPUSTAT[11] - Force mask bit on write
    bool preserve_masked_pixels;    // GPUSTAT[12] - Check mask before draw
    GPUField field;                 // GPUSTAT[13] - Interlace field
    bool texture_disable;           // GPUSTAT[15] - Disable texturing
    HorizontalResRaw hres_raw;      // GPUSTAT[18:16] - Horizontal resolution
    GPUVerticalResolution vres;     // GPUSTAT[19] - Vertical resolution
    GPUVideoMode vmode;             // GPUSTAT[20] - NTSC/PAL
    GPUDisplayDepth display_depth;  // GPUSTAT[21] - Display color depth
    bool interlaced;                // GPUSTAT[22] - Interlace enable
    bool interrupt;                 // GPUSTAT[24] - IRQ flag
    GPUDMADirection dma_setting;    // GPUSTAT[30:29] - DMA direction
    
    // Texture window fields (GP0(E2))
    uint8_t texture_window_x_mask;
    uint8_t texture_window_y_mask;
    uint8_t texture_window_x_offset;
    uint8_t texture_window_y_offset;
    
    // Drawing area fields (GP0(E3), GP0(E4))
    uint16_t drawing_area_left;
    uint16_t drawing_area_top;
    uint16_t drawing_area_right;
    uint16_t drawing_area_bottom;
    
    // Drawing offset (GP0(E5))
    int16_t drawing_x_offset;
    int16_t drawing_y_offset;
    
    // VRAM load state (for GP0(A0))
    uint16_t vram_load_x;
    uint16_t vram_load_y;
    uint16_t vram_load_w;
    uint16_t vram_load_h;
    uint32_t vram_load_count;
    
    // --- VRAM (1MB Video Memory) ---
    Vram vram;  // 1024x512 16-bit framebuffer
    Mutex vram_mutex;  // Protects VRAM from simultaneous CPU/GPU thread access
    
    // --- Renderer (OpenGL Backend) ---
    Renderer renderer;  // OpenGL drawing operations
    
    // --- System Interconnect ---
    Interconnect* inter;  // Pointer to interconnect for IRQs
    
    // --- GPU Threading ---
    struct GpuThreadState* thread_state;  // Pointer to GPU thread state (NULL if threading disabled)
    
} GPU;

// ============================================================================
// Public API Functions
// ============================================================================

/**
 * Initialize GPU with full system reset (clears VRAM)
 * @param gpu Pointer to GPU instance
 * @param inter Pointer to interconnect for IRQ requests
 */
void gpu_init(GPU* gpu, Interconnect* inter);

/**
 * Soft reset GPU (does NOT clear VRAM)
 * Equivalent to GP1(0x00) command
 * @param gpu Pointer to GPU instance
 */
void gpu_soft_reset(GPU* gpu);

/**
 * Legacy function for backward compatibility with old code
 * Defined in gpu_core.c, declared here for old code that expects it
 */
struct GPU; // Forward declaration
void gpu_init_full(struct GPU* gpu, struct Interconnect* inter);

/**
 * Write command/data to GP0 port (0x1F801810)
 * Handles commands and image data
 * @param gpu Pointer to GPU instance
 * @param value 32-bit command or data word
 */
void gpu_gp0(GPU* gpu, uint32_t value);

/**
 * Write command to GP1 port (0x1F801814)
 * GPU control commands (reset, display config, DMA mode, etc.)
 * @param gpu Pointer to GPU instance
 * @param command 32-bit command word
 */
void gpu_gp1(GPU* gpu, uint32_t command);

/**
 * Read GPUSTAT register (0x1F801814)
 * Returns GPU status and configuration bits
 * @param gpu Pointer to GPU instance
 * @return 32-bit GPUSTAT value
 */
uint32_t gpu_read_status(GPU* gpu);

/**
 * Read GPUREAD port (0x1F801810)
 * Returns VRAM data or command response
 * @param gpu Pointer to GPU instance
 * @return 32-bit data word
 */
uint32_t gpu_read_data(GPU* gpu);

/**
 * DMA write to GP0 (called by DMA controller)
 * @param gpu Pointer to GPU instance
 * @param value 32-bit data word
 */
void gpu_dma_write(GPU* gpu, uint32_t value);

/**
 * End DMA write burst (process queued commands)
 * @param gpu Pointer to GPU instance
 */
void gpu_end_dma_write(GPU* gpu);

/**
 * DMA read from VRAM (called by DMA controller)
 * @param gpu Pointer to GPU instance
 * @param words Output buffer for VRAM data
 * @param word_count Number of words to read
 */
void gpu_dma_read(GPU* gpu, uint32_t* words, uint32_t word_count);

/**
 * Check if DMA write is ready
 * @param gpu Pointer to GPU instance
 * @return true if DMA can write to GP0
 */
bool gpu_dma_can_write(const GPU* gpu);

/**
 * Check if DMA read is ready
 * @param gpu Pointer to GPU instance  
 * @return true if DMA can read from GPUREAD
 */
bool gpu_dma_can_read(const GPU* gpu);

/**
 * Update CRTC timing (advance scanline, check VBlank)
 * Called from main loop timing system
 * @param gpu Pointer to GPU instance
 * @param ticks Number of GPU ticks elapsed
 */
void gpu_update_crtc(GPU* gpu, uint32_t ticks);

/**
 * Force VBlank interrupt (for timing synchronization)
 * @param gpu Pointer to GPU instance
 */
void gpu_trigger_vblank(GPU* gpu);

#endif // GPU_CORE_H
