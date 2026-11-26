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
#include <stdint.h>

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

/* Who has the input. debug_ui_keyboard_captured() is true while a text field,
 * the quick menu or a confirmation holds the keyboard, and the host must not
 * act on game or TTY keys. debug_ui_filter_pad() takes the pad word the host
 * just read (active low) and returns what the SIO should see: the same word,
 * or 0xFFFF while the interface is using the pad to navigate. */
bool     debug_ui_keyboard_captured(void);
uint16_t debug_ui_filter_pad(uint16_t buttons);

/* F5 / F8. The interface decides whether the key is a savestate request in
 * the shell that is up (and which slot it means) and parks it for
 * debug_ui_take_state_request(); false means the key was something else. */
bool debug_ui_quick_state(bool save, bool shift);
bool debug_ui_take_fullscreen_request(void);
bool debug_ui_take_quit_request(void);
bool debug_ui_take_state_request(bool* out_save, char* path, size_t path_size);
void debug_ui_notify_state_result(bool save, bool ok, const char* path);

/* The renderer switch, same shape as the savestate handshake: the shell asks,
 * main.c owns the window and the device and carries it out, then says what
 * actually happened — which is not always what was asked for, since a backend
 * that fails to come up is rolled back to the previous one. */
bool debug_ui_take_gfx_request(GfxDeviceRequest* out);
void debug_ui_notify_gfx_result(int backend, int device_index, bool ok, const char* detail);

/* --- start screen ----------------------------------------------------------
 * Shown when the binary is started without a BIOS on the command line — a
 * double-click, a shortcut. It runs before the machine exists: main.c brings up
 * the window and the renderer, then calls debug_ui_launcher_render() once per
 * frame until a start request comes back. The screen lists the BIOS images and
 * disc images it finds in roms/ and games/ (beside the working directory and
 * beside the executable), offers the system file dialog for anything else, and
 * remembers the last choice in zs1_launcher.txt.
 *
 * A start request carries the BIOS path and the disc path ("" for no disc).
 * main.c loads the BIOS; if that fails it calls debug_ui_launcher_failed() and
 * keeps the screen up, otherwise debug_ui_launcher_done(), which saves the
 * choice and hands the window to the gameplay shell. A quit from the screen
 * comes back through debug_ui_take_quit_request(), as from the quick menu.
 *
 * With a BIOS on the command line none of this runs, so scripts, the cluster
 * and make hwtest start exactly as before. */
void debug_ui_launcher_render(void);
bool debug_ui_launcher_take_start(char* bios, size_t bios_size, char* game, size_t game_size);
void debug_ui_launcher_failed(const char* message);
void debug_ui_launcher_done(void);

#ifdef __cplusplus
}
#endif

#endif // DEBUG_UI_H
