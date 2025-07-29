/**
 * @file zoni_gpu.c
 * @brief PlayStation GPU emulation for ZoniStationOne
 * 
 * This file implements the GPU emulation following the PlayStation's
 * Graphics Processing Unit specifications.
 */

#include "zoni_gpu.h"
#include "zoni_endian.h"
#include <string.h>

// GPU initialization
zoni_error_t zoni_gpu_init(zoni_gpu_t* gpu, const zoni_gpu_config_t* config) {
    if (!gpu || !config) {
        return ZONI_ERROR_INVALID_PARAMETER;
    }
    
    // Initialize SDL2 video subsystem
    if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) {
        zoni_log(ZONI_LOG_ERROR, "Failed to initialize SDL2 video: %s", SDL_GetError());
        return ZONI_ERROR_INITIALIZATION_FAILED;
    }
    
    // Create SDL2 window
    gpu->window = SDL_CreateWindow(
        "ZoniStationOne - PlayStation Emulator",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        640, 480,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE
    );
    
    if (!gpu->window) {
        zoni_log(ZONI_LOG_ERROR, "Failed to create SDL2 window: %s", SDL_GetError());
        return ZONI_ERROR_INITIALIZATION_FAILED;
    }
    
    // Create SDL2 renderer
    gpu->renderer = SDL_CreateRenderer(gpu->window, -1, SDL_RENDERER_ACCELERATED);
    if (!gpu->renderer) {
        zoni_log(ZONI_LOG_ERROR, "Failed to create SDL2 renderer: %s", SDL_GetError());
        return ZONI_ERROR_INITIALIZATION_FAILED;
    }
    
    // Create SDL2 texture for framebuffer
    gpu->texture = SDL_CreateTexture(
        gpu->renderer,
        SDL_PIXELFORMAT_RGBA8888,
        SDL_TEXTUREACCESS_STREAMING,
        640, 480
    );
    
    if (!gpu->texture) {
        zoni_log(ZONI_LOG_ERROR, "Failed to create SDL2 texture: %s", SDL_GetError());
        return ZONI_ERROR_INITIALIZATION_FAILED;
    }
    
    // Initialize GPU state
    memset(gpu, 0, sizeof(zoni_gpu_t));
    gpu->config = *config;
    gpu->initialized = true;
    gpu->display_enabled = true;
    
    // Set up timing
    gpu->frame_interval = (config->mode == ZONI_GPU_MODE_NTSC) ? 16667 : 20000; // 60Hz vs 50Hz
    gpu->last_frame_time = SDL_GetPerformanceCounter();
    
    // Initialize GPU registers
    gpu->status = ZONI_GPU_STATUS_READY | ZONI_GPU_STATUS_DMA_READY;
    gpu->display_width = 640;
    gpu->display_height = 480;
    
    // Clear VRAM and framebuffer
    memset(gpu->vram, 0, PSX_GPU_VRAM_SIZE);
    memset(gpu->framebuffer, 0, sizeof(gpu->framebuffer));
    
    zoni_log(ZONI_LOG_INFO, "GPU initialized successfully (SDL2)");
    return ZONI_SUCCESS;
}

void zoni_gpu_shutdown(zoni_gpu_t* gpu) {
    if (!gpu) return;
    
    if (gpu->texture) {
        SDL_DestroyTexture(gpu->texture);
        gpu->texture = NULL;
    }
    
    if (gpu->renderer) {
        SDL_DestroyRenderer(gpu->renderer);
        gpu->renderer = NULL;
    }
    
    if (gpu->window) {
        SDL_DestroyWindow(gpu->window);
        gpu->window = NULL;
    }
    
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
    gpu->initialized = false;
    
    zoni_log(ZONI_LOG_INFO, "GPU shutdown");
}

void zoni_gpu_reset(zoni_gpu_t* gpu) {
    if (!gpu) return;
    
    // Reset GPU registers
    gpu->status = ZONI_GPU_STATUS_READY | ZONI_GPU_STATUS_DMA_READY;
    gpu->read = 0;
    gpu->gp0 = 0;
    gpu->gp1 = 0;
    
    // Reset display settings
    gpu->display_start_x = 0;
    gpu->display_start_y = 0;
    gpu->display_width = 640;
    gpu->display_height = 480;
    gpu->horizontal_start = 0;
    gpu->horizontal_end = 640;
    gpu->vertical_start = 0;
    gpu->vertical_end = 480;
    
    // Reset drawing settings
    gpu->draw_offset_x = 0;
    gpu->draw_offset_y = 0;
    gpu->draw_area_x = 0;
    gpu->draw_area_y = 0;
    gpu->draw_area_width = 640;
    gpu->draw_area_height = 480;
    
    // Clear VRAM and framebuffer
    memset(gpu->vram, 0, PSX_GPU_VRAM_SIZE);
    memset(gpu->framebuffer, 0, sizeof(gpu->framebuffer));
    
    gpu->frame_count = 0;
    gpu->vblank_count = 0;
    gpu->display_enabled = true;
    
    zoni_log(ZONI_LOG_INFO, "GPU reset");
}

// Helper function for filling rectangles
static void zoni_gpu_fill_rectangle(zoni_gpu_t* gpu, u32 x, u32 y, u32 width, u32 height, u32 color) {
    if (!gpu || !gpu->initialized) return;
    
    // Convert PlayStation color to RGBA
    u8 r = (color >> 0) & 0xFF;
    u8 g = (color >> 8) & 0xFF;
    u8 b = (color >> 16) & 0xFF;
    u32 rgba = (r << 0) | (g << 8) | (b << 16) | (0xFF << 24);
    
    // Fill rectangle in framebuffer
    for (u32 py = y; py < y + height && py < 480; py++) {
        for (u32 px = x; px < x + width && px < 640; px++) {
            gpu->framebuffer[py * 640 + px] = rgba;
        }
    }
}

// GPU control functions
zoni_error_t zoni_gpu_write_gp0(zoni_gpu_t* gpu, u32 value) {
    if (!gpu || !gpu->initialized) {
        return ZONI_ERROR_INVALID_PARAMETER;
    }
    
    // Store the command
    gpu->gp0 = value;
    
    // Extract command
    u8 command = (value >> 24) & 0xFF;
    
            zoni_log(ZONI_LOG_INFO, "GPU GP0 write: 0x%08X (command: 0x%02X)", value, command);
    
    // Handle different commands
    switch (command) {
        case ZONI_GPU_CMD_CLEAR_CACHE:
            // Clear GPU cache (no-op for now)
            break;
            
        case ZONI_GPU_CMD_FILL_RECT:
            // Fill rectangle command
            {
                u32 color = value & 0xFFFFFF;
                u32 x = (value >> 12) & 0x3FF;
                u32 y = (value >> 22) & 0x1FF;
                u32 width = ((value >> 0) & 0x3FF) + 1;
                u32 height = ((value >> 10) & 0x1FF) + 1;
                
                zoni_gpu_fill_rectangle(gpu, x, y, width, height, color);
            }
            break;
            
        case ZONI_GPU_CMD_DISPLAY_ENABLE:
            gpu->display_enabled = (value & 1) != 0;
            zoni_log(ZONI_LOG_DEBUG, "GPU display %s", gpu->display_enabled ? "enabled" : "disabled");
            break;
            
        default:
            zoni_log(ZONI_LOG_DEBUG, "GPU command 0x%02X not implemented", command);
            break;
    }
    
    return ZONI_SUCCESS;
}

zoni_error_t zoni_gpu_write_gp1(zoni_gpu_t* gpu, u32 value) {
    if (!gpu || !gpu->initialized) {
        return ZONI_ERROR_INVALID_PARAMETER;
    }
    
    gpu->gp1 = value;
    
    // Extract command
    u8 command = (value >> 24) & 0xFF;
    
            zoni_log(ZONI_LOG_INFO, "GPU GP1 write: 0x%08X (command: 0x%02X)", value, command);
    
    switch (command) {
        case 0x00: // Reset GPU
            zoni_gpu_reset(gpu);
            break;
            
        case 0x03: // Display enable/disable
            gpu->display_enabled = (value & 1) != 0;
            break;
            
        case 0x04: // DMA direction
            // Handle DMA direction setting
            break;
            
        case 0x05: // Display start address
            gpu->display_start_x = (value & 0x3FF) * 64;
            gpu->display_start_y = ((value >> 10) & 0x1FF) * 2;
            break;
            
        case 0x06: // Horizontal display range
            gpu->horizontal_start = (value & 0xFFF) * 8;
            gpu->horizontal_end = ((value >> 12) & 0xFFF) * 8;
            break;
            
        case 0x07: // Vertical display range
            gpu->vertical_start = (value & 0x3FF);
            gpu->vertical_end = ((value >> 10) & 0x3FF);
            break;
            
        case 0x08: // Display mode
            // Handle display mode setting
            break;
            
        default:
            zoni_log(ZONI_LOG_DEBUG, "GPU GP1 command 0x%02X not implemented", command);
            break;
    }
    
    return ZONI_SUCCESS;
}

u32 zoni_gpu_read_gp0(zoni_gpu_t* gpu) {
    if (!gpu || !gpu->initialized) {
        return 0;
    }
    
    return gpu->read;
}

u32 zoni_gpu_read_gp1(zoni_gpu_t* gpu) {
    if (!gpu || !gpu->initialized) {
        return 0;
    }
    
    u32 status = gpu->status;
    
    // Update status based on current state
    if (gpu->display_enabled) {
        status |= ZONI_GPU_STATUS_DISPLAY_ENABLED;
    }
    
    return status;
}

// GPU rendering functions
zoni_error_t zoni_gpu_render_frame(zoni_gpu_t* gpu) {
    if (!gpu || !gpu->initialized) {
        return ZONI_ERROR_INVALID_PARAMETER;
    }
    
    // Update display if enabled
    if (gpu->display_enabled) {
        zoni_gpu_update_display(gpu);
    }
    
    gpu->frame_count++;
    return ZONI_SUCCESS;
}

zoni_error_t zoni_gpu_update_display(zoni_gpu_t* gpu) {
    if (!gpu || !gpu->initialized || !gpu->texture) {
        return ZONI_ERROR_INVALID_PARAMETER;
    }
    
    // Update SDL2 texture with framebuffer data
    SDL_UpdateTexture(gpu->texture, NULL, gpu->framebuffer, 640 * 4);
    
    // Clear renderer
    SDL_SetRenderDrawColor(gpu->renderer, 0, 0, 0, 255);
    SDL_RenderClear(gpu->renderer);
    
    // Render texture
    SDL_RenderCopy(gpu->renderer, gpu->texture, NULL, NULL);
    
    // Present renderer
    SDL_RenderPresent(gpu->renderer);
    
    return ZONI_SUCCESS;
}

zoni_error_t zoni_gpu_clear_screen(zoni_gpu_t* gpu, u32 color) {
    if (!gpu || !gpu->initialized) {
        return ZONI_ERROR_INVALID_PARAMETER;
    }
    
    // Clear framebuffer with color
    for (int i = 0; i < PSX_GPU_FRAMEBUFFER_SIZE / 4; i++) {
        gpu->framebuffer[i] = color;
    }
    
    return ZONI_SUCCESS;
}

// GPU VRAM access
zoni_error_t zoni_gpu_vram_write(zoni_gpu_t* gpu, u32 address, u32 value) {
    if (!gpu || !gpu->initialized || address >= PSX_GPU_VRAM_SIZE) {
        return ZONI_ERROR_INVALID_PARAMETER;
    }
    
    // Write to VRAM
    zoni_write_le32(&gpu->vram[address], value);
    
    return ZONI_SUCCESS;
}

zoni_error_t zoni_gpu_vram_read(zoni_gpu_t* gpu, u32 address, u32* value) {
    if (!gpu || !gpu->initialized || !value || address >= PSX_GPU_VRAM_SIZE) {
        return ZONI_ERROR_INVALID_PARAMETER;
    }
    
    // Read from VRAM
    *value = zoni_read_le32(&gpu->vram[address]);
    
    return ZONI_SUCCESS;
}

// GPU timing
void zoni_gpu_vblank(zoni_gpu_t* gpu) {
    if (!gpu || !gpu->initialized) return;
    
    gpu->status |= ZONI_GPU_STATUS_VBLANK;
    gpu->vblank_count++;
}

bool zoni_gpu_is_vblank(zoni_gpu_t* gpu) {
    if (!gpu || !gpu->initialized) return false;
    
    return (gpu->status & ZONI_GPU_STATUS_VBLANK) != 0;
}

// Debug functions
void zoni_gpu_dump_registers(zoni_gpu_t* gpu) {
    if (!gpu) return;
    
    zoni_log(ZONI_LOG_INFO, "GPU Registers:");
    zoni_log(ZONI_LOG_INFO, "  Status: 0x%08X", gpu->status);
    zoni_log(ZONI_LOG_INFO, "  Read: 0x%08X", gpu->read);
    zoni_log(ZONI_LOG_INFO, "  GP0: 0x%08X", gpu->gp0);
    zoni_log(ZONI_LOG_INFO, "  GP1: 0x%08X", gpu->gp1);
    zoni_log(ZONI_LOG_INFO, "  Display: %dx%d at (%d,%d)", 
             gpu->display_width, gpu->display_height, 
             gpu->display_start_x, gpu->display_start_y);
    zoni_log(ZONI_LOG_INFO, "  Enabled: %s", gpu->display_enabled ? "yes" : "no");
    zoni_log(ZONI_LOG_INFO, "  Frames: %d, VBlanks: %d", gpu->frame_count, gpu->vblank_count);
}

void zoni_gpu_dump_vram(zoni_gpu_t* gpu, u32 address, u32 size) {
    if (!gpu || address >= PSX_GPU_VRAM_SIZE) return;
    
    zoni_log(ZONI_LOG_INFO, "GPU VRAM dump at 0x%08X (%d bytes):", address, size);
    
    for (u32 i = 0; i < size && address + i < PSX_GPU_VRAM_SIZE; i += 16) {
        char line[256];
        int pos = 0;
        
        pos += snprintf(line + pos, sizeof(line) - pos, "0x%08X: ", address + i);
        
        for (u32 j = 0; j < 16 && address + i + j < PSX_GPU_VRAM_SIZE; j++) {
            pos += snprintf(line + pos, sizeof(line) - pos, "%02X ", gpu->vram[address + i + j]);
        }
        
        zoni_log(ZONI_LOG_INFO, "%s", line);
    }
} 