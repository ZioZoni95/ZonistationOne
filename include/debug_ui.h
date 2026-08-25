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

#ifdef __cplusplus
extern "C" {
#endif

void debug_ui_init(SDL_Window* window, void* gl_context);
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

#ifdef __cplusplus
}
#endif

#endif // DEBUG_UI_H
