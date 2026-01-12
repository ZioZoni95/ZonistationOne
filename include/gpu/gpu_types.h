/**
 * gpu_types.h
 * GPU type definitions, enums, and constants
 * Based on DuckStation architecture with PlayStation hardware specifications
 */
#ifndef GPU_TYPES_H
#define GPU_TYPES_H

#include <stdint.h>
#include <stdbool.h>

// ============================================================================
// VRAM Constants (PlayStation Hardware Specifications)
// ============================================================================

enum {
    VRAM_WIDTH = 1024,
    VRAM_HEIGHT = 512,
    VRAM_SIZE = VRAM_WIDTH * VRAM_HEIGHT * sizeof(uint16_t), // 1MB
    VRAM_WIDTH_MASK = VRAM_WIDTH - 1,
    VRAM_HEIGHT_MASK = VRAM_HEIGHT - 1,
    
    // Texture page dimensions
    TEXTURE_PAGE_WIDTH = 256,
    TEXTURE_PAGE_HEIGHT = 256,
    
    // CLUT (Color Lookup Table) size
    GPU_CLUT_SIZE = 256,
    
    // Display limits (can exceed VRAM height in interlaced PAL)
    GPU_MAX_DISPLAY_WIDTH = 720,
    GPU_MAX_DISPLAY_HEIGHT = 576,
    
    // Dither matrix size
    DITHER_MATRIX_SIZE = 4,
    
    // VRAM pages for texture cache
    VRAM_PAGE_WIDTH = 64,
    VRAM_PAGE_HEIGHT = 256,
    VRAM_PAGES_WIDE = VRAM_WIDTH / VRAM_PAGE_WIDTH,   // 16
    VRAM_PAGES_HIGH = VRAM_HEIGHT / VRAM_PAGE_HEIGHT, // 2
    NUM_VRAM_PAGES = VRAM_PAGES_WIDE * VRAM_PAGES_HIGH,
};

// Maximum primitive dimensions
enum {
    MAX_PRIMITIVE_WIDTH = 1024,
    MAX_PRIMITIVE_HEIGHT = 512,
};

// Timing constants
enum {
    NTSC_TICKS_PER_LINE = 3413,
    NTSC_TOTAL_LINES = 263,
    PAL_TICKS_PER_LINE = 3406,
    PAL_TOTAL_LINES = 314,
};

// Drawing area coordinate mask
enum {
    DRAWING_AREA_COORD_MASK = 1023,
};

// ============================================================================
// GPU Enumerations
// ============================================================================

/** DMA Direction (GPUSTAT[30:29]) */
typedef enum {
    GPU_DMA_OFF = 0,           // DMA disabled/finished
    GPU_DMA_FIFO = 1,          // FIFO mode (GP0 via DMA)
    GPU_DMA_CPU_TO_GP0 = 2,    // CPU writes forwarded to GP0
    GPU_DMA_VRAM_TO_CPU = 3    // GPUREAD reads data from VRAM
} GPUDMADirection;

/** GPU Blitter State (DuckStation architecture) */
typedef enum {
    GPU_BLITTER_IDLE = 0,           // Ready to accept commands
    GPU_BLITTER_WRITING_VRAM = 1,   // Receiving VRAM upload data (GP0(0xA0))
    GPU_BLITTER_READING_VRAM = 2    // VRAM readback active (GP0(0xC0))
} GPUBlitterState;

/** GPU Primitive Type */
typedef enum {
    GPU_PRIMITIVE_RESERVED = 0,
    GPU_PRIMITIVE_POLYGON = 1,
    GPU_PRIMITIVE_LINE = 2,
    GPU_PRIMITIVE_RECTANGLE = 3
} GPUPrimitive;

/** Rectangle Size Mode */
typedef enum {
    GPU_RECT_VARIABLE = 0,
    GPU_RECT_1X1 = 1,
    GPU_RECT_8X8 = 2,
    GPU_RECT_16X16 = 3
} GPUDrawRectangleSize;

/** Texture Color Mode (GPUSTAT[8:7]) */
typedef enum {
    GPU_TEXTURE_MODE_PALETTE_4BIT = 0,  // 4-bit indexed (16 colors)
    GPU_TEXTURE_MODE_PALETTE_8BIT = 1,  // 8-bit indexed (256 colors)
    GPU_TEXTURE_MODE_DIRECT_16BIT = 2,  // Direct 15-bit BGR
    GPU_TEXTURE_MODE_RESERVED = 3       // Not used
} GPUTextureMode;

/** Semi-Transparency Mode (GPUSTAT[6:5]) */
typedef enum {
    GPU_TRANSPARENCY_HALF_BG_PLUS_HALF_FG = 0,  // 0.5*B + 0.5*F
    GPU_TRANSPARENCY_BG_PLUS_FG = 1,             // 1.0*B + 1.0*F
    GPU_TRANSPARENCY_BG_MINUS_FG = 2,            // 1.0*B - 1.0*F
    GPU_TRANSPARENCY_BG_PLUS_QUARTER_FG = 3,     // 1.0*B + 0.25*F
    GPU_TRANSPARENCY_DISABLED = 4                // Not a register value
} GPUTransparencyMode;

/** Interlaced Display Mode */
typedef enum {
    GPU_INTERLACED_NONE,
    GPU_INTERLACED_INTERLEAVED_FIELDS,
    GPU_INTERLACED_SEPARATE_FIELDS
} GPUInterlacedDisplayMode;

/** Video Mode (GPUSTAT[20]) */
typedef enum {
    GPU_VIDEO_MODE_NTSC = 0,  // 60Hz
    GPU_VIDEO_MODE_PAL = 1    // 50Hz
} GPUVideoMode;

/** Vertical Resolution (GPUSTAT[19]) */
typedef enum {
    GPU_VERTICAL_RES_240 = 0,  // 240 lines (progressive)
    GPU_VERTICAL_RES_480 = 1   // 480 lines (interlaced)
} GPUVerticalResolution;

/** Display Area Color Depth (GPUSTAT[21]) */
typedef enum {
    GPU_DISPLAY_DEPTH_15BIT = 0,  // 15-bit BGR
    GPU_DISPLAY_DEPTH_24BIT = 1   // 24-bit RGB
} GPUDisplayDepth;

/** Field Type for Interlaced Video (GPUSTAT[13]) */
typedef enum {
    GPU_FIELD_BOTTOM = 0,  // Bottom field (even lines) or progressive
    GPU_FIELD_TOP = 1      // Top field (odd lines)
} GPUField;

/** GP0 Port Mode (Internal State) */
typedef enum {
    GP0_MODE_COMMAND,     // Expecting GP0 command words
    GP0_MODE_IMAGE_LOAD,  // Expecting pixel data for VRAM transfer
    GP0_MODE_IMAGE_STORE  // Sending pixel data from VRAM to CPU
} GP0Mode;

/** Horizontal Resolution Raw (GPUSTAT[18:16]) */
typedef struct {
    uint8_t hr1; // Bits 17:16
    uint8_t hr2; // Bit 18
} HorizontalResRaw;

// ============================================================================
// GPU State Structures
// ============================================================================

/** GPUSTAT Register (All status bits in one 32-bit value) */
typedef union {
    uint32_t bits;
    struct {
        uint32_t texture_page_x_base : 4;       // [3:0]   Texture page X base (N*64)
        uint32_t texture_page_y_base : 1;       // [4]     Texture page Y base (N*256)
        uint32_t semi_transparency_mode : 2;    // [6:5]   Transparency mode
        uint32_t texture_color_mode : 2;        // [8:7]   Texture bit depth
        uint32_t dither_enable : 1;             // [9]     Dithering enabled
        uint32_t draw_to_displayed_field : 1;   // [10]    Drawing to display area allowed
        uint32_t set_mask_while_drawing : 1;    // [11]    Force mask bit on write
        uint32_t check_mask_before_draw : 1;    // [12]    Don't draw to mask=1 pixels
        uint32_t interlaced_field : 1;          // [13]    Current field (interlaced)
        uint32_t reverse_flag : 1;              // [14]    Reverse flag (not used)
        uint32_t texture_disable : 1;           // [15]    Disable texturing
        uint32_t horizontal_resolution_2 : 1;   // [16]    Horizontal resolution bit 2
        uint32_t horizontal_resolution_1 : 2;   // [18:17] Horizontal resolution bits 1-0
        uint32_t vertical_resolution : 1;       // [19]    Vertical resolution
        uint32_t pal_mode : 1;                  // [20]    Video mode (PAL/NTSC)
        uint32_t display_area_color_depth_24 : 1; // [21]  24-bit display mode
        uint32_t vertical_interlace : 1;        // [22]    Interlaced mode enabled
        uint32_t display_disable : 1;           // [23]    Display output disabled
        uint32_t interrupt_request : 1;         // [24]    IRQ flag
        uint32_t dma_data_request : 1;          // [25]    DMA ready flag (hardcoded)
        uint32_t ready_to_receive_cmd : 1;      // [26]    Ready for commands (hardcoded 1)
        uint32_t ready_to_send_vram : 1;        // [27]    Ready to send VRAM (hardcoded 1)
        uint32_t ready_to_receive_dma : 1;      // [28]    Ready for DMA (hardcoded 1)
        uint32_t dma_direction : 2;             // [30:29] DMA direction
        uint32_t drawing_even_odd_lines : 1;    // [31]    Interlace drawing mode
    };
} GPUSTAT;

/** Drawing Area (Clamped Rectangle) */
typedef struct {
    uint16_t left;    // X1 (0-1023)
    uint16_t top;     // Y1 (0-511)
    uint16_t right;   // X2 (0-1023)
    uint16_t bottom;  // Y2 (0-511)
} GPUDrawingArea;

/** Drawing Offset (Signed) */
typedef struct {
    int16_t x;  // -1024 to +1023
    int16_t y;  // -1024 to +1023
} GPUDrawingOffset;

/** Texture Window Settings */
typedef struct {
    uint8_t x_mask;    // Bits 0-4: X mask (8-pixel steps)
    uint8_t y_mask;    // Bits 5-9: Y mask (8-pixel steps)
    uint8_t x_offset;  // Bits 10-14: X offset (8-pixel steps)
    uint8_t y_offset;  // Bits 15-19: Y offset (8-pixel steps)
} GPUTextureWindow;

/** CRTC (Display) State */
typedef struct {
    uint16_t display_vram_left;           // Display area X start in VRAM
    uint16_t display_vram_top;            // Display area Y start in VRAM
    uint16_t display_vram_width;          // Display area width
    uint16_t display_vram_height;         // Display area height
    uint16_t horizontal_display_start;    // H display start (dotclock units)
    uint16_t horizontal_display_end;      // H display end (dotclock units)
    uint16_t vertical_display_start;      // V display start (scanlines)
    uint16_t vertical_display_end;        // V display end (scanlines)
    uint32_t dot_clock_divider;           // Dot clock divider
    uint32_t current_scanline;            // Current raster scanline
    uint32_t current_tick_in_scanline;    // Current tick within scanline
    bool in_vblank;                       // True if in VBlank period
    uint8_t interlaced_field;             // Field for DRAWING (0=odd/1=even) - toggles at start of new frame
    uint8_t interlaced_display_field;     // Field for DISPLAY (0/1) - switches early at vblank start
} CRTCState;

/** GP0 Command Buffer */
#define MAX_GPU_COMMAND_WORDS 16
typedef struct {
    uint32_t buffer[MAX_GPU_COMMAND_WORDS];  // Command parameter storage
    uint8_t count;                            // Number of words in buffer
} GP0CommandBuffer;

/** VRAM Transfer State */
typedef struct {
    uint16_t x;           // Target/source X coordinate
    uint16_t y;           // Target/source Y coordinate
    uint16_t width;       // Transfer width
    uint16_t height;      // Transfer height
    uint32_t count;       // Pixels transferred so far
    uint32_t pixel_count; // Total pixels in transfer
} VRAMTransferState;

#endif // GPU_TYPES_H
