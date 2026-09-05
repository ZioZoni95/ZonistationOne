/* SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 ZioZoni95
 *
 * Part of ZoniStation One, a PlayStation 1 emulator.
 * See LICENSE for the full licence text and THIRD-PARTY.md for the
 * components of this project that have other authors.
 */

/* The renderer's public face: a dispatcher onto whichever backend is live.
 *
 * This file used to be the whole OpenGL renderer; that is now
 * src/gpu/renderer_gl.c, reached through the GfxBackend vtable. The ~120 call
 * sites in gpu.c, gpu_commands.c, debug_ui.cpp, savestate.c, lua_debug.c and
 * main.c are unchanged — they still call renderer_xxx(&gpu->renderer, ...) —
 * which is the point: the split is invisible above this line.
 *
 * Every entry point tolerates an unbound Renderer and does nothing. That is not
 * defensive habit, it is required: gpu_reset_state() pushes GP0/GP1 reset
 * values through four of these setters while the machine is being built, and a
 * backend that failed to initialise must leave a machine that still runs
 * headless rather than a null dereference.
 */

#include "renderer.h"
#include "gpu_backend.h"
#include "log.h"

#include <stddef.h>

/* The one live backend. renderer_get_vram_readback() takes no Renderer — it is
 * called from the Lua probe surface and the inspector, which hold neither — so
 * the dispatcher needs a way to reach the backend without one. */
static const GfxBackend* s_active = NULL;

const GfxBackend* gfx_backend_get(GfxBackendType type) {
    switch (type) {
        case GFX_BACKEND_GL33:   return &gfx_backend_gl33;
#ifdef ENABLE_VULKAN
        case GFX_BACKEND_VULKAN: return &gfx_backend_vulkan;
#else
        case GFX_BACKEND_VULKAN: return NULL;
#endif
        default:                 return NULL;
    }
}

const char* gfx_backend_unavailable_reason(GfxBackendType type) {
    switch (type) {
        case GFX_BACKEND_GL33:   return NULL;
#ifdef ENABLE_VULKAN
        case GFX_BACKEND_VULKAN: return NULL;
#else
        case GFX_BACKEND_VULKAN: return "not compiled in — needs libvulkan-dev and glslang-tools at build time";
#endif
        default:                 return "unknown backend";
    }
}

bool renderer_select_backend(Renderer* renderer, GfxBackendType type) {
    if (!renderer) return false;
    const GfxBackend* be = gfx_backend_get(type);
    if (!be) {
        LOG_RENDERER_ERROR("[RENDERER] Backend %d unavailable: %s",
                           (int)type, gfx_backend_unavailable_reason(type));
        return false;
    }
    renderer->vt   = be;
    renderer->impl = NULL;   /* bound by renderer_init() */
    s_active       = be;
    LOG_RENDERER_INFO("[RENDERER] Backend selected: %s", be->name);
    return true;
}

const GfxBackend* renderer_backend(const Renderer* renderer) {
    return renderer ? renderer->vt : NULL;
}

bool renderer_init_ex(Renderer* renderer, SDL_Window* window, const GfxDeviceRequest* req) {
    if (!renderer) return false;
    /* Nobody called renderer_select_backend(): take the one that is always
     * there rather than failing, so an older caller still boots. */
    if (!renderer->vt && !renderer_select_backend(renderer, GFX_BACKEND_GL33))
        return false;
    if (!renderer->vt->init(&renderer->impl, window, req)) {
        LOG_RENDERER_ERROR("[RENDERER] %s backend failed to initialise", renderer->vt->name);
        renderer->impl = NULL;
        return false;
    }
    return true;
}

bool renderer_init(Renderer* renderer, SDL_Window* window) {
    return renderer_init_ex(renderer, window, NULL);
}

int renderer_enumerate_devices(GfxBackendType type, GfxDeviceInfo* out, int max) {
    const GfxBackend* be = gfx_backend_get(type);
    if (!be || !be->enumerate_devices || !out || max <= 0) return 0;
    return be->enumerate_devices(out, max);
}

void renderer_destroy(Renderer* renderer) {
    if (!renderer || !renderer->vt || !renderer->impl) return;
    renderer->vt->destroy(renderer->impl);
    renderer->impl = NULL;
}

/* --- Forwarding.
 *
 * `impl` is NULL between renderer_select_backend() and renderer_init(), which is
 * exactly the window gpu_reset_state() runs in. The backend's state is a
 * file-static that exists before its init runs, so the guard below is on `vt`
 * for the setters that must work in that window, and on `impl` for everything
 * that needs a live device. --- */

#define VT(r)      ((r) && (r)->vt)
#define LIVE(r)    (VT(r) && (r)->impl)

/* The setters gpu_reset_state() calls before init. The backend resolves a NULL
 * impl to its own static state, so these land where init will find them. */
static GfxImpl early_impl(Renderer* r) { return r->impl; }

void renderer_start_gpu_thread(Renderer* renderer, SDL_Window* window, void* native_ctx) {
    if (!LIVE(renderer)) return;
    renderer->vt->start_thread(renderer->impl, window, native_ctx);
}
void renderer_stop_gpu_thread(Renderer* renderer) {
    if (!LIVE(renderer)) return;
    renderer->vt->stop_thread(renderer->impl);
}
void renderer_submit_frame(Renderer* renderer, void* imgui_draw_data) {
    if (!LIVE(renderer)) return;
    renderer->vt->submit_frame(renderer->impl, imgui_draw_data);
}
void renderer_wait_frame_done(Renderer* renderer) {
    if (!LIVE(renderer)) return;
    renderer->vt->wait_frame_done(renderer->impl);
}

void renderer_push_triangle(Renderer* renderer, RendererPosition pos[3], RendererColor col[3],
                            RendererTexCoord tex[3], uint16_t clut, uint16_t tpage) {
    if (!LIVE(renderer)) return;
    renderer->vt->push_triangle(renderer->impl, pos, col, tex, clut, tpage);
}
void renderer_push_quad(Renderer* renderer, RendererPosition pos[4], RendererColor col[4],
                        RendererTexCoord tex[4], uint16_t clut, uint16_t tpage) {
    if (!LIVE(renderer)) return;
    renderer->vt->push_quad(renderer->impl, pos, col, tex, clut, tpage);
}
void renderer_push_line(Renderer* renderer, RendererPosition pos[2], RendererColor col[2]) {
    if (!LIVE(renderer)) return;
    renderer->vt->push_line(renderer->impl, pos, col);
}
void renderer_draw(Renderer* renderer) {
    if (!LIVE(renderer)) return;
    renderer->vt->draw(renderer->impl);
}
void renderer_display(Renderer* renderer) {
    if (!LIVE(renderer)) return;
    renderer->vt->display(renderer->impl);
}
void renderer_blit_vram(Renderer* renderer, uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
    if (!LIVE(renderer)) return;
    renderer->vt->blit_vram(renderer->impl, x, y, w, h);
}

void renderer_set_texture_mode(Renderer* renderer, bool enabled) {
    if (!VT(renderer)) return;
    renderer->vt->set_texture_mode(early_impl(renderer), enabled);
}
void renderer_set_raw_texture_mode(Renderer* renderer, bool enabled) {
    if (!VT(renderer)) return;
    renderer->vt->set_raw_texture_mode(early_impl(renderer), enabled);
}
void renderer_set_screen_scale(Renderer* renderer, uint16_t width, uint16_t height) {
    if (!VT(renderer)) return;
    renderer->vt->set_screen_scale(early_impl(renderer), width, height);
}
void renderer_set_texture_window(Renderer* renderer, uint8_t mask_x, uint8_t mask_y,
                                 uint8_t offset_x, uint8_t offset_y) {
    if (!VT(renderer)) return;
    renderer->vt->set_texture_window(early_impl(renderer), mask_x, mask_y, offset_x, offset_y);
}
void renderer_set_draw_offset(Renderer* renderer, int16_t x, int16_t y) {
    if (!VT(renderer)) return;
    renderer->vt->set_draw_offset(early_impl(renderer), x, y);
}
void renderer_set_drawing_area(Renderer* renderer, uint16_t left, uint16_t top,
                               uint16_t right, uint16_t bottom) {
    if (!VT(renderer)) return;
    renderer->vt->set_drawing_area(early_impl(renderer), left, top, right, bottom);
}
void renderer_set_semi_trans_mode(Renderer* renderer, bool enabled, uint8_t mode) {
    if (!VT(renderer)) return;
    renderer->vt->set_semi_trans_mode(early_impl(renderer), enabled, mode);
}
void renderer_set_dither_mode(Renderer* renderer, bool enabled) {
    if (!VT(renderer)) return;
    renderer->vt->set_dither_mode(early_impl(renderer), enabled);
}
void renderer_set_mask_mode(Renderer* renderer, bool enabled) {
    if (!VT(renderer)) return;
    renderer->vt->set_mask_mode(early_impl(renderer), enabled);
}
void renderer_set_mask_test(Renderer* renderer, bool enabled) {
    if (!VT(renderer)) return;
    renderer->vt->set_mask_test(early_impl(renderer), enabled);
}

void renderer_upload_vram(Renderer* renderer, const uint16_t* vram_data,
                          uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
    if (!LIVE(renderer)) return;
    renderer->vt->upload_vram(renderer->impl, vram_data, x, y, w, h);
}
void renderer_upload_vram_rect(Renderer* renderer, const uint16_t* vram_data,
                               uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
    if (!LIVE(renderer)) return;
    renderer->vt->upload_vram_rect(renderer->impl, vram_data, x, y, w, h);
}
bool renderer_read_vram_rect(Renderer* renderer, uint16_t* vram,
                             uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
    if (!LIVE(renderer)) return false;
    return renderer->vt->read_vram_rect(renderer->impl, vram, x, y, w, h);
}
void renderer_request_vram_readback(Renderer* renderer) {
    if (!LIVE(renderer)) return;
    renderer->vt->request_vram_readback(renderer->impl);
}
const uint16_t* renderer_get_vram_readback(uint32_t* seq_out) {
    if (!s_active) return NULL;
    return s_active->get_vram_readback(seq_out);
}

void renderer_set_display_region(Renderer* renderer, uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
    if (!VT(renderer)) return;
    renderer->vt->set_display_region(early_impl(renderer), x, y, w, h);
}
void renderer_set_display_depth24(Renderer* renderer, bool depth24) {
    if (!VT(renderer)) return;
    renderer->vt->set_display_depth24(early_impl(renderer), depth24);
}
void renderer_set_display_blank(Renderer* renderer, bool blank) {
    if (!VT(renderer)) return;
    renderer->vt->set_display_blank(early_impl(renderer), blank);
}

void renderer_set_vram_view_params(Renderer* renderer, const VramViewParams* p) {
    if (!LIVE(renderer)) return;
    renderer->vt->set_vram_view_params(renderer->impl, p);
}
void renderer_update_vram_viewer(Renderer* renderer, const uint8_t* vram_bytes) {
    if (!LIVE(renderer)) return;
    renderer->vt->update_vram_viewer(renderer->impl, vram_bytes);
}
void renderer_get_pool_stats(Renderer* renderer, uint32_t* used, uint32_t* peak,
                             uint32_t* updates, uint32_t* skips) {
    if (!LIVE(renderer)) return;
    renderer->vt->get_pool_stats(renderer->impl, used, peak, updates, skips);
}

GfxTexHandle renderer_get_display_texture(Renderer* renderer) {
    if (!LIVE(renderer)) return 0;
    return renderer->vt->get_display_texture(renderer->impl);
}
GfxTexHandle renderer_get_scanout_texture(Renderer* renderer) {
    if (!LIVE(renderer)) return 0;
    return renderer->vt->get_scanout_texture(renderer->impl);
}
GfxTexHandle renderer_get_vram_viewer_texture(Renderer* renderer) {
    if (!LIVE(renderer)) return 0;
    return renderer->vt->get_vram_viewer_texture(renderer->impl);
}
