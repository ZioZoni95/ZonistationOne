// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025-2026 ZioZoni95
//
// CRTC scanout: extracts the display window out of the unified VRAM image.
// Port of the GL scanout shader in renderer_gl.c, unchanged in behaviour.
#version 450

layout(location = 0) in  vec2 v_uv;
layout(location = 0) out vec4 o_col;

layout(set = 0, binding = 0) uniform sampler2D u_vram;

layout(push_constant) uniform ScanoutPush {
    ivec2 disp_off;    // display start (VRAM halfword x, line y)
    ivec2 disp_size;   // display size in output pixels
    int   depth24;     // GPUSTAT.21
} pc;

// Recover the raw PS1 halfword from the 5:5:5:1-expanded RGBA8 texel. Bit 15
// must come back too: in 24bpp it is a data bit of the packed byte stream, not
// a mask flag, so dropping it corrupts every pixel whose high byte is >= 0x80.
uint to16(vec4 t) {
    uint r = uint(t.r * 255.0 + 0.5) >> 3;
    uint g = uint(t.g * 255.0 + 0.5) >> 3;
    uint b = uint(t.b * 255.0 + 0.5) >> 3;
    uint a = (t.a > 0.5) ? 1u : 0u;
    return r | (g << 5) | (b << 10) | (a << 15);
}

void main() {
    int px = int(v_uv.x * float(pc.disp_size.x));
    int py = int(v_uv.y * float(pc.disp_size.y));
    int y  = pc.disp_off.y + py;

    if (pc.depth24 == 1) {
        // 24bpp: 3 bytes per pixel spanning 1.5 halfwords — recombine two
        // texels and byte-shift on odd pixels.
        int  hx = pc.disp_off.x + (px * 3) / 2;
        uint s0 = to16(texelFetch(u_vram, ivec2(hx,     y), 0));
        uint s1 = to16(texelFetch(u_vram, ivec2(hx + 1, y), 0));
        uint c  = ((s1 << 16) | s0) >> (uint(px & 1) * 8u);
        o_col = vec4(float(c & 0xFFu), float((c >> 8) & 0xFFu), float((c >> 16) & 0xFFu), 255.0) / 255.0;
    } else {
        // 15bpp: the texel already holds the expanded colour.
        o_col = vec4(texelFetch(u_vram, ivec2(pc.disp_off.x + px, y), 0).rgb, 1.0);
    }
}
