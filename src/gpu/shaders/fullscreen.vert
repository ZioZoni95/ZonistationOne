// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025-2026 ZioZoni95
//
// Attribute-less fullscreen triangle, shared by the scanout and VRAM-viewer
// passes. Draw with vkCmdDraw(cmd, 3, 1, 0, 0) and no vertex buffer bound.
//
// v_uv.y == 0 lands on image row 0 in both APIs: GL puts NDC y=-1 at the bottom
// and counts texture rows up from there, Vulkan puts NDC y=-1 at the top and
// counts image rows down from there. Opposite conventions that agree on row 0,
// so this is the same shader as the GL one with no flip inserted.
#version 450

layout(location = 0) out vec2 v_uv;

void main() {
    vec2 p = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
    v_uv        = p;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
