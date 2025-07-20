#ifndef NEWCORE_RENDERER_H
#define NEWCORE_RENDERER_H

#include <stdint.h>
#include <stdbool.h>

// Renderer state structure for newcore
typedef struct {
    uint8_t status;
    // TODO: Add more renderer state fields as needed
} NcRenderer;

// Initialize renderer
void nc_renderer_init(NcRenderer* renderer);

#endif // NEWCORE_RENDERER_H 