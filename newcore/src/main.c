#include "../include/config.h"
#include "../include/input.h"
#include "../include/menu.h"
#include "../include/cdimage.h"
#include "../include/savestate.h"
#include "../include/emulator.h"
#include <stdio.h>
#include <string.h>

int main(int argc, char *argv[]) {
    // --- Pre-initialize core (config, logging, etc.) ---
    emu_core_preinit();

    // --- Frontend initialization ---
    input_init();
    // TODO: platform_init();
    menu_init();

    // --- Core initialization ---
    if (emu_core_init() != 0) {
        printf("[MAIN] Core init failed\n");
        return 1;
    }

    // --- Plugin loading (stub) ---
    printf("[MAIN] Loading plugins (stub)\n");
    // TODO: LoadPlugins();

    // --- CD/file loading (stub) ---
    cd_image_load(NULL);

    // --- Savestate loading (stub) ---
    savestate_load(NULL);

    // --- Ready to run? (stub logic) ---
    int ready_to_go = 1; // For now, always ready

    if (ready_to_go) {
        // --- Prepare emulation (stub) ---
        printf("[MAIN] Preparing emulation (stub)\n");
        // TODO: menu_prepare_emu();
        // --- Main emulation loop ---
        while (!emu_core_should_quit()) {
            emulator_frame();
            // TODO: Check for frontend actions, input, etc.
            // For demo, break after 10 frames
            static int frame_count = 0;
            if (++frame_count >= 10) emu_core_ask_exit();
        }
    } else {
        // --- Menu loop (stub) ---
        printf("[MAIN] Entering menu loop (stub)\n");
        // TODO: menu_loop();
    }

    // --- Shutdown/Cleanup ---
    printf("[MAIN] Shutting down (stub)\n");
    // TODO: ClosePlugins();
    input_close();
    menu_close();
    cd_image_close();
    savestate_save("autosave.sav");
    printf("[SHUTDOWN] Freeing emulator resources (stub)\n");
    return 0;
} 