#ifndef DEBUG_UI_H
#define DEBUG_UI_H

#include <stdbool.h>
#include <SDL2/SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

void debug_ui_init(SDL_Window* window, SDL_GLContext ctx);
void debug_ui_shutdown(void);
void debug_ui_process_event(SDL_Event* event);
void debug_ui_begin_frame(void);
void debug_ui_render(void);
bool debug_ui_wants_keyboard(void);

// Called from log sink callback
void debug_ui_push_log(int category, int level, const char* msg);

#ifdef __cplusplus
}
#endif

#endif // DEBUG_UI_H
