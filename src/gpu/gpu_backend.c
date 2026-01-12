/**
 * gpu_backend.c
 * GPU Backend Abstraction
 * 
 * Implements generic rendering helpers similar to DuckStation's GPUBackend class.
 * This module bridges the gap between the core GPU logic and the specific renderer implementation.
 */

#include "gpu/gpu_backend.h"
#include "log.h"
#include <math.h>

void GPU_Backend_GetScreenQuadClipSpaceCoordinates(
    int32_t bounds_x, int32_t bounds_y, int32_t bounds_w, int32_t bounds_h,
    int32_t rt_width, int32_t rt_height,
    float* out_x, float* out_y
) {
    // DuckStation Logic:
    // const GSVector2 fsize = GSVector2(rt_size);
    // const GSVector2 x = ((fboundsxxyy.xy() * GSVector2::cxpr(2.0f)) / fsize.xx()) - GSVector2::cxpr(1.0f);
    // const GSVector2 y = GSVector2::cxpr(1.0f) - (GSVector2::cxpr(2.0f) * (fboundsxxyy.zw() / fsize.yy()));

    float f_rt_w = (float)rt_width;
    float f_rt_h = (float)rt_height;

    // Normalize bounds to -1.0 to 1.0 (NDC)
    // X: (2 * x / w) - 1
    // Y: 1 - (2 * y / h)  (Inverted Y for OpenGL)

    float x0 = (2.0f * (float)bounds_x / f_rt_w) - 1.0f;
    float x1 = (2.0f * (float)(bounds_x + bounds_w) / f_rt_w) - 1.0f;
    
    float y0 = 1.0f - (2.0f * (float)bounds_y / f_rt_h);
    float y1 = 1.0f - (2.0f * (float)(bounds_y + bounds_h) / f_rt_h);

    // Output format expects 4 floats? DuckStation returns GSVector4.
    // The caller in DrawScreenQuad uses .xy() .zyzw() etc.
    // Let's simplified this to return the 4 corners.
    
    // Quad vertices: TL, TR, BL, BR
    // x: x0, x1, x0, x1
    // y: y0, y0, y1, y1
    
    if (out_x) {
        out_x[0] = x0; out_x[1] = x1; out_x[2] = x0; out_x[3] = x1;
    }
    if (out_y) {
        out_y[0] = y0; out_y[1] = y0; out_y[2] = y1; out_y[3] = y1;
    }
}

void GPU_Backend_DrawScreenQuad(
    Renderer* renderer,
    int32_t bounds_x, int32_t bounds_y, int32_t bounds_w, int32_t bounds_h,
    int32_t rt_width, int32_t rt_height,
    float uv_x, float uv_y, float uv_w, float uv_h
) {
    if (!renderer) return;

    // Use backend logic to calculate NDC coordinates
    // In a real implementation, we would write directly to a vertex buffer mapped from the GPU.
    // Here we adapt to the existing Renderer's push_quad API which expects VRAM integer coordinates.
    // However, DrawScreenQuad is usually for presenting to the WINDOW, not drawing to VRAM.
    
    // CURRENT LIMITATION: The current Renderer implementation (src/renderer.c) is hardcoded 
    // to transform integer VRAM coordinates to NDC using 'screen_scale' uniform.
    // To support generic screen quads (like for UI or final presentation), we would need:
    // 1. A new shader in Renderer that accepts raw NDC coordinates.
    // 2. OR modify renderer_push_quad to handle float coordinates.
    
    // For now, we will assume we are drawing to the VRAM area and convert back to integer coords if possible,
    // OR we use the existing 'renderer_display_vram' logic which bypasses some checks.
    
    // Since the user wants to REFRACTOR, this function serves as a placeholder for the logic 
    // that SHOULD exist in the renderer to handle generic screen drawing.
    
    // DuckStation Logic:
    // vertices[0].Set(xy.xy(), uv_bounds.xy());
    // vertices[1].Set(xy.zyzw().xy(), uv_bounds.zyzw().xy());
    // ...
    // g_gpu_device->Draw(0, 4);

    // Mapping to our renderer:
    // We can use renderer_blit_vram or renderer_display_vram as they are our current "Backend" implementations.
    // But to be strictly "Backend" agnostic, we should be generating the vertices here.
    
    LOG_WARN("GPU_Backend_DrawScreenQuad: Not fully implemented. Using renderer_display_vram fallback.");
    
    // Fallback: This assumes the input bounds map 1:1 to the VRAM display area.
    renderer_display_vram(renderer, (uint16_t)uv_x, (uint16_t)uv_y, (uint16_t)uv_w, (uint16_t)uv_h);
}
