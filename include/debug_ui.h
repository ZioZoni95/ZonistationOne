/* SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 ZioZoni95
 *
 * Part of ZoniStation One, a PlayStation 1 emulator.
 * See LICENSE for the full licence text and THIRD-PARTY.md for the
 * components of this project that have other authors.
 */
#ifndef DEBUG_UI_H
#define DEBUG_UI_H

#include <SDL3/SDL.h>
#include <stdbool.h>
#include <stddef.h>

#include "gpu_backend.h"

#ifdef __cplusplus
extern "C" {
#endif

void debug_ui_init(SDL_Window* window, void* gl_context, int backend);

/* The two ImGui backend halves on their own, for a hot renderer switch: the
 * platform half is bound to the SDL window and the renderer half to the
 * graphics device, and a switch destroys both. The ImGui *context* — fonts,
 * imgui.ini, the docking layout, the pinned watches — is created by
 * debug_ui_init() and must survive, so it is not touched by either of these. */
void debug_ui_backend_init(SDL_Window* window, void* gl_context, int backend);
void debug_ui_backend_shutdown(void);
void debug_ui_process_event(SDL_Event* event);
void debug_ui_render(void* cpu_ptr, void* interconnect_ptr);
void debug_ui_shutdown(void);
bool debug_ui_step_requested(void); // Consumed once per call (edge-triggered)
bool debug_ui_vram_viewer_open(void); // Snapshotting VRAM for it costs 2 MB/frame

/* Machine bar identity — set once after args are parsed and the disc loaded.
 * Strings are copied, so the caller's buffers need not outlive the call. */
void debug_ui_set_machine_info(const char* bios_name, const char* disc_name);

/* Live vitals for the machine bar. Fed once per frame from the main loop using
 * counters it already has (frame budget vs. measured wall time, SPU ring depth).
 * Cheap by construction: two perf-counter reads, no logging. */
void debug_ui_set_vitals(double frame_ms, double budget_ms,
                         int audio_queue, int audio_target, double drift_pct);

/* --- gameplay shell handshake ---------------------------------------------
 * The window has two shells: the debug workspace and a gameplay shell that
 * shows the screen and an overlay. The shell does not own the machine, so the
 * host loop asks it what the player pressed and then does the work itself.
 *
 * debug_ui_escape_pressed() returns true when the gameplay shell consumed the
 * key (it opened or closed its menu). False means Escape still means quit. */
bool debug_ui_escape_pressed(void);
bool debug_ui_take_quit_request(void);
bool debug_ui_take_state_request(bool* out_save, char* path, size_t path_size);
void debug_ui_notify_state_result(bool save, bool ok, const char* path);

/* The renderer switch, same shape as the savestate handshake: the shell asks,
 * main.c owns the window and the device and carries it out, then says what
 * actually happened — which is not always what was asked for, since a backend
 * that fails to come up is rolled back to the previous one. */
bool debug_ui_take_gfx_request(GfxDeviceRequest* out);
void debug_ui_notify_gfx_result(int backend, int device_index, bool ok, const char* detail);

#ifdef __cplusplus
}
#endif

#endif // DEBUG_UI_H
