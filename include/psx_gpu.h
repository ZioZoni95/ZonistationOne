#ifndef PSX_GPU_H
#define PSX_GPU_H

#include "psx_types.h"

// PSX-SPX: Graphics Processing Unit Implementation
// Following guide.tex GPU structure with PSX-SPX register specifications

// PSX-SPX: GPU Register addresses
#define GPU_GP0         0x1F801810  // GP0 - Command/Data
#define GPU_GP1         0x1F801814  // GP1 - Status/Command

// PSX-SPX: GPUSTAT bits
#define GPUSTAT_TEXTURE_PAGE_X_MASK     0x0000000F
#define GPUSTAT_TEXTURE_PAGE_Y_MASK     0x00000010
#define GPUSTAT_SEMI_TRANSPARENCY_MASK  0x00000060
#define GPUSTAT_TEXTURE_PAGE_COLORS     0x00000180
#define GPUSTAT_DITHER_ENABLE           0x00000200
#define GPUSTAT_DRAW_TO_DISPLAY         0x00000400
#define GPUSTAT_TEXTURE_DISABLE         0x00000800
#define GPUSTAT_RECTANGLE_TEXTURE_FLIP  0x00001000
#define GPUSTAT_DRAWING_AREA_RIGHT      0x00002000
#define GPUSTAT_DRAWING_AREA_BOTTOM     0x00004000
#define GPUSTAT_INTERLACE_FIELD         0x00080000
#define GPUSTAT_REVERSE_FLAG            0x00800000
#define GPUSTAT_TEXTURE_PAGE_Y_BIT2     0x00800000
#define GPUSTAT_HORIZONTAL_RESOLUTION   0x00000000  // Complex, see PSX-SPX
#define GPUSTAT_VERTICAL_RESOLUTION     0x00080000
#define GPUSTAT_VIDEO_MODE              0x00100000
#define GPUSTAT_DISPLAY_COLOR_DEPTH     0x00200000
#define GPUSTAT_VERTICAL_INTERLACE      0x00400000
#define GPUSTAT_DISPLAY_ENABLE          0x00800000
#define GPUSTAT_IRQ_REQUEST             0x01000000
#define GPUSTAT_DMA_REQUEST             0x02000000
#define GPUSTAT_READY_CMD_WORD          0x04000000
#define GPUSTAT_READY_VRAM_TO_CPU       0x08000000
#define GPUSTAT_READY_DMA_BLOCK         0x10000000
#define GPUSTAT_DMA_DIRECTION           0x60000000

// PSX-SPX: GPU state structure
typedef struct {
    // Registers
    u32 gpustat;        // GPU Status register
    u32 gpuread;        // GPU Read data
    
    // Command processing
    u32 gp0_buffer[16]; // Command buffer
    int gp0_words_remaining;
    int gp0_command_length;
    
    // Display state
    u32 display_area_x, display_area_y;
    u32 display_area_w, display_area_h;
    
    // Drawing state
    u32 drawing_area_x1, drawing_area_y1;
    u32 drawing_area_x2, drawing_area_y2;
    u32 drawing_offset_x, drawing_offset_y;
    
    bool texture_disable;
    bool dither_enable;
    bool draw_to_display_area;
    
    // TODO: Add OpenGL rendering state (Guide.tex chapter 4)
    
} psx_gpu_t;

// GPU interface functions
void gpu_init(void);
void gpu_reset(void);
void gpu_step(void);

// Register access
u32 gpu_read32(u32 addr);
void gpu_write32(u32 addr, u32 value);

// Command processing
void gpu_gp0_command(u32 command);
void gpu_gp1_command(u32 command);

#endif // PSX_GPU_H