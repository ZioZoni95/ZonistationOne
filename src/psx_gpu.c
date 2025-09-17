#include "../include/psx_gpu.h"
#include <stdio.h>
#include <string.h>

// PSX-SPX: Graphics Processing Unit implementation (skeleton)
static psx_gpu_t gpu;

void gpu_init(void) {
    memset(&gpu, 0, sizeof(gpu));
    gpu_reset();
    printf("[GPU] GPU initialized\n");
}

void gpu_reset(void) {
    // PSX-SPX: GPU reset state
    gpu.gpustat = 0x14802000;  // Default GPUSTAT value
    gpu.gpuread = 0;
    
    // Clear command buffer
    memset(gpu.gp0_buffer, 0, sizeof(gpu.gp0_buffer));
    gpu.gp0_words_remaining = 0;
    gpu.gp0_command_length = 0;
    
    // Reset display/drawing areas
    gpu.display_area_x = 0;
    gpu.display_area_y = 0;
    gpu.display_area_w = 0;
    gpu.display_area_h = 0;
    
    gpu.drawing_area_x1 = 0;
    gpu.drawing_area_y1 = 0;
    gpu.drawing_area_x2 = 0;
    gpu.drawing_area_y2 = 0;
    gpu.drawing_offset_x = 0;
    gpu.drawing_offset_y = 0;
    
    gpu.texture_disable = false;
    gpu.dither_enable = false;
    gpu.draw_to_display_area = false;
    
    printf("[GPU] GPU reset, GPUSTAT=0x%08X\n", gpu.gpustat);
}

void gpu_step(void) {
    // TODO: Implement GPU timing and rendering
}

u32 gpu_read32(u32 addr) {
    switch (addr) {
        case GPU_GP0: // GPUREAD
            printf("[GPU] GP0 read = 0x%08X\n", gpu.gpuread);
            return gpu.gpuread;
            
        case GPU_GP1: // GPUSTAT  
            printf("[GPU] GP1 read (GPUSTAT) = 0x%08X\n", gpu.gpustat);
            return gpu.gpustat;
            
        default:
            printf("[GPU] ERROR: Unmapped read32 at 0x%08X\n", addr);
            return 0;
    }
}

void gpu_write32(u32 addr, u32 value) {
    switch (addr) {
        case GPU_GP0: // GP0 Command/Data
            printf("[GPU] GP0 write = 0x%08X\n", value);
            gpu_gp0_command(value);
            break;
            
        case GPU_GP1: // GP1 Command
            printf("[GPU] GP1 write = 0x%08X\n", value);
            gpu_gp1_command(value);
            break;
            
        default:
            printf("[GPU] ERROR: Unmapped write32 at 0x%08X = 0x%08X\n", addr, value);
            break;
    }
}

void gpu_gp0_command(u32 command) {
    // PSX-SPX: GP0 Command processing
    
    if (gpu.gp0_words_remaining == 0) {
        // Start of new command
        u8 opcode = (command >> 24) & 0xFF;
        
        // PSX-SPX: Determine command length based on opcode
        switch (opcode) {
            case 0x00: // NOP
                gpu.gp0_command_length = 1;
                break;
            case 0x01: // Clear Cache
                gpu.gp0_command_length = 1;
                break;
            case 0x20: // Monochrome Triangle
                gpu.gp0_command_length = 4;
                break;
            case 0x28: // Monochrome Quad
                gpu.gp0_command_length = 5;
                break;
            case 0x2C: // Textured Quad
                gpu.gp0_command_length = 9;
                break;
            // TODO: Add more commands
            default:
                printf("[GPU] TODO: Unimplemented GP0 command 0x%02X\n", opcode);
                gpu.gp0_command_length = 1; // Skip unknown commands
                break;
        }
        
        gpu.gp0_words_remaining = gpu.gp0_command_length;
    }
    
    // Store command word
    int index = gpu.gp0_command_length - gpu.gp0_words_remaining;
    gpu.gp0_buffer[index] = command;
    gpu.gp0_words_remaining--;
    
    // Execute complete command
    if (gpu.gp0_words_remaining == 0) {
        u8 opcode = (gpu.gp0_buffer[0] >> 24) & 0xFF;
        
        switch (opcode) {
            case 0x00: // NOP
                printf("[GPU] GP0(00h) - NOP\n");
                break;
            case 0x01: // Clear Cache  
                printf("[GPU] GP0(01h) - Clear Cache\n");
                break;
            default:
                printf("[GPU] TODO: Execute GP0 command 0x%02X\n", opcode);
                break;
        }
    }
}

void gpu_gp1_command(u32 command) {
    // PSX-SPX: GP1 Command processing
    u8 opcode = (command >> 24) & 0xFF;
    
    switch (opcode) {
        case 0x00: // Reset GPU
            printf("[GPU] GP1(00h) - Reset GPU\n");
            gpu_reset();
            break;
            
        case 0x01: // Reset Command Buffer
            printf("[GPU] GP1(01h) - Reset Command Buffer\n");
            gpu.gp0_words_remaining = 0;
            break;
            
        case 0x02: // Acknowledge IRQ
            printf("[GPU] GP1(02h) - Acknowledge IRQ\n");
            gpu.gpustat &= ~GPUSTAT_IRQ_REQUEST;
            break;
            
        case 0x03: // Display Enable
            {
                bool enable = (command & 1) == 0;  // 0=Enable, 1=Disable
                printf("[GPU] GP1(03h) - Display Enable: %s\n", enable ? "ON" : "OFF");
                if (enable) {
                    gpu.gpustat |= GPUSTAT_DISPLAY_ENABLE;
                } else {
                    gpu.gpustat &= ~GPUSTAT_DISPLAY_ENABLE;
                }
            }
            break;
            
        case 0x04: // DMA Direction
            {
                u32 direction = command & 3;
                printf("[GPU] GP1(04h) - DMA Direction: %d\n", direction);
                gpu.gpustat = (gpu.gpustat & ~GPUSTAT_DMA_DIRECTION) | (direction << 29);
            }
            break;
            
        case 0x05: // Display Area Start
            {
                gpu.display_area_x = command & 0x3FF;
                gpu.display_area_y = (command >> 10) & 0x1FF;
                printf("[GPU] GP1(05h) - Display Area: (%d,%d)\n", gpu.display_area_x, gpu.display_area_y);
            }
            break;
            
        case 0x06: // Horizontal Display Range
            {
                u32 x1 = command & 0xFFF;
                u32 x2 = (command >> 12) & 0xFFF;
                printf("[GPU] GP1(06h) - Horizontal Range: %d-%d\n", x1, x2);
            }
            break;
            
        case 0x07: // Vertical Display Range  
            {
                u32 y1 = command & 0x3FF;
                u32 y2 = (command >> 10) & 0x3FF;
                printf("[GPU] GP1(07h) - Vertical Range: %d-%d\n", y1, y2);
            }
            break;
            
        case 0x08: // Display Mode
            {
                printf("[GPU] GP1(08h) - Display Mode: 0x%06X\n", command & 0xFFFFFF);
                // TODO: Set resolution, color depth, interlace, etc.
            }
            break;
            
        default:
            printf("[GPU] TODO: Unimplemented GP1 command 0x%02X\n", opcode);
            break;
    }
}