#include "../include/cdimage.h"
#include <stdio.h>

void cd_image_load(const char* filename) {
    // @pcs: Stub implementation for CD image loading
    if (filename) {
        printf("[CDIMAGE] Loading CD image: %s (stub)\n", filename);
    } else {
        printf("[CDIMAGE] No CD image specified (stub)\n");
    }
    // TODO: Implement actual CD image loading
}

void cd_image_close(void) {
    // @pcs: Stub implementation for CD image shutdown
    printf("[CDIMAGE] Closing CD image (stub)\n");
    // TODO: Free CD image resources
} 