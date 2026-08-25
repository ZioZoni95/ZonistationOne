// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025-2026 ZioZoni95
//
// Shared push-constant block for the PS1 primitive rasteriser.
//
// GL passed each of these as a separate uniform and re-set them per batch. In
// Vulkan the whole set is one push-constant range visible to both stages, which
// is the cheapest way to carry per-batch state and removes the per-uniform
// location bookkeeping the GL backend keeps in its struct.
//
// 56 bytes; the Vulkan guaranteed minimum is 128.

layout(push_constant) uniform PS1Push {
    ivec2 offset;          // GP0(E5) drawing offset, added to every vertex
    vec2  screen_scale;    // half width/height, PSX coords -> NDC
    ivec4 tex_window;      // GP0(E2) pre-computed (and_x, and_y, or_x, or_y)
    int   use_texture;
    int   raw_texture;     // 1 = texture colour straight through, no modulation
    int   dither_enable;
    int   stp_mode;        // -1 off, 0 opaque pass (discard STP=1), 1 blend pass (discard STP=0)
    int   set_mask;        // GP0(E6).0 — force bit 15 on every pixel drawn
    int   mask_test;       // GP0(E6).1 — skip pixels whose destination bit 15 is set
} pc;
