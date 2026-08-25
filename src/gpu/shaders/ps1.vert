// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025-2026 ZioZoni95
#version 450
#extension GL_GOOGLE_include_directive : require
#include "ps1_common.glsl"

layout(location = 0) in ivec2 vertex_position;  // PSX VRAM coords (int16)
layout(location = 1) in uvec3 vertex_color;     // PSX RGB (uint8)
layout(location = 2) in ivec2 vertex_texcoord;  // PSX VRAM texcoords (int16)
layout(location = 3) in uvec2 vertex_tpage;     // CLUT (x), TPage (y) (uint16)

layout(location = 0) out vec3 color;
layout(location = 1) out vec2 tex_coord;
layout(location = 2) flat out uvec2 tpage_info;

void main() {
    ivec2 p = vertex_position + pc.offset;

    // Y is NOT flipped, in either API, and for the same reason: the render
    // target is the unified VRAM image, whose row N must be PSX VRAM line N —
    // the same row a CPU or MDEC upload writes and the same row the scanout
    // pass reads. GL puts image row 0 at NDC y=-1 counting up from the bottom;
    // Vulkan puts image row 0 at NDC y=-1 counting down from the top. Opposite
    // conventions that agree on where row 0 lands, so the mapping below is
    // identical in both and gl_FragCoord.y is the VRAM row in both.
    float xpos = (float(p.x) / pc.screen_scale.x) - 1.0;
    float ypos = (float(p.y) / pc.screen_scale.y) - 1.0;
    gl_Position = vec4(xpos, ypos, 0.0, 1.0);

    color = vec3(float(vertex_color.r) / 255.0,
                 float(vertex_color.g) / 255.0,
                 float(vertex_color.b) / 255.0);
    tex_coord  = vec2(float(vertex_texcoord.x), float(vertex_texcoord.y));
    tpage_info = vertex_tpage;
}
