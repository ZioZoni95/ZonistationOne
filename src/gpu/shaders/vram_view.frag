// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025-2026 ZioZoni95
//
// VRAM debug viewer: decodes the whole 1024x512 store the way the UI asks for.
// Port of the GL viewer shader in renderer_gl.c, unchanged in behaviour.
// Debug surface only — it never touches what the emulated machine sees.
#version 450

layout(location = 0) in  vec2 v_uv;
layout(location = 0) out vec4 o_col;

layout(set = 0, binding = 0) uniform sampler2D u_vram;

layout(push_constant) uniform ViewerPush {
    ivec2 clut;     // CLUT position for the indexed modes
    ivec3 flags;    // x = greyscale, y = show_alpha, z = shift24 phase
    int   mode;     // 0 = 4bpp, 1 = 8bpp, 2 = 16bpp, 3 = 24bpp
} pc;

// Same 5:5:5:1 recovery the scanout pass uses — bit 15 included, because in
// 24bpp it is a data bit and in mask view it is the value being looked at.
uint to16(ivec2 p) {
    vec4 t = texelFetch(u_vram, ivec2(clamp(p.x, 0, 1023), clamp(p.y, 0, 511)), 0);
    uint r = uint(t.r * 255.0 + 0.5) >> 3;
    uint g = uint(t.g * 255.0 + 0.5) >> 3;
    uint b = uint(t.b * 255.0 + 0.5) >> 3;
    uint a = (t.a > 0.5) ? 1u : 0u;
    return r | (g << 5) | (b << 10) | (a << 15);
}

vec3 expand(uint v) {
    return vec3(float( v        & 0x1Fu),
                float((v >>  5) & 0x1Fu),
                float((v >> 10) & 0x1Fu)) * (8.0 / 255.0);
}

// One VRAM halfword lives at linear index y*1024+x; the indexed modes need
// arbitrary CLUT entries, so index arithmetic is done flat and folded back.
uint at(int idx) { return to16(ivec2(idx & 1023, (idx >> 10) & 511)); }

void main() {
    int  x   = int(v_uv.x * 1024.0);
    int  y   = int(v_uv.y * 512.0);
    int  idx = y * 1024 + x;
    uint raw = at(idx);
    vec3 c;

    if (pc.mode == 0) {
        // 4bpp: four indices per halfword, averaged into this slot so the whole
        // page stays visible at 1:1.
        int base = pc.clut.y * 1024 + pc.clut.x;
        c = vec3(0.0);
        for (int n = 0; n < 4; n++)
            c += expand(at(base + int((raw >> uint(n * 4)) & 0xFu)));
        c *= 0.25;
    } else if (pc.mode == 1) {
        int  base = pc.clut.y * 1024 + pc.clut.x;
        vec3 e0   = expand(at(base + int( raw       & 0xFFu)));
        vec3 e1   = expand(at(base + int((raw >> 8) & 0xFFu)));
        c = (e0 + e1) * 0.5;
    } else if (pc.mode == 3) {
        // 24bpp: three bytes per pixel straddling halfwords — read the byte
        // stream at this slot, offset by the phase.
        int  off = idx * 2 + pc.flags.z;
        uint b0  = (at( off      >> 1) >> uint(( off      & 1) * 8)) & 0xFFu;
        uint b1  = (at((off + 1) >> 1) >> uint(((off + 1) & 1) * 8)) & 0xFFu;
        uint b2  = (at((off + 2) >> 1) >> uint(((off + 2) & 1) * 8)) & 0xFFu;
        c = vec3(float(b0), float(b1), float(b2)) / 255.0;
    } else {
        c = expand(raw);
    }

    if (pc.flags.y != 0) {
        // Mask bit only — makes write-protected pixels obvious.
        c = vec3(((raw & 0x8000u) != 0u) ? 1.0 : 0.0);
    } else if (pc.flags.x != 0) {
        c = vec3(dot(c, vec3(77.0, 150.0, 29.0) / 256.0));
    }
    o_col = vec4(c, 1.0);
}
