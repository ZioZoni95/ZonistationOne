// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025-2026 ZioZoni95
//
// PS1 primitive rasteriser, fragment stage. A faithful port of the OpenGL
// fragment shader in renderer_gl.c: same CLUT decode, same texture window, same
// STP two-pass split, same 4x4 dither, same mask-bit semantics. Parity with the
// GL backend is the point — a difference here is a defect, not a feature.
#version 450
#extension GL_GOOGLE_include_directive : require
#include "ps1_common.glsl"

layout(location = 0) in vec3 color;
layout(location = 1) in vec2 tex_coord;
layout(location = 2) flat in uvec2 tpage_info;   // x = CLUT, y = TPage

layout(location = 0) out vec4 frag_color;

// The unified VRAM image, sampled while it is also the colour attachment.
//
// That is a feedback loop, and it is legal only because the image sits in
// GENERAL layout and the backend issues a pipeline barrier between batches —
// exactly where the GL backend calls glTextureBarrier(). Reads inside one batch
// are as undefined here as they are there; both APIs only promise to see what
// earlier *draws* wrote. Anything stricter would need
// VK_EXT_fragment_shader_interlock, which this device has, and which is the
// upgrade path once parity is proven.
layout(set = 0, binding = 0) uniform sampler2D vram_texture;

// Recover the PS1 halfword from the 5:5:5:1-expanded RGBA8 texel. The expansion
// ((v<<3)|(v>>2)) is exactly invertible, so this round-trips losslessly.
uint to16(vec4 t) {
    uint r = uint(t.r * 255.0 + 0.5) >> 3;
    uint g = uint(t.g * 255.0 + 0.5) >> 3;
    uint b = uint(t.b * 255.0 + 0.5) >> 3;
    uint a = (t.a > 0.5) ? 1u : 0u;
    return r | (g << 5) | (b << 10) | (a << 15);
}

uint vram_read(ivec2 p) { return to16(texelFetch(vram_texture, p, 0)); }

vec3 psx_to_rgb(uint raw) {
    return vec3(float( raw        & 0x1Fu) / 31.0,
                float((raw >>  5) & 0x1Fu) / 31.0,
                float((raw >> 10) & 0x1Fu) / 31.0);
}

void main() {
    // Mask-bit test, GP0(E6).1. Hardware skips a pixel whose destination
    // halfword already has bit 15 set, which is how a game protects sprites it
    // has drawn from being overpainted.
    if (pc.mask_test == 1) {
        uint dst = vram_read(ivec2(gl_FragCoord.xy));
        if ((dst & 0x8000u) != 0u) discard;
    }

    // Alpha is not opacity: the unified VRAM image stores the PS1 mask bit in
    // it, so a drawn pixel must carry the bit a CPU write would. Untextured
    // primitives write 0 unless GP0(E6).0 forces it; textured ones copy the
    // source texel's bit 15 (STP), OR'd with the force flag.
    float mask_a = (pc.set_mask != 0) ? 1.0 : 0.0;
    vec4 final_color = vec4(color, mask_a);

    if (pc.use_texture == 1) {
        uint clut   = tpage_info.x;
        uint tpage  = tpage_info.y;
        uint depth  = (tpage >> 7) & 3u;
        uint page_x = (tpage & 0xFu) * 64u;
        uint page_y = ((tpage >> 4) & 1u) * 256u;
        uint clut_x = (clut & 0x3Fu) * 16u;
        uint clut_y = (clut >> 6) & 0x1FFu;

        // Through int, then clamp. GLSL leaves float-to-uint undefined for
        // negatives, and interpolation rounding puts a vertex UV of 0
        // fractionally below zero: NVIDIA saturated to 0 while Mesa wrapped to
        // 0xFFFFFFFF, which the 0xFF mask turned into column 255. That was one
        // of the three pieces of undefined GL behind the iGPU artifacts, and it
        // is undefined in Vulkan GLSL for the same reason.
        uint u_raw = uint(clamp(int(tex_coord.x), 0, 255));
        uint v_raw = uint(clamp(int(tex_coord.y), 0, 255));
        uint u = (u_raw & uint(pc.tex_window.x)) | uint(pc.tex_window.z);
        uint v = (v_raw & uint(pc.tex_window.y)) | uint(pc.tex_window.w);

        vec3 tex_rgb   = vec3(0.0);
        uint raw_color = 0u;

        if (depth == 0u) {                       // 4bpp paletted
            uint raw_word = vram_read(ivec2(page_x + (u / 4u), page_y + v));
            uint index    = (raw_word >> ((u & 3u) * 4u)) & 0xFu;
            raw_color     = vram_read(ivec2(clut_x + index, clut_y));
        } else if (depth == 1u) {                // 8bpp paletted
            uint raw_word = vram_read(ivec2(page_x + (u / 2u), page_y + v));
            uint index    = (raw_word >> ((u & 1u) * 8u)) & 0xFFu;
            raw_color     = vram_read(ivec2(clut_x + index, clut_y));
        } else {                                 // 15bpp direct (depth 2 or 3)
            raw_color     = vram_read(ivec2(page_x + u, page_y + v));
        }

        // Fully black and STP clear is the PS1's transparent texel.
        if (raw_color == 0u) discard;
        // The two-pass split for semi-transparent textured primitives: blend
        // state is per-draw but STP is per-texel, so pass 1 draws the opaque
        // texels with blending off and pass 2 the semi-transparent ones with it
        // on. A device with dualSrcBlend can collapse these into one pass.
        if (pc.stp_mode == 0 && (raw_color & 0x8000u) != 0u) discard;
        if (pc.stp_mode == 1 && (raw_color & 0x8000u) == 0u) discard;

        tex_rgb = psx_to_rgb(raw_color);
        if ((raw_color & 0x8000u) != 0u) mask_a = 1.0;

        final_color = (pc.raw_texture == 1) ? vec4(tex_rgb, mask_a)
                                            : vec4(tex_rgb * color * 2.0, mask_a);
    }

    if (pc.dither_enable == 1) {
        const int dither_table[16] = int[16](
            -4,  0, -3,  1,
             2, -2,  3, -1,
            -3,  1, -4,  0,
             3, -1,  2, -2
        );
        int   dx   = int(mod(gl_FragCoord.x, 4.0));
        int   dy   = int(mod(gl_FragCoord.y, 4.0));
        float doff = float(dither_table[dy * 4 + dx]) / 255.0;
        vec3  c_d  = clamp(final_color.rgb + vec3(doff), 0.0, 1.0);
        final_color.rgb = floor(c_d * 255.0 / 8.0) / 31.0;
    }

    frag_color = final_color;
}
