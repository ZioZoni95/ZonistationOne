/**
 * gpu.h
 * Main GPU header - forwards to modular GPU architecture
 * 
 * This file provides backward compatibility with the old monolithic API
 * while using the new modular GPU structure underneath.
 * 
 * New modular structure:
 *   - gpu/gpu_types.h  - Type definitions and enums
 *   - gpu/gpu_core.h   - Main GPU state and public API
 */
#ifndef GPU_H
#define GPU_H

// Include new modular GPU headers
#include "gpu/gpu_types.h"

// ============================================================================
// Legacy Type Aliases (must be defined BEFORE including gpu_core.h)
// ============================================================================

// Old enum names mapped to new GPU types
typedef GPUTextureMode TextureDepth;
#define T4Bit GPU_TEXTURE_MODE_PALETTE_4BIT
#define T8Bit GPU_TEXTURE_MODE_PALETTE_8BIT
#define T15Bit GPU_TEXTURE_MODE_DIRECT_16BIT

typedef GPUField Field;
#define Bottom GPU_FIELD_BOTTOM
#define Top GPU_FIELD_TOP

typedef GPUVerticalResolution VerticalRes;
#define Y240Lines GPU_VERTICAL_RES_240
#define Y480Lines GPU_VERTICAL_RES_480

typedef GPUVideoMode VMode;
#define Ntsc GPU_VIDEO_MODE_NTSC
#define Pal GPU_VIDEO_MODE_PAL

typedef GPUDisplayDepth DisplayDepth;
#define D15Bits GPU_DISPLAY_DEPTH_15BIT
#define D24Bits GPU_DISPLAY_DEPTH_24BIT

typedef GPUDMADirection GpuDmaSetting;
#define GPU_DMA_Off GPU_DMA_OFF
#define GPU_DMA_Fifo GPU_DMA_FIFO
#define GPU_DMA_CpuToGp0 GPU_DMA_CPU_TO_GP0
#define GPU_DMA_VRamToCpu GPU_DMA_VRAM_TO_CPU

typedef GP0Mode Gp0Mode;

typedef GP0CommandBuffer CommandBuffer;

// NOW include gpu_core.h (which uses the types defined above)
#include "gpu/gpu_core.h"

// Legacy type aliases for backward compatibility
typedef GPU Gpu;  // Old code uses 'Gpu', new code uses 'GPU'

// Forward declarations still needed for old code
struct Interconnect;
typedef struct Interconnect Interconnect;

// ============================================================================
// Legacy Function Wrappers (for backward compatibility)
// ============================================================================

// gpu_init_full is defined in gpu.c for backward compatibility

// All other functions are already defined in gpu_core.h and work with both types

#endif // GPU_H