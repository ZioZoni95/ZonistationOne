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

#ifdef __cplusplus
}
#endif

#endif // DEBUG_UI_H
