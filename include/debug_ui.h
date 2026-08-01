/* SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 ZioZoni95
 *
 * Part of ZoniStation One, a PlayStation 1 emulator.
 * See LICENSE for the full licence text and THIRD-PARTY.md for the
 * components of this project that have other authors.
 */
#ifndef DEBUG_UI_H
#define DEBUG_UI_H

#include <SDL2/SDL.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void debug_ui_init(SDL_Window* window, SDL_GLContext gl_context);
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

#ifdef __cplusplus
}
#endif

#endif // DEBUG_UI_H
