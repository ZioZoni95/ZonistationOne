/**
 * @file zoni_gpu.h
 * @brief PlayStation GPU emulation for ZoniStationOne
 * 
 * This file defines the GPU structure and emulation interface
 * for the PlayStation's Graphics Processing Unit.
 */

#ifndef ZONI_GPU_H
#define ZONI_GPU_H

#include "zoni_common.h"
#include <SDL2/SDL.h>

// PlayStation GPU constants
#define PSX_GPU_WIDTH 1024
#define PSX_GPU_HEIGHT 512
#define PSX_GPU_VRAM_SIZE (1024 * 512 * 2) // 1MB VRAM
#define PSX_GPU_FRAMEBUFFER_SIZE (640 * 480 * 4) // RGBA framebuffer

// GPU display modes
typedef enum {
    ZONI_GPU_MODE_NTSC = 0,  // 60Hz, 640x480
    ZONI_GPU_MODE_PAL = 1     // 50Hz, 640x480
} zoni_gpu_mode_t;

// GPU status register bits (following PCSX ReARMed)
#define PSXGPU_LCF     (1u<<31)  // Line counter flag
#define PSXGPU_nBUSY   (1u<<26)  // GPU not busy
#define PSXGPU_ILACE   (1u<<22)  // Interlace mode
#define PSXGPU_RGB24   (1u<<21)  // RGB24 mode
#define PSXGPU_DHEIGHT (1u<<19)  // Double height mode
#define PSXGPU_FIELD   (1u<<13)  // Field mode

// Default GPU status (matching PCSX ReARMed)
#define PSX_GPU_STATUS_DEFAULT (PSXGPU_nBUSY | PSXGPU_DHEIGHT | PSXGPU_FIELD)  // 0x10802000

// GPU command types
typedef enum {
    ZONI_GPU_CMD_NOP = 0x00,
    ZONI_GPU_CMD_CLEAR_CACHE = 0x01,
    ZONI_GPU_CMD_FILL_RECT = 0x02,
    ZONI_GPU_CMD_COPY_RECT = 0x03,
    ZONI_GPU_CMD_DRAW_MODE = 0x04,
    ZONI_GPU_CMD_DRAW_AREA = 0x05,
    ZONI_GPU_CMD_DRAW_OFFSET = 0x06,
    ZONI_GPU_CMD_VRAM_TO_CPU = 0x07,
    ZONI_GPU_CMD_CPU_TO_VRAM = 0x08,
    ZONI_GPU_CMD_SEND_VRAM = 0x09,
    ZONI_GPU_CMD_SEND_CPU = 0x0A,
    ZONI_GPU_CMD_DMA_MODE = 0x0B,
    ZONI_GPU_CMD_DISPLAY_MODE = 0x0C,
    ZONI_GPU_CMD_DISPLAY_START = 0x0D,
    ZONI_GPU_CMD_HORIZONTAL_RANGE = 0x0E,
    ZONI_GPU_CMD_VERTICAL_RANGE = 0x0F,
    ZONI_GPU_CMD_DISPLAY_ENABLE = 0x10
} zoni_gpu_command_t;

// GPU configuration
typedef struct {
    zoni_gpu_mode_t mode;
    bool enable_display;
    bool enable_vblank;
    u32 frame_rate;
    u32 display_width;
    u32 display_height;
} zoni_gpu_config_t;

// GPU state
typedef struct zoni_gpu_s {
    // VRAM (Video RAM)
    u8 vram[PSX_GPU_VRAM_SIZE];
    
    // Framebuffer for SDL2
    u32 framebuffer[PSX_GPU_FRAMEBUFFER_SIZE];
    
    // GPU registers
    u32 status;
    u32 read;
    u32 gp0;
    u32 gp1;
    
    // Display settings
    u32 display_start_x;
    u32 display_start_y;
    u32 display_width;
    u32 display_height;
    u32 horizontal_start;
    u32 horizontal_end;
    u32 vertical_start;
    u32 vertical_end;
    
    // Drawing settings
    u32 draw_offset_x;
    u32 draw_offset_y;
    u32 draw_area_x;
    u32 draw_area_y;
    u32 draw_area_width;
    u32 draw_area_height;
    
    // SDL2 components
    SDL_Window* window;
    SDL_Renderer* renderer;
    SDL_Texture* texture;
    
    // Configuration
    zoni_gpu_config_t config;
    
    // State
    bool initialized;
    bool display_enabled;
    u32 frame_count;
    u32 vblank_count;
    
    // Timing
    u64 last_frame_time;
    u64 frame_interval;
    
} zoni_gpu_t;

// GPU functions
zoni_error_t zoni_gpu_init(zoni_gpu_t* gpu, const zoni_gpu_config_t* config);
void zoni_gpu_shutdown(zoni_gpu_t* gpu);
void zoni_gpu_reset(zoni_gpu_t* gpu);

// GPU control
zoni_error_t zoni_gpu_write_gp0(zoni_gpu_t* gpu, u32 value);
zoni_error_t zoni_gpu_write_gp1(zoni_gpu_t* gpu, u32 value);
u32 zoni_gpu_read_gp0(zoni_gpu_t* gpu);
u32 zoni_gpu_read_gp1(zoni_gpu_t* gpu);

// GPU rendering
zoni_error_t zoni_gpu_render_frame(zoni_gpu_t* gpu);
zoni_error_t zoni_gpu_update_display(zoni_gpu_t* gpu);
zoni_error_t zoni_gpu_clear_screen(zoni_gpu_t* gpu, u32 color);

// GPU VRAM access
zoni_error_t zoni_gpu_vram_write(zoni_gpu_t* gpu, u32 address, u32 value);
zoni_error_t zoni_gpu_vram_read(zoni_gpu_t* gpu, u32 address, u32* value);

// GPU timing
void zoni_gpu_vblank(zoni_gpu_t* gpu);
bool zoni_gpu_is_vblank(zoni_gpu_t* gpu);

// Debug functions
void zoni_gpu_dump_registers(zoni_gpu_t* gpu);
void zoni_gpu_dump_vram(zoni_gpu_t* gpu, u32 address, u32 size);

#endif // ZONI_GPU_H 