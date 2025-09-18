/*
 * ZonistationOne - PlayStation One Emulator
 * Main Entry Point
 * 
 * Architecture inspired by PCSX Redux
 * Author: ZioZoni95
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include "core/system.h"
#include "core/emulator.h"
#include "core/logger.h"

static psx_emulator_t *g_emulator = NULL;
static int g_running = 1;

void signal_handler(int signal) {
    log_info("Received signal %d, shutting down...", signal);
    g_running = 0;
}

void setup_signal_handlers(void) {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
#ifndef _WIN32
    signal(SIGQUIT, signal_handler);
#endif
}

void print_usage(const char *program) {
    printf("ZonistationOne - PlayStation One Emulator\n\n");
    printf("Usage: %s [options] [rom_file]\n\n", program);
    printf("Options:\n");
    printf("  -h, --help        Show this help message\n");
    printf("  -b, --bios <file> Specify BIOS file\n");
    printf("  -v, --verbose     Enable verbose logging\n");
    printf("  -d, --debug       Enable debug mode\n");
    printf("  --version         Show version information\n");
}

int main(int argc, char *argv[]) {
    printf("ZonistationOne v1.0.0 - PlayStation One Emulator\n");
    printf("Based on PCSX Redux architecture\n\n");

    setup_signal_handlers();

    // Parse command line arguments
    const char *bios_file = NULL;
    const char *rom_file = NULL;
    int verbose = 0;
    int debug = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "--version") == 0) {
            printf("Version: 1.0.0\n");
            return 0;
        } else if (strcmp(argv[i], "-b") == 0 || strcmp(argv[i], "--bios") == 0) {
            if (i + 1 < argc) {
                bios_file = argv[++i];
            } else {
                fprintf(stderr, "Error: BIOS file not specified\n");
                return 1;
            }
        } else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
            verbose = 1;
        } else if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--debug") == 0) {
            debug = 1;
        } else if (argv[i][0] != '-') {
            rom_file = argv[i];
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            return 1;
        }
    }

    // Initialize logging
    logger_init(verbose ? LOG_DEBUG : LOG_INFO);

    // Create and initialize emulator
    g_emulator = emulator_create();
    if (!g_emulator) {
        log_error("Failed to create emulator instance");
        return 1;
    }

    // Configure emulator
    if (bios_file) {
        if (emulator_load_bios(g_emulator, bios_file) != 0) {
            log_error("Failed to load BIOS: %s", bios_file);
            emulator_destroy(g_emulator);
            return 1;
        }
    }

    if (debug) {
        emulator_set_debug_mode(g_emulator, 1);
    }

    // Initialize emulator
    if (emulator_init(g_emulator) != 0) {
        log_error("Failed to initialize emulator");
        emulator_destroy(g_emulator);
        return 1;
    }

    // Load ROM if specified
    if (rom_file) {
        if (emulator_load_rom(g_emulator, rom_file) != 0) {
            log_error("Failed to load ROM: %s", rom_file);
            emulator_destroy(g_emulator);
            return 1;
        }
    }

    log_info("Starting emulator main loop...");

    // Main emulation loop
    while (g_running && emulator_is_running(g_emulator)) {
        if (emulator_step(g_emulator) != 0) {
            log_error("Emulator step failed");
            break;
        }
    }

    log_info("Shutting down emulator...");

    // Clean up
    emulator_shutdown(g_emulator);
    emulator_destroy(g_emulator);
    logger_shutdown();

    printf("ZonistationOne shutdown complete.\n");
    return 0;
}