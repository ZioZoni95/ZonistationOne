#include "../include/emulator.h"
#include "../include/log.h"
#include <stdio.h>
#include <string.h>

static int g_emu_want_quit = 0;

// @pcs: Pre-initialize emulator core (config, logging, etc.)
int emu_core_preinit(void) {
    printf("[EMU] Core preinit (stub)\n");
    // TODO: Set up config, logging, etc.
    return 0;
}

// @pcs: Initialize emulator core (memory, bios, etc.)
int emu_core_init(void) {
    printf("[EMU] Core init (stub)\n");
    // TODO: Initialize memory, BIOS, etc.
    return 0;
}

// @pcs: Request emulator exit
void emu_core_ask_exit(void) {
    printf("[EMU] Core ask exit (stub)\n");
    g_emu_want_quit = 1;
}

// @pcs: Run a single emulation frame (stub)
void emulator_frame(void) {
    printf("[EMU] Emulation frame (stub)\n");
    // TODO: Step CPU, DMA, events, input, render, etc.
}

// @pcs: Check if emulator wants to quit
int emu_core_should_quit(void) {
    return g_emu_want_quit;
} 