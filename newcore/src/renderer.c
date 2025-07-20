#include "../include/renderer_plugin.h"
#include "../include/log.h"
#include <stdio.h>

static int plugin_init(void) {
    NC_LOGI("Renderer plugin: init");
    return 0;
}

static void plugin_shutdown(void) {
    NC_LOGI("Renderer plugin: shutdown");
}

static void plugin_render_frame(const uint16_t* vram, int width, int height) {
    (void)vram;
    (void)width;
    (void)height;
    NC_LOGI("Renderer plugin: render_frame (stub)");
}

static NcRendererPlugin plugin = {
    .init = plugin_init,
    .shutdown = plugin_shutdown,
    .render_frame = plugin_render_frame
};

NcRendererPlugin* get_renderer_plugin(void) {
    return &plugin;
} 