#ifndef NEWCORE_RENDERER_PLUGIN_H
#define NEWCORE_RENDERER_PLUGIN_H

#include <stdint.h>

// Renderer plugin API for newcore
// All renderer plugins must implement this interface

typedef struct NcRendererPlugin {
    int  (*init)(void);
    void (*shutdown)(void);
    void (*render_frame)(const uint16_t* vram, int width, int height);
    // Add more functions as needed (resize, input, etc.)
} NcRendererPlugin;

// The plugin must export this function
NcRendererPlugin* get_renderer_plugin(void);

#endif // NEWCORE_RENDERER_PLUGIN_H 