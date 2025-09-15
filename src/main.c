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
#include "event_scheduler.h" // <<< ADDED: Include for event scheduling

// Add prototype to fix implicit declaration warning
void interconnect_check_bios_boot(struct Interconnect* inter);

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
    int log_level = LOG_LEVEL_INFO; // Changed: Default to WARN for better performance
    bool show_help = false;
    bool log_single_file = false;
    int log_rate_limit_n = 0;
    bool disable_irq_logs = false;
    bool disable_interconnect_logs = false;
    bool disable_dma_logs = false;
    bool fast_mode = false; // PERFORMANCE: New fast mode option

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
        } else if (strcmp(argv[i], "--silent") == 0) {
            log_level = LOG_LEVEL_SILENT;
        } else if (strcmp(argv[i], "--no-irq-logs") == 0) {
            disable_irq_logs = true;
        } else if (strcmp(argv[i], "--no-interconnect-logs") == 0) {
            disable_interconnect_logs = true;
        } else if (strcmp(argv[i], "--no-dma-logs") == 0) {
            disable_dma_logs = true;
        } else if (strcmp(argv[i], "--fast") == 0) {
            fast_mode = true;
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
        printf("  --debug            Verbose debug output (default: WARN)\n");
        printf("  --trace            Ultra-verbose trace output\n");
        printf("  --fast             Performance mode: minimal logging, max speed\n\n");
        printf("Component Control (PCSX ReARMed style):\n");
        printf("  --no-irq-logs      Disable IRQ/interrupt logging (reduces spam)\n");
        printf("  --no-interconnect-logs Disable I/O interconnect logging (reduces spam)\n");
        printf("  --no-dma-logs      Disable DMA transfer logging\n\n");
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
    // Initialize new logging system with performance optimizations
    log_init();
    
    // PERFORMANCE: Apply aggressive rate limiting by default for noisy categories
    log_set_rate_limit(LOG_CAT_CPU, 3, 10000);          // First 3 CPU logs, then every 10000th
    log_set_rate_limit(LOG_CAT_IRQ, 2, 5000);           // First 2 IRQ logs, then every 5000th
    log_set_rate_limit(LOG_CAT_INTERCONNECT, 5, 50000); // First 5 interconnect logs, then every 50000th
    log_set_rate_limit(LOG_CAT_DMA, 3, 10000);          // First 3 DMA logs, then every 10000th
    log_set_rate_limit(LOG_CAT_GPU, 5, 1000);           // First 5 GPU logs, then every 1000th
    log_set_rate_limit(LOG_CAT_CDROM, 10, 1000);        // First 10 CDROM logs, then every 1000th
    
    if (log_rate_limit_n > 0) {
        // Override with user-specified rate limiting
        log_set_rate_limit(LOG_CAT_IRQ, 5, log_rate_limit_n);
        log_set_rate_limit(LOG_CAT_INTERCONNECT, 10, log_rate_limit_n);
        log_set_rate_limit(LOG_CAT_DMA, 5, log_rate_limit_n);
        log_set_rate_limit(LOG_CAT_CPU, 3, log_rate_limit_n);
        log_set_rate_limit(LOG_CAT_GPU, 5, log_rate_limit_n);
        log_set_rate_limit(LOG_CAT_CDROM, 10, log_rate_limit_n);
    }
    
    // PERFORMANCE: Fast mode - disable most logging for maximum speed
    if (fast_mode) {
        log_level = LOG_LEVEL_ERROR;  // Only errors
        log_set_category_enabled(LOG_CAT_CPU, false);
        log_set_category_enabled(LOG_CAT_IRQ, false);
        log_set_category_enabled(LOG_CAT_INTERCONNECT, false);
        log_set_category_enabled(LOG_CAT_DMA, false);
        log_set_category_enabled(LOG_CAT_GPU, false);
        log_set_category_enabled(LOG_CAT_TIMER, false);
        // Keep SYSTEM, BIOS, CDROM for critical messages
        LOG_SYSTEM_INFO("Fast mode enabled - minimal logging for maximum performance");
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
    LOG_SYSTEM_INFO("Emulator started");

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
    uint32_t cycles_per_frame = 33868800 / 60; // PSX CPU speed / NTSC refresh rate
    
    // PERFORMANCE: Reduce CPU cycles in fast mode for much better performance
    if (fast_mode) {
        cycles_per_frame = cycles_per_frame / 4; // Run at 1/4 speed for 4x performance boost
        LOG_SYSTEM_INFO("Fast mode: Reduced CPU cycles to %u per frame (1/4 speed)", cycles_per_frame);
    }

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
        1024, 512, // Native PSX VRAM resolution
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
    LOG_SYSTEM_INFO("Initializing Emulator Components...");

    Bios bios_data;
    Ram ram_memory;
    Interconnect interconnect_state;
    Cpu cpu_state;

    // 1. Initialize RAM
    LOG_SYSTEM_INFO("  Initializing RAM...");
    ram_init(&ram_memory);

    // 2. Load BIOS
    LOG_SYSTEM_INFO("  Loading BIOS...");
    if (!bios_load(&bios_data, bios_path)) {
        // Cleanup on failure
        SDL_GL_DeleteContext(gl_context);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    // 3. Initialize Interconnect (connects all hardware components)
    LOG_SYSTEM_INFO("  Initializing Interconnect...");
    interconnect_init(&interconnect_state, &bios_data, &ram_memory);

    // 4. Initialize the Renderer (using the instance inside the GPU)
    LOG_SYSTEM_INFO("  Initializing Renderer...");
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
    if (!cdrom_load_disc(&interconnect_state.cdrom, "games/Crash Bandicoot.bin")) {
        LOG_SYSTEM_INFO("No game disc loaded. Running BIOS-only mode.");
        // Initialize CD-ROM in "no disc" state for BIOS menu
        interconnect_state.cdrom.disc_present = false;
        interconnect_state.cdrom.current_state = CD_STATE_IDLE;
        LOG_SYSTEM_INFO("CD-ROM initialized in no-disc state for BIOS menu.");
    } else {
        LOG_SYSTEM_INFO("Game disc loaded successfully.");
    }


    // 6. Initialize CPU (pass it the fully connected interconnect)
    LOG_SYSTEM_INFO("  Initializing CPU...");
    cpu_init(&cpu_state, &interconnect_state);

    // --- Schedule Initial Events (using new event system) ---
    #define VBLANK_CYCLES 564480
    // Only schedule VBlank event at startup. Timer0 events are scheduled by timer logic when needed.
    eventq_schedule(&interconnect_state, EVQ_VBLANK, VBLANK_CYCLES);
    // PCSX ReARMed-style: Schedule Timer0 event at boot so it is always in the event queue
    eventq_schedule(&interconnect_state, EVQ_TIMER0, 1024); // Initial guess: 1024 cycles

    LOG_SYSTEM_INFO("All Emulator Components Initialized.");

    // --- Main Emulation Loop ---
    LOG_SYSTEM_INFO("=== Main emulation loop starting ===");
    bool should_quit = false;
    SDL_Event event;
    uint64_t total_cycles = 0;
    
    // PERFORMANCE: Frame skipping for fast mode
    uint32_t frame_skip = fast_mode ? 2 : 0; // Skip 2 out of 3 frames in fast mode
    uint32_t frame_counter = 0;

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

        // --- Run Emulation for One Frame ---
        uint32_t cycles_run = 0;
        
        // FAST MODE: Run CPU instructions in batches for better performance
        const uint32_t batch_size = fast_mode ? 10000 : 1000; // Larger batches in fast mode
        
        // FIX: BIOS Boot Bypass - Force continue if stuck too long
        static uint64_t total_instructions = 0;
        static uint32_t last_progress_pc = 0xbfc00000;
        static uint32_t stuck_counter = 0;
        static bool bios_menu_reached = false;
        
        while (cycles_run < cycles_per_frame) {
            // Run CPU instructions in batches for better performance
            uint32_t batch_cycles = (cycles_per_frame - cycles_run > batch_size) ? batch_size : (cycles_per_frame - cycles_run);
            
            for (uint32_t i = 0; i < batch_cycles; i++) {
                cpu_run_next_instruction(&cpu_state);  // Use debug version to compare
                total_instructions++;
                
                // Only check for events every 100 instructions in fast mode, every 10 in normal mode
                if ((fast_mode && (i % 100 == 0)) || (!fast_mode && (i % 10 == 0))) {
                    eventq_dispatch_due(&interconnect_state);
                }
            }
            
            cycles_run += batch_cycles;
            
            // BIOS Boot Progress Check (only check periodically for performance)
            if (total_instructions % 50000 == 0) { // Check every 50k instructions instead of every instruction
                // Check if we've reached the BIOS menu (common patterns)
                if (!bios_menu_reached && 
                    (cpu_state.pc == 0x80000000 || 
                     (cpu_state.pc >= 0x80000000 && cpu_state.pc < 0x80200000) ||
                     cpu_state.pc == 0x80000080)) {
                    bios_menu_reached = true;
                    LOG_SYSTEM_INFO("BIOS-BOOT: BIOS menu reached at PC=0x%08x after %llu instructions", 
                                   cpu_state.pc, total_instructions);
                    // Enable interrupts for BIOS menu operation
                    interconnect_state.irq_mask = 0x0003; // Enable IRQ0 (Timer0) and IRQ1 (VBlank)
                    LOG_SYSTEM_INFO("BIOS-BOOT: Interrupts enabled for BIOS menu operation.");
                }
                
                if (cpu_state.pc == last_progress_pc) {
                    stuck_counter++;
                    // If stuck for too long, force enable interrupts and continue
                    if (stuck_counter > 10) { // Much faster recovery - check every 50k instructions
                        if (!fast_mode) LOG_SYSTEM_WARN("BIOS-BOOT: STUCK at PC=0x%08x. Forcing interrupt enable.", cpu_state.pc);
                        // Force enable interrupts to wake up BIOS
                        interconnect_state.irq_mask = 0x0003; // Enable IRQ0 (Timer0) and IRQ1 (VBlank)
                        stuck_counter = 0; // Reset counter
                    }
                } else {
                    last_progress_pc = cpu_state.pc;
                    stuck_counter = 0;
                }
                
                // Progress reporting (much less frequent)
                if (!fast_mode && total_instructions % 10000000 == 0) {
                    LOG_SYSTEM_INFO("BIOS-BOOT: Progress: %llu instructions, PC=0x%08x, I_MASK=0x%04x, I_STAT=0x%04x", 
                                   total_instructions, cpu_state.pc, 
                                   interconnect_state.irq_mask, interconnect_state.irq_status);
                }
            }
        }
        
        // Step timers once per frame with the total cycles executed
        timers_step(&interconnect_state.timers_state, cycles_per_frame);
        // Step the CD-ROM scheduler
        cdrom_step(&interconnect_state.cdrom, cycles_per_frame);
        // Check if BIOS needs boot helper for interrupt configuration
        interconnect_check_bios_boot(&interconnect_state);
        
        // --- Render and Display Frame (with frame skipping) ---
        frame_counter++;
        bool should_render = !fast_mode || (frame_counter % (frame_skip + 1) == 0);
        
        if (should_render) {
            SDL_GL_SwapWindow(window);
            check_gl_error("After SwapWindow");
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