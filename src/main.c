/**
 * main.c
 * Entry point for the ZoniStation One Emulator.
 * Initializes all subsystems (SDL, OpenGL, Core Components), runs the main
 * emulation loop, and handles cleanup.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
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
#include "event_scheduler.h" // <<< ADDED: Include for event scheduling
#include "controller.h" // <<< ADDED: Include for gamepad input


/*
 * Command Line Logging Options:
 *   --debug            Set log level to DEBUG (verbose output)
 *   --trace            Set log level to TRACE (ultra-verbose, per-instruction/cycle)
 *   --quiet            Set log level to WARN (minimal output)
 *   --log-rate-limit=N Only log the first N debug/trace messages per component, then every Nth after that
 *   --log-single-file  Log everything to emulator_log.txt (disables per-component logs in logs/)
 *
 * Examples:
 *   ./myps1_emu --debug
 *   ./myps1_emu --trace --log-rate-limit=1000
 *   ./myps1_emu --debug --log-single-file
 */

int main(int argc, char *argv[]) {
    // --- Argument Parsing ---
    // Usage: ./myps1_emu [options] <BIOS_PATH>
    const char* bios_path = NULL;
    int log_level = LOG_LEVEL_WARN;
    bool show_help = false;
    bool log_single_file = false;
    int log_rate_limit_n = 0;
    bool disable_irq_logs = false;
    bool disable_interconnect_logs = false;
    bool disable_dma_logs = false;
    bool bios_strings_mode = false;

    for (int i = 1; i < argc; ++i) {
        if (strncmp(argv[i], "--log-rate-limit=", 17) == 0) {
            log_rate_limit_n = atoi(argv[i] + 17);
        } else if (strcmp(argv[i], "--log-single-file") == 0) {
            log_single_file = true;
        } else if (strcmp(argv[i], "--debug") == 0) {
            log_level = LOG_LEVEL_DEBUG;
        } else if (strcmp(argv[i], "--quiet") == 0) {
            log_level = LOG_LEVEL_SILENT;
        } else if (strcmp(argv[i], "--trace") == 0) {
            log_level = LOG_LEVEL_TRACE;
        } else if (strcmp(argv[i], "--silent") == 0) {
            log_level = LOG_LEVEL_SILENT;
        } else if (strcmp(argv[i], "--no-irq-logs") == 0) {
            disable_irq_logs = true;
        } else if (strcmp(argv[i], "--no-interconnect-logs") == 0) {
            disable_interconnect_logs = true;
        } else if (strcmp(argv[i], "--no-dma-logs") == 0) {
            disable_dma_logs = true;
        } else if (strcmp(argv[i], "--bios-strings") == 0) {
            bios_strings_mode = true;
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            show_help = true;
        } else if (!bios_path) {
            bios_path = argv[i];
        } else {
            printf("Unknown option: %s\n", argv[i]);
            printf("Usage: %s [options] <BIOS_PATH>\n", argv[0]);
            printf("Use --help for full option list.\n");
            return 1;
        }
    }
    if (show_help) {
        printf("PS1 Emulator with PCSX ReARMed-style Component-Based Logging\n");
        printf("Usage: %s [options] <BIOS_PATH>\n\n", argv[0]);
        printf("Log Level Options:\n");
        printf("  --silent           No logging output\n");
        printf("  --quiet            Warnings and errors only\n");
        printf("  --debug            Verbose debug output (default: INFO)\n");
        printf("  --trace            Ultra-verbose trace output\n\n");
        printf("Component Control (PCSX ReARMed style):\n");
        printf("  --no-irq-logs      Disable IRQ/interrupt logging (reduces spam)\n");
        printf("  --no-interconnect-logs Disable I/O interconnect logging (reduces spam)\n");
        printf("  --no-dma-logs      Disable DMA transfer logging\n\n");
        printf("BIOS Debug:\n");
        printf("  --bios-strings     Dump all TCRF hidden string blocks at startup\n");
        printf("                     (PIO Shell, Control PAD, Std Libraries, CD debug)\n\n");
        printf("Output Control:\n");
        printf("  --log-rate-limit=N Rate limit: log first N msgs, then every Nth (default: auto)\n");
        printf("  --log-single-file  Log to emulator_log.txt instead of console\n\n");
        printf("Categories: SYSTEM, CPU, IRQ, DMA, GPU, CDROM, TIMER, BIOS, INTERCONNECT,\n");
        printf("           RENDERER, EVENT, GTE, VRAM, RAM, DEBUG\n\n");
        printf("Examples:\n");
        printf("  %s roms/SCPH1001.BIN                    # Default logging\n", argv[0]);
        printf("  %s --debug --no-irq-logs roms/SCPH1001.BIN  # Debug without IRQ spam\n", argv[0]);
        printf("  %s --quiet --log-single-file roms/SCPH1001.BIN # Minimal to file\n\n", argv[0]);
        printf("  --help, -h         Show this help message\n");
        printf("  <BIOS_PATH>        Path to PS1 BIOS image (default: roms/SCPH1001.BIN)\n");
        return 0;
    }
    // Initialize new logging system
    log_init();
    if (log_rate_limit_n > 0) {
        // Apply rate limiting to noisy categories
        log_set_rate_limit(LOG_CAT_IRQ, 5, log_rate_limit_n);
        log_set_rate_limit(LOG_CAT_INTERCONNECT, 10, log_rate_limit_n);
        log_set_rate_limit(LOG_CAT_DMA, 5, log_rate_limit_n);
    }
    
    // Apply component-specific disable options (PCSX ReARMed style)
    if (disable_irq_logs) {
        log_set_category_enabled(LOG_CAT_IRQ, false);
        LOG_SYSTEM_INFO("IRQ logging disabled");
    }
    if (disable_interconnect_logs) {
        log_set_category_enabled(LOG_CAT_INTERCONNECT, false);
        LOG_SYSTEM_INFO("Interconnect logging disabled");
    }
    if (disable_dma_logs) {
        log_set_category_enabled(LOG_CAT_DMA, false);
        LOG_SYSTEM_INFO("DMA logging disabled");
    }
    if (!bios_path) {
        bios_path = "roms/SCPH1001.BIN";
    }
    log_set_level(log_level);
    LOG_SYSTEM_WARN("Emulator started");

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
                 LOG_SYSTEM_INFO("--- Log Started ---");
    }

    // --- Configuration ---
    // Define a number of CPU cycles to run per frame. This helps pace the emulation.
    // This value might need tuning for performance vs. accuracy.
    const uint32_t cycles_per_frame = 33868800 / 60; // PSX CPU speed / NTSC refresh rate

         LOG_SYSTEM_INFO("--- ZoniStation One Emulator ---");
     LOG_SYSTEM_INFO("Attempting to load BIOS from: %s", bios_path);

    // --- SDL & OpenGL Initialization ---
         LOG_SYSTEM_INFO("Initializing SDL Video...");
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
                 LOG_SYSTEM_ERROR("SDL_Init Error: %s", SDL_GetError());
        return 1;
    }

    // Set OpenGL context attributes for a modern OpenGL 3.3 Core Profile.
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);

         LOG_SYSTEM_INFO("Creating SDL Window (1024x512, OpenGL)...");
    SDL_Window* window = SDL_CreateWindow(
        "ZoniStation One",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        1024, 512, // Full VRAM size for black border (classic PSX look)
        SDL_WINDOW_OPENGL
    );
    if (!window) {
                 LOG_SYSTEM_ERROR("SDL_CreateWindow Error: %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }

         LOG_SYSTEM_INFO("Creating OpenGL Context...");
    SDL_GLContext gl_context = SDL_GL_CreateContext(window);
    if (!gl_context) {
                 LOG_SYSTEM_ERROR("SDL_GL_CreateContext Error: %s", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    // Initialize GLEW *after* the OpenGL context is created.
    LOG_SYSTEM_INFO("Initializing GLEW...");
    glewExperimental = GL_TRUE;
    GLenum glewError = glewInit();
    if (glewError != GLEW_OK) {
        LOG_SYSTEM_ERROR("Error initializing GLEW! %s", glewGetErrorString(glewError));
        SDL_GL_DeleteContext(gl_context);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    LOG_SYSTEM_INFO("GLEW Initialized. OpenGL Version: %s", glGetString(GL_VERSION));
    check_gl_error("After GLEW Init");

    // --- Emulator Component Initialization ---
    LOG_SYSTEM_WARN("Initializing Emulator Components...");

    Bios bios_data;
    Ram ram_memory;
    Interconnect interconnect_state;
    Cpu cpu_state;
    Controller gamepad;  // <<< ADDED: Game controller state

    // 1. Initialize RAM
    LOG_SYSTEM_DEBUG("  Initializing RAM...");
    ram_init(&ram_memory);

    // 2. Load BIOS
    LOG_SYSTEM_DEBUG("  Loading BIOS...");
    if (!bios_load(&bios_data, bios_path)) {
        // Cleanup on failure
        SDL_GL_DeleteContext(gl_context);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    // Print the "hidden text" bootstrap strings from the BIOS ROM.
    // --bios-strings dumps all TCRF blocks; default prints only the kernel banner.
    if (bios_strings_mode)
        bios_print_all_hidden_strings(&bios_data);
    else
        bios_print_bootstrap_strings(&bios_data);

    // 3. Initialize Interconnect (connects all hardware components)
    LOG_SYSTEM_DEBUG("  Initializing Interconnect...");
    interconnect_init(&interconnect_state, &bios_data, &ram_memory);

    // 4. Initialize the Renderer (using the instance inside the GPU)
    LOG_SYSTEM_DEBUG("  Initializing Renderer...");
    if (!renderer_init(&interconnect_state.gpu.renderer)) {
        LOG_SYSTEM_ERROR("Failed to initialize renderer!");
        // Cleanup on failure
        SDL_GL_DeleteContext(gl_context);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    
    // 5. Load a game disc into the CD-ROM drive (OPTIONAL)
    // NOTE: The emulator can run BIOS-only without a game disc
    // If no disc is loaded, the emulator will just run the BIOS to its menu.
    LOG_SYSTEM_INFO("Attempting to load game disc (optional)...");
    if (!cdrom_load_disc(&interconnect_state.cdrom, "games/Crash Bandicoot.cue")) {
        LOG_SYSTEM_WARN("No game disc loaded. Running BIOS-only mode.");
        // Initialize CD-ROM in "no disc" state for BIOS menu
        interconnect_state.cdrom.disc_present = false;
        interconnect_state.cdrom.drive_state = DRIVE_IDLE;
        LOG_SYSTEM_INFO("CD-ROM initialized in no-disc state for BIOS menu.");
    } else {
        LOG_SYSTEM_INFO("Game disc loaded successfully.");
    }


    // 6. Initialize CPU (pass it the fully connected interconnect)
    LOG_SYSTEM_DEBUG("  Initializing CPU...");
    cpu_init(&cpu_state, &interconnect_state);

    // 7. Set CPU pointer in interconnect for direct exception triggering
    interconnect_set_cpu(&interconnect_state, &cpu_state);

    // 8. Initialize Controller (keyboard input) <<< ADDED
    LOG_SYSTEM_DEBUG("  Initializing Controller...");
    controller_init(&gamepad);
    sio_set_controller_connected(&interconnect_state.sio, true);  // Enable in SIO

    // --- Schedule Initial Events (using new event system) ---
    #define VBLANK_CYCLES 564480
    // Only schedule VBlank event at startup. Timer0 events are scheduled by timer logic when needed.
    eventq_schedule(&interconnect_state, EVQ_VBLANK, VBLANK_CYCLES);
    // PCSX ReARMed-style: Schedule Timer0 event at boot so it is always in the event queue
    eventq_schedule(&interconnect_state, EVQ_TIMER0, 1024); // Initial guess: 1024 cycles

    LOG_SYSTEM_WARN("All Emulator Components Initialized.");

    // --- Main Emulation Loop ---
    LOG_SYSTEM_WARN("=== Main emulation loop starting ===");
    bool should_quit = false;
    SDL_Event event;
    uint64_t total_cycles = 0;

    const uint64_t frame_ticks = (uint64_t)((double)SDL_GetPerformanceFrequency() / 60.0);
    uint64_t next_frame_tick = SDL_GetPerformanceCounter() + frame_ticks;

    while (!should_quit) {
        // --- Handle Input/Window Events ---
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                LOG_SYSTEM_INFO("SDL_QUIT event received.");
                should_quit = true;
            } else if (event.type == SDL_KEYDOWN) {
                if (event.key.keysym.sym == SDLK_ESCAPE) {
                    LOG_SYSTEM_INFO("Escape key pressed. Quitting.");
                    should_quit = true;
                }
            }
        }

        // --- Update Controller State from Keyboard <<< ADDED
        uint16_t button_state = controller_update_from_keyboard(&gamepad);
        sio_set_button_state(&interconnect_state.sio, button_state);

        // --- Inject Keyboard Input into TTY Input Buffer for getc() ---
        const uint8_t* keys = SDL_GetKeyboardState(NULL);
        static bool prev_keys[SDL_NUM_SCANCODES] = {false};

        // Only inject on key press (0->1 transition) to avoid repeats
        if (keys[SDL_SCANCODE_W] && !prev_keys[SDL_SCANCODE_W])
            interconnect_tty_input_add(&interconnect_state, 'w');
        if (keys[SDL_SCANCODE_S] && !prev_keys[SDL_SCANCODE_S])
            interconnect_tty_input_add(&interconnect_state, 's');
        if (keys[SDL_SCANCODE_D] && !prev_keys[SDL_SCANCODE_D])
            interconnect_tty_input_add(&interconnect_state, 'd');
        if (keys[SDL_SCANCODE_A] && !prev_keys[SDL_SCANCODE_A])
            interconnect_tty_input_add(&interconnect_state, 'a');
        if (keys[SDL_SCANCODE_SPACE] && !prev_keys[SDL_SCANCODE_SPACE])
            interconnect_tty_input_add(&interconnect_state, '\n');  // SPACE = Enter
        if (keys[SDL_SCANCODE_RETURN] && !prev_keys[SDL_SCANCODE_RETURN])
            interconnect_tty_input_add(&interconnect_state, '\n');  // Return = Enter
        if (keys[SDL_SCANCODE_E] && !prev_keys[SDL_SCANCODE_E])
            interconnect_tty_input_add(&interconnect_state, 'e');
        if (keys[SDL_SCANCODE_C] && !prev_keys[SDL_SCANCODE_C])
            interconnect_tty_input_add(&interconnect_state, 'c');
        if (keys[SDL_SCANCODE_X] && !prev_keys[SDL_SCANCODE_X])
            interconnect_tty_input_add(&interconnect_state, 'x');
        if (keys[SDL_SCANCODE_Z] && !prev_keys[SDL_SCANCODE_Z])
            interconnect_tty_input_add(&interconnect_state, 'z');

        // Update previous key state
        memcpy(prev_keys, keys, sizeof(prev_keys));

        // --- Run Emulation for One Frame ---
        uint32_t cycles_run = 0;
        
        while (cycles_run < cycles_per_frame) {
            uint32_t cycles_remaining = cycles_per_frame - cycles_run;
            uint32_t to_next_event = eventq_cycles_until_next(&interconnect_state);
            uint32_t run_chunk = (to_next_event == 0) ? 1 : to_next_event;
            if (run_chunk > cycles_remaining) {
                run_chunk = cycles_remaining;
            }

            for (uint32_t i = 0; i < run_chunk; ++i) {
                cpu_run_next_instruction(&cpu_state);
            }

            cycles_run += run_chunk;
        }

        // --- Render and Display Frame ---
        // Flush any remaining primitives to the GPU
        renderer_draw(&interconnect_state.gpu.renderer);
        // Swap buffers to display the frame
        SDL_GL_SwapWindow(window);
        check_gl_error("After SwapWindow");

        // Simple 60Hz throttling to keep frame timing stable.
        uint64_t now = SDL_GetPerformanceCounter();
        if (now < next_frame_tick) {
            uint64_t ticks_left = next_frame_tick - now;
            uint64_t ms_left = (ticks_left * 1000ULL) / (uint64_t)SDL_GetPerformanceFrequency();
            if (ms_left > 1) {
                SDL_Delay((uint32_t)(ms_left - 1));
            }
            while (SDL_GetPerformanceCounter() < next_frame_tick) {
                // short spin for better frame boundary precision
            }
            next_frame_tick += frame_ticks;
        } else {
            // If the frame overran, resync to now instead of accumulating lag forever.
            next_frame_tick = now + frame_ticks;
        }

        total_cycles += cycles_per_frame;
    }

    // --- Cleanup ---
    LOG_SYSTEM_INFO("Emulation loop finished. Cleaning up...");
    
    renderer_destroy(&interconnect_state.gpu.renderer);
    LOG_SYSTEM_INFO("Destroying SDL GL Context and Window...");
    SDL_GL_DeleteContext(gl_context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    LOG_SYSTEM_INFO("SDL Quit.");

    LOG_SYSTEM_INFO("--- ZoniStation One Emulator Finished ---");
    LOG_SYSTEM_INFO("Emulator stopped");
    return 0;
}