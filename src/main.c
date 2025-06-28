/**
 * main.c
 * Entry point for the ZoniStation One Emulator.
 * Initializes all subsystems (SDL, OpenGL, Core Components), runs the main
 * emulation loop, and handles cleanup.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <sys/stat.h> // For file size checking
#include <unistd.h>   // For access(), rename()

// --- Graphics/Windowing Includes ---
// Uses SDL2 for window creation, event handling, and OpenGL context management.
// Uses GLEW for loading modern OpenGL extensions.
#include <SDL2/SDL.h>
#define GLEW_STATIC
#include <GL/glew.h>

// --- Emulator Core Components ---
#include "cpu.h"
#include "interconnect.h"
#include "bios.h"
#include "ram.h"
#include "renderer.h"
#include "cdrom.h" // <<< UPDATED: Added include for the CD-ROM component
#include "log.h"




int main(int argc, char *argv[]) {
    // --- Argument Parsing ---
    // Usage: ./myps1_emu [--debug|--quiet] <BIOS_PATH>
    const char* bios_path = NULL;
    int log_level = LOG_LEVEL_INFO;
    bool show_help = false;
    bool log_single_file = false;
    int log_rate_limit_n = 0;

    for (int i = 1; i < argc; ++i) {
        if (strncmp(argv[i], "--log-rate-limit=", 17) == 0) {
            log_rate_limit_n = atoi(argv[i] + 17);
        } else if (strcmp(argv[i], "--log-single-file") == 0) {
            log_single_file = true;
        } else if (strcmp(argv[i], "--debug") == 0) {
            log_level = LOG_LEVEL_DEBUG;
        } else if (strcmp(argv[i], "--quiet") == 0) {
            log_level = LOG_LEVEL_WARN;
        } else if (strcmp(argv[i], "--trace") == 0) {
            log_level = LOG_LEVEL_TRACE;
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            show_help = true;
        } else if (!bios_path) {
            bios_path = argv[i];
        } else {
            printf("Unknown option: %s\n", argv[i]);
            printf("Usage: %s [--debug|--trace|--quiet|--log-rate-limit=N|--log-single-file] <BIOS_PATH>\n", argv[0]);
            return 1;
        }
    }
    if (show_help) {
        printf("Usage: %s [--debug|--trace|--quiet|--log-rate-limit=N|--log-single-file] <BIOS_PATH>\n", argv[0]);
        printf("  --debug            Set log level to DEBUG (verbose output)\n");
        printf("  --trace            Set log level to TRACE (ultra-verbose, per-instruction/cycle)\n");
        printf("  --quiet            Set log level to WARN (minimal output)\n");
        printf("  --log-rate-limit=N Only log the first N debug/trace messages per component, then every Nth after that\n");
        printf("  --log-single-file  Log everything to emulator_log.txt (disables per-component logs in logs/)\n");
        printf("                    (Default: per-component logs in logs/ directory)\n");
        printf("  --help             Show this help message\n");
        printf("  <BIOS_PATH>        Path to PS1 BIOS image (default: roms/SCPH1001.BIN)\n");
        return 0;
    }
    if (log_rate_limit_n > 0) {
        log_set_rate_limit(1, log_rate_limit_n);
    }
    if (!bios_path) {
        bios_path = "roms/SCPH1001.BIN";
    }
    log_set_level(log_level);
    LOG_INFO("Emulator started");

    // --- File Logging Setup ---
    // Log file rotation: if emulator_log.txt > 50MB, move to emulator_log.old.txt
    if (log_single_file) {
        const char* log_filename = "emulator_log.txt";
        struct stat st;
        if (stat(log_filename, &st) == 0 && st.st_size > 50 * 1024 * 1024) {
            unlink("emulator_log.old.txt");
            rename(log_filename, "emulator_log.old.txt");
        }
        FILE *log_file = freopen(log_filename, "w", stdout);
        if (log_file == NULL) {
            perror("Failed to open log file for stdout");
            return 1;
        }
        freopen(log_filename, "a", stderr);
        setbuf(stdout, NULL);
        setbuf(stderr, NULL);
        LOG_INFO("--- Log Started ---");
    }

    // --- Configuration ---
    // Define a number of CPU cycles to run per frame. This helps pace the emulation.
    // This value might need tuning for performance vs. accuracy.
    const uint32_t cycles_per_frame = 33868800 / 60; // PSX CPU speed / NTSC refresh rate

    LOG_INFO("--- ZoniStation One Emulator ---");
    LOG_INFO("Attempting to load BIOS from: %s", bios_path);

    // --- SDL & OpenGL Initialization ---
    LOG_INFO("Initializing SDL Video...");
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        LOG_ERROR("SDL_Init Error: %s", SDL_GetError());
        return 1;
    }

    // Set OpenGL context attributes for a modern OpenGL 3.3 Core Profile.
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);

    LOG_INFO("Creating SDL Window (1024x512, OpenGL)...");
    SDL_Window* window = SDL_CreateWindow(
        "ZoniStation One",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        1024, 512, // Native PSX VRAM resolution
        SDL_WINDOW_OPENGL
    );
    if (!window) {
        LOG_ERROR("SDL_CreateWindow Error: %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    LOG_INFO("Creating OpenGL Context...");
    SDL_GLContext gl_context = SDL_GL_CreateContext(window);
    if (!gl_context) {
        LOG_ERROR("SDL_GL_CreateContext Error: %s", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    // Initialize GLEW *after* the OpenGL context is created.
    LOG_INFO("Initializing GLEW...");
    glewExperimental = GL_TRUE;
    GLenum glewError = glewInit();
    if (glewError != GLEW_OK) {
        LOG_ERROR("Error initializing GLEW! %s", glewGetErrorString(glewError));
        SDL_GL_DeleteContext(gl_context);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    LOG_INFO("GLEW Initialized. OpenGL Version: %s", glGetString(GL_VERSION));
    check_gl_error("After GLEW Init");

    // --- Emulator Component Initialization ---
    LOG_INFO("Initializing Emulator Components...");

    Bios bios_data;
    Ram ram_memory;
    Interconnect interconnect_state;
    Cpu cpu_state;

    // 1. Initialize RAM
    LOG_INFO("  Initializing RAM...");
    ram_init(&ram_memory);

    // 2. Load BIOS
    LOG_INFO("  Loading BIOS...");
    if (!bios_load(&bios_data, bios_path)) {
        // Cleanup on failure
        SDL_GL_DeleteContext(gl_context);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    // 3. Initialize Interconnect (connects all hardware components)
    LOG_INFO("  Initializing Interconnect...");
    interconnect_init(&interconnect_state, &bios_data, &ram_memory);

    // 4. Initialize the Renderer (using the instance inside the GPU)
    LOG_INFO("  Initializing Renderer...");
    if (!renderer_init(&interconnect_state.gpu.renderer)) {
        LOG_ERROR("Failed to initialize renderer!");
        // Cleanup on failure
        SDL_GL_DeleteContext(gl_context);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    
    // 5. Load a game disc into the CD-ROM drive
    // NOTE: Replace "path/to/your/game.bin" with an actual game image.
    // If no disc is loaded, the emulator will just run the BIOS.
    if (!cdrom_load_disc(&interconnect_state.cdrom, "games/Crassh Bandicoot.bin")) {
        LOG_INFO("Warning: Could not load game disc. Running BIOS only.");
    }


    // 6. Initialize CPU (pass it the fully connected interconnect)
    LOG_INFO("  Initializing CPU...");
    cpu_init(&cpu_state, &interconnect_state);

    LOG_INFO("All Emulator Components Initialized.");

    // --- Main Emulation Loop ---
    LOG_INFO("Starting Emulation Loop...");
    bool should_quit = false;
    SDL_Event event;
    uint64_t total_cycles = 0;

    while (!should_quit) {
        // --- Handle Input/Window Events ---
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                LOG_INFO("SDL_QUIT event received.");
                should_quit = true;
            } else if (event.type == SDL_KEYDOWN) {
                if (event.key.keysym.sym == SDLK_ESCAPE) {
                    LOG_INFO("Escape key pressed. Quitting.");
                    should_quit = true;
                }
            }
        }

        // --- Run Emulation for One Frame ---
        
        // Execute a frame's worth of CPU cycles.
        // cpu_run_cycles is a hypothetical function. If you have cpu_run_next_instruction,
        // you would loop that call `cycles_per_frame` times.
        for (uint32_t i = 0; i < cycles_per_frame; ++i) {
             cpu_run_next_instruction(&cpu_state);
             // Step timers with each CPU cycle for proper timing
             timers_step(&interconnect_state.timers_state, 1);
        }

        // <<< UPDATED: Step the CD-ROM scheduler >>>
        // This is critical for handling timed CD-ROM commands. It must be called
        // regularly, passing the number of CPU cycles that have just run.
        // NOTE: This is required for CDROM IRQ2 (CDROM) to be triggered and for command completion.
        cdrom_step(&interconnect_state.cdrom, cycles_per_frame);

        // Check if BIOS needs boot helper for interrupt configuration
        interconnect_check_bios_boot(&interconnect_state);

        // Trigger VBlank IRQ0 at the end of each frame
        gpu_trigger_vblank_irq(&interconnect_state.gpu);

        total_cycles += cycles_per_frame;

        // --- Render and Display Frame ---
        // The GPU emulation sends drawing commands to the renderer during CPU execution.
        // Here, we just need to swap the buffers to show the result on screen.
        SDL_GL_SwapWindow(window);
        check_gl_error("After SwapWindow");
    }

    // --- Cleanup ---
    LOG_INFO("Emulation loop finished. Cleaning up...");

    renderer_destroy(&interconnect_state.gpu.renderer);
    LOG_INFO("Destroying SDL GL Context and Window...");
    SDL_GL_DeleteContext(gl_context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    LOG_INFO("SDL Quit.");

    LOG_INFO("--- ZoniStation One Emulator Finished ---");
    LOG_INFO("Emulator stopped");
    return 0;
}