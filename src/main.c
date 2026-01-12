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
#include <signal.h>   // For signal handling

// --- Graphics/Windowing Includes ---
// Uses SDL2 for window creation, event handling, and OpenGL context management.
// Uses GLEW for loading modern OpenGL extensions.
#include <SDL2/SDL.h>
#define GLEW_STATIC
#include <GL/glew.h>

// --- Emulator Core Components ---
#include "cpu.h"
#include "cpu/cpu_debugger.h"
#include "interconnect.h"
#include "bios/bios_core.h"
#include "ram.h"
#include "renderer.h"
#include "cdrom/cdrom_core.h" // Modular CDROM controller
#include "controller.h"
#include "log.h"
// #include "event_scheduler.h" // REMOVED: DuckStation-style - no event scheduler
#include "irq/irq_core.h"  // For IRQ module functions

// Add prototype to fix implicit declaration warning
void interconnect_check_bios_boot(struct Interconnect* inter);

// ============================================================================
// Global State
// ============================================================================

// Signal handler flag for clean shutdown
volatile sig_atomic_t signal_received = 0;

// Signal handler for SIGINT (Ctrl+C)
void signal_handler(int signum) {
    (void)signum; // Suppress unused parameter warning
    signal_received = 1;
}

// ============================================================================
// Global Debug State
// ============================================================================

bool debugger_enabled = false;
bool debugger_paused = false;
bool debugger_single_step = false;

// ============================================================================
// Help & Usage Display
// ============================================================================

static void show_usage(const char* program_name) {
    printf("\n");
    printf("ZonistationOne - PlayStation 1 Emulator\n");
    printf("========================================\n\n");
    printf("USAGE:\n");
    printf("  %s [options] [BIOS_PATH]\n\n", program_name);
    
    printf("LOG LEVELS:\n");
    printf("  --silent                No logging output\n");
    printf("  --quiet                 Warnings and errors only\n");
    printf("  (default)               INFO level logging\n");
    printf("  --debug                 Verbose debug output\n");
    printf("  --trace                 Ultra-verbose trace output\n\n");
    
    printf("COMPONENT CONTROL:\n");
    printf("  --no-irq-logs           Disable IRQ/interrupt logging\n");
    printf("  --no-interconnect-logs  Disable I/O interconnect logging\n");
    printf("  --no-dma-logs           Disable DMA transfer logging\n\n");
    
    printf("OUTPUT CONTROL:\n");
    printf("  --log-single-file       Log to emulator_log.txt instead of console\n");
    printf("  --log-rate-limit=N      Rate limit: log first N msgs, then every Nth\n\n");
    
    printf("PERFORMANCE:\n");
    printf("  --gpu-thread            Enable GPU multi-threading (experimental)\n\n");
    
    printf("DEBUGGER:\n");
    printf("  --debugger              Enable CPU debugger\n");
    printf("  --break-at=ADDR         Set initial breakpoint (hex: 0x80001000 or 80001000)\n");
    printf("                          Can be used multiple times for multiple breakpoints\n");
    printf("  Runtime: F12=pause, then use commands: c(ontinue) s(tep) r(egisters) b(reak) q(uit)\n\n");
    
    printf("OTHER:\n");
    printf("  -h, --help              Show this help message\n\n");
    
    printf("ARGUMENTS:\n");
    printf("  BIOS_PATH               Path to PS1 BIOS image\n");
    printf("                          (default: roms/SCPH1001.BIN)\n\n");
    
    printf("LOG CATEGORIES:\n");
    printf("  SYSTEM, CPU, IRQ, DMA, GPU, CDROM, TIMER, BIOS, INTERCONNECT,\n");
    printf("  RENDERER, EVENT, GTE, VRAM, RAM, DEBUG\n\n");
    
    printf("EXAMPLES:\n");
    printf("  %s roms/SCPH1001.BIN\n", program_name);
    printf("    Start with default logging\n\n");
    
    printf("  %s --debug --no-irq-logs roms/SCPH1001.BIN\n", program_name);
    printf("    Debug mode without IRQ spam\n\n");
    
    printf("  %s --gpu-thread --quiet roms/SCPH1001.BIN\n", program_name);
    printf("    Multi-threaded GPU with minimal logging\n\n");
    
    printf("  %s --trace --log-single-file roms/SCPH1001.BIN\n", program_name);
    printf("    Maximum logging to file\n\n");
}

// ============================================================================
// Main Entry Point
// ============================================================================

int main(int argc, char *argv[]) {
    // ========================================================================
    // Command-Line Argument Parsing
    // ========================================================================
    
    const char* bios_path = NULL;
    int log_level = LOG_LEVEL_INFO;
    bool log_single_file = false;
    int log_rate_limit_n = 0;
    bool disable_irq_logs = false;
    bool disable_interconnect_logs = false;
    bool disable_dma_logs = false;
    bool enable_gpu_thread = false;
    bool enable_debugger = false;
    uint32_t initial_breakpoints[10]; // Store up to 10 initial breakpoints
    int initial_breakpoint_count = 0;

    // Parse command-line arguments
    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        
        // Help option
        if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) {
            show_usage(argv[0]);
            return 0;
        }
        // Log level options
        else if (strcmp(arg, "--silent") == 0) {
            log_level = LOG_LEVEL_SILENT;
        }
        else if (strcmp(arg, "--quiet") == 0) {
            log_level = LOG_LEVEL_WARN;
        }
        else if (strcmp(arg, "--debug") == 0) {
            log_level = LOG_LEVEL_DEBUG;
        }
        else if (strcmp(arg, "--trace") == 0) {
            log_level = LOG_LEVEL_TRACE;
        }
        // Component control options
        else if (strcmp(arg, "--no-irq-logs") == 0) {
            disable_irq_logs = true;
        }
        else if (strcmp(arg, "--no-interconnect-logs") == 0) {
            disable_interconnect_logs = true;
        }
        else if (strcmp(arg, "--no-dma-logs") == 0) {
            disable_dma_logs = true;
        }
        // Output control options
        else if (strcmp(arg, "--log-single-file") == 0) {
            log_single_file = true;
        }
        else if (strncmp(arg, "--log-rate-limit=", 17) == 0) {
            log_rate_limit_n = atoi(arg + 17);
            if (log_rate_limit_n <= 0) {
                fprintf(stderr, "Error: --log-rate-limit value must be positive\n");
                return 1;
            }
        }
        // Performance options
        else if (strcmp(arg, "--gpu-thread") == 0) {
            enable_gpu_thread = true;
        }
        // Debugger options
        else if (strcmp(arg, "--debugger") == 0) {
            enable_debugger = true;
        }
        else if (strncmp(arg, "--break-at=", 11) == 0) {
            // Parse breakpoint address (format: --break-at=0x80001000 or --break-at=80001000)
            uint32_t addr = 0;
            const char* addr_str = arg + 11;
            if ((sscanf(addr_str, "0x%x", &addr) == 1 || sscanf(addr_str, "%x", &addr) == 1) &&
                initial_breakpoint_count < 10) {
                initial_breakpoints[initial_breakpoint_count++] = addr;
                printf("Will set breakpoint at 0x%08X\n", addr);
            } else if (initial_breakpoint_count >= 10) {
                fprintf(stderr, "Error: Too many breakpoints (max 10)\n");
                return 1;
            } else {
                fprintf(stderr, "Error: Invalid breakpoint address format '%s'\n", addr_str);
                fprintf(stderr, "Use format: --break-at=0x80001000 or --break-at=80001000\n");
                return 1;
            }
        }
        // BIOS path argument
        else if (arg[0] != '-') {
            if (bios_path != NULL) {
                fprintf(stderr, "Error: Multiple BIOS paths specified\n");
                fprintf(stderr, "Use '%s --help' for usage information\n", argv[0]);
                return 1;
            }
            bios_path = arg;
        }
        // Unknown option
        else {
            fprintf(stderr, "Error: Unknown option '%s'\n", arg);
            fprintf(stderr, "Use '%s --help' for usage information\n", argv[0]);
            return 1;
        }
    }
    
    // Set default BIOS path if not specified
    if (!bios_path) {
        bios_path = "roms/SCPH1001.BIN";
    }
    
    // ========================================================================
    // Logging System Initialization
    // ========================================================================
    
    log_init();
    log_set_level(log_level);
    
    // Apply rate limiting to noisy components
    if (log_rate_limit_n > 0) {
        log_set_rate_limit(LOG_CAT_IRQ, 5, log_rate_limit_n);
        log_set_rate_limit(LOG_CAT_INTERCONNECT, 10, log_rate_limit_n);
        log_set_rate_limit(LOG_CAT_DMA, 5, log_rate_limit_n);
    }
    
    // Apply component-specific logging disables
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
    
    LOG_SYSTEM_WARN("Emulator started");

    // ========================================================================
    // Signal Handler Setup
    // ========================================================================
    
    // Install signal handler for SIGINT (Ctrl+C) to allow clean shutdown
    signal(SIGINT, signal_handler);

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
    // (DuckStation-style: No event scheduler - VBlank fires directly at end of frame)
    // const uint32_t cycles_per_frame = 33868800 / 60; // Moved below - see CYCLES_PER_FRAME

    LOG_SYSTEM_INFO("--- ZoniStation One Emulator ---");
    LOG_SYSTEM_INFO("Attempting to load BIOS from: %s", bios_path);

    // --- SDL & OpenGL Initialization ---
    LOG_SYSTEM_INFO("Initializing SDL Video...");
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        LOG_SYSTEM_ERROR("SDL_Init Error: %s", SDL_GetError());
        return 1;
    }

    // Set OpenGL context attributes (Core Profile 3.3)
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);

    LOG_SYSTEM_INFO("Creating SDL Window (1024x512, OpenGL)...");
    SDL_Window* window = SDL_CreateWindow(
        "ZoniStation One",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        1024, 512,
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

    // Initialize GLEW
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

    BiosState bios_data;
    Ram ram_memory;
    Interconnect interconnect_state;
    Cpu cpu_state;

    // 1. Initialize RAM
    LOG_SYSTEM_DEBUG("  Initializing RAM...");
    ram_init(&ram_memory);

    // 2. Load BIOS
    LOG_SYSTEM_DEBUG("  Loading BIOS...");
    bios_init(&bios_data);
    if (!bios_load_and_verify(&bios_data, bios_path)) {
        SDL_GL_DeleteContext(gl_context);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    // 3. Initialize Interconnect
    LOG_SYSTEM_DEBUG("  Initializing Interconnect...");
    interconnect_init(&interconnect_state, &bios_data, &ram_memory);

    // 4. Initialize Renderer
    LOG_SYSTEM_DEBUG("  Initializing Renderer...");
    if (!renderer_init(&interconnect_state.gpu.renderer)) {
        LOG_SYSTEM_ERROR("Failed to initialize renderer!");
        SDL_GL_DeleteContext(gl_context);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    
    // 4b. Initialize GPU Thread (multi-threading support)
    if (enable_gpu_thread) {
        LOG_SYSTEM_INFO("  Initializing GPU Thread...");
        gpu_thread_init(&interconnect_state.gpu_thread_state, &interconnect_state.gpu, true);
        
        // Set OpenGL context for GPU thread
        gpu_thread_set_gl_context(&interconnect_state.gpu_thread_state, window, gl_context);
        
        // Release context from main thread so GPU thread can use it
        SDL_GL_MakeCurrent(window, NULL);
        
        // Link GPU to thread state
        interconnect_state.gpu.thread_state = &interconnect_state.gpu_thread_state;
        
        if (!gpu_thread_start(&interconnect_state.gpu_thread_state)) {
            LOG_SYSTEM_ERROR("Failed to start GPU thread!");
            SDL_GL_DeleteContext(gl_context);
            SDL_DestroyWindow(window);
            SDL_Quit();
            return 1;
        }
        LOG_SYSTEM_INFO("  GPU Thread started successfully (multi-threaded mode enabled)");
    } else {
        LOG_SYSTEM_INFO("  GPU Thread disabled (running in single-threaded mode)");
        gpu_thread_init(&interconnect_state.gpu_thread_state, &interconnect_state.gpu, false);
        
        // No threading - set thread_state to NULL
        interconnect_state.gpu.thread_state = NULL;
    }
    
    // 5. Load game disc (optional)
    LOG_SYSTEM_INFO("Attempting to load game disc (optional)...");
    if (!cdrom_load_disc(&interconnect_state.cdrom_state, "games/Crash Bandicoot.cue")) {
        LOG_SYSTEM_WARN("No game disc loaded. Running BIOS-only mode.");
    } else {
        LOG_SYSTEM_INFO("Game disc loaded successfully.");
    }

    // 6. Initialize CPU
    LOG_SYSTEM_DEBUG("  Initializing CPU...");
    LOG_SYSTEM_INFO("Threading: GPU thread %s", 
                    enable_gpu_thread ? "enabled" : "disabled");
    
    cpu_init(&cpu_state, &interconnect_state);
    interconnect_set_cpu(&interconnect_state, &cpu_state);

    // 7. Initialize Debugger (if enabled)
    if (enable_debugger) {
        LOG_SYSTEM_INFO("Initializing CPU debugger...");
        cpu_debugger_init();
        debugger_enabled = true;
        
        // Set initial breakpoints from command line
        for (int i = 0; i < initial_breakpoint_count; i++) {
            if (cpu_debugger_add_breakpoint(BREAKPOINT_TYPE_EXECUTE, initial_breakpoints[i], false, true)) {
                LOG_SYSTEM_INFO("Breakpoint set at 0x%08X", initial_breakpoints[i]);
            } else {
                LOG_SYSTEM_WARN("Failed to set breakpoint at 0x%08X", initial_breakpoints[i]);
            }
        }
        
        LOG_SYSTEM_INFO("Debugger initialized with %d breakpoint(s)", initial_breakpoint_count);
    }

    // ========================================================================
    // VBlank/HBlank Counter (DuckStation-style - no event scheduler)
    // ========================================================================
    static uint32_t vblank_count = 0;
    static uint32_t hblank_count = 0;
    const uint32_t CYCLES_PER_FRAME = 564480; // NTSC: 33868800 Hz / 60 Hz
    
    // PS1 NTSC timing (DuckStation values):
    // - CPU: 33.8688 MHz
    // - Scanlines per frame: 263 (NTSC)
    // - Cycles per scanline: ~2144 (calculated from GPU dot clock)
    const uint32_t CYCLES_PER_SCANLINE = 2144;
    uint32_t cycles_until_hblank = CYCLES_PER_SCANLINE;
    
    LOG_SYSTEM_INFO("VBlank timing: %u cycles per frame (60 Hz NTSC)", CYCLES_PER_FRAME);
    LOG_SYSTEM_INFO("HBlank timing: %u cycles per scanline (~15.7 KHz)", CYCLES_PER_SCANLINE);

    LOG_SYSTEM_WARN("All Emulator Components Initialized.");
    LOG_SYSTEM_WARN("=== Main emulation loop starting ===");

    bool should_quit = false;
    SDL_Event event;
    uint64_t total_cycles = 0;

    while (!should_quit) {
        // Check for SIGINT (Ctrl+C) signal
        if (signal_received) {
            LOG_SYSTEM_INFO("SIGINT received, shutting down...");
            should_quit = true;
            break;
        }
        
        // Handle SDL events (always, not just when debugger is enabled)
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                should_quit = true;
                break;
            }
            // Handle controller input
            controller_handle_sdl_event(&event);
            
            // Handle debugger hotkeys if debugger is enabled
            if (debugger_enabled && event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_F12) {
                // F12 hotkey to pause execution
                cpu_debugger_set_single_step_flag();
                debugger_paused = true;
                printf("\n=== DEBUGGER PAUSED (F12) ===\n");
                break;
            }
        }
        
        // Handle debugger commands if paused
        if (debugger_enabled && debugger_paused) {
            printf("\n=== DEBUGGER PAUSED ===\n");
            printf("Commands: c(ontinue) s(tep) r(egisters) b(reak) <addr> q(uit)\n");
            printf("Hotkeys: F12=pause execution\n");
            printf("debugger> ");
            fflush(stdout);
            
            char cmd[256];
            if (fgets(cmd, sizeof(cmd), stdin) != NULL) {
                // Remove newline
                cmd[strcspn(cmd, "\n")] = 0;
                
                if (strcmp(cmd, "c") == 0 || strcmp(cmd, "continue") == 0) {
                    debugger_paused = false;
                    printf("Continuing execution...\n");
                } else if (strcmp(cmd, "s") == 0 || strcmp(cmd, "step") == 0) {
                    cpu_debugger_set_single_step_flag();
                    debugger_paused = false;
                    printf("Stepping one instruction...\n");
                } else if (strcmp(cmd, "r") == 0 || strcmp(cmd, "registers") == 0) {
                    printf("CPU Registers:\n");
                    for (int i = 0; i < 32; i += 4) {
                        printf("  r%02d: %08x r%02d: %08x r%02d: %08x r%02d: %08x\n",
                               i, cpu_state.regs.r[i], i+1, cpu_state.regs.r[i+1], 
                               i+2, cpu_state.regs.r[i+2], i+3, cpu_state.regs.r[i+3]);
                    }
                    printf("  PC: %08x\n", cpu_state.current_pc);
                } else if (strncmp(cmd, "b ", 2) == 0 || strncmp(cmd, "break ", 6) == 0) {
                    const char* addr_str = strncmp(cmd, "b ", 2) == 0 ? cmd + 2 : cmd + 6;
                    uint32_t addr = 0;
                    if (sscanf(addr_str, "0x%x", &addr) == 1 || sscanf(addr_str, "%x", &addr) == 1) {
                        if (cpu_debugger_add_breakpoint(BREAKPOINT_TYPE_EXECUTE, addr, false, true)) {
                            printf("Breakpoint set at 0x%08X\n", addr);
                        } else {
                            printf("Failed to set breakpoint at 0x%08X\n", addr);
                        }
                    } else {
                        printf("Invalid address format. Use: b 0x80001000 or b 80001000\n");
                    }
                } else if (strcmp(cmd, "q") == 0 || strcmp(cmd, "quit") == 0) {
                    should_quit = true;
                    printf("Quitting...\n");
                } else if (strlen(cmd) > 0) {
                    printf("Unknown command: %s\n", cmd);
                }
            }
            
            // Skip emulation if we're still paused
            if (debugger_paused) {
                continue;
            }
        }

        // Run emulation for one frame (DuckStation-style direct execution)
        uint32_t cycles_run = 0;
        
        while (cycles_run < CYCLES_PER_FRAME) {
            // Run CPU instruction (most instructions = 1 cycle, some slower)
            cpu_run_next_instruction(&cpu_state);
            uint32_t cycles_executed = 1; // Most instructions are 1 cycle
            
            // Tick timers IMMEDIATELY after each instruction (DuckStation-style)
            timers_add_sysclk_ticks(&interconnect_state.timers_state, &interconnect_state, cycles_executed);
            
            // === HBlank timing (critical for Timer 0/1 sync modes) ===
            if (cycles_until_hblank <= cycles_executed) {
                // HBlank start - pulse Timer 0 gate (for sync modes)
                timer_set_gate(&interconnect_state.timers_state, &interconnect_state, 0, true);
                
                // Add HBlank tick for Timer 1 (if using external clock)
                timers_add_hblank_ticks(&interconnect_state.timers_state, &interconnect_state, 1);
                
                hblank_count++;
                
                // Log first few HBlanks for debugging
                if (hblank_count <= 5) {
                    LOG_SYSTEM_INFO("[HBlank] #%u at cycle %u", hblank_count, cycles_run);
                }
                
                // HBlank end - deassert Timer 0 gate after short pulse
                // (In reality HBlank lasts ~26 GPU cycles, but we pulse instantly)
                timer_set_gate(&interconnect_state.timers_state, &interconnect_state, 0, false);
                
                // Reset counter with carry-over
                cycles_until_hblank = CYCLES_PER_SCANLINE - (cycles_executed - cycles_until_hblank);
            } else {
                cycles_until_hblank -= cycles_executed;
            }
            
            // === VBlank timing ===
            static uint32_t cycles_until_vblank = CYCLES_PER_FRAME;
            if (cycles_until_vblank <= cycles_executed) {
                // Trigger VBlank (IRQ0)
                irq_request(&interconnect_state.irq_state, 0, "VBlank");
                vblank_count++;
                
                if (vblank_count <= 3) {
                    LOG_SYSTEM_INFO("[VBlank] #%u at cycle %u", vblank_count, cycles_run);
                }
                
                // Reset for next frame
                cycles_until_vblank = CYCLES_PER_FRAME - (cycles_executed - cycles_until_vblank);
            } else {
                cycles_until_vblank -= cycles_executed;
            }
            interconnect_check_cdrom_events(&interconnect_state);
            
            cycles_run += cycles_executed;
        }
        
        // === END OF FRAME - Fire VBlank directly (no event scheduler) ===
        vblank_count++;
        
        // DuckStation-style field toggling:
        // 1. Switch display field EARLY at vblank start (for what we're SHOWING)
        // 2. Toggle drawing field at END of vblank/start of new frame (for what we DRAW TO)
        if (interconnect_state.gpu.interlaced) {
            // Switch display field early (for rendering the correct field)
            interconnect_state.gpu.crtc_state.interlaced_display_field = 
                interconnect_state.gpu.crtc_state.interlaced_field ^ 1;
            
            // Toggle the GPUSTAT interlaced_field bit (inverted logic in DuckStation)
            interconnect_state.gpu.field = 
                (interconnect_state.gpu.crtc_state.interlaced_field == 0) ? Bottom : Top;
        } else {
            interconnect_state.gpu.crtc_state.interlaced_display_field = 0;
            interconnect_state.gpu.field = Top;
        }
        
        // Fire VBlank interrupt (IRQ0)
        interconnect_request_irq(&interconnect_state, IRQ_VBLANK, "VBlank");
        
        // Notify Timer 1 of VBlank gate (for sync mode)
        timer_set_gate(&interconnect_state.timers_state, &interconnect_state, 1, true);
        
        // At end of VBlank / start of new frame, toggle the DRAWING field
        if (interconnect_state.gpu.interlaced) {
            interconnect_state.gpu.crtc_state.interlaced_field ^= 1;
        } else {
            interconnect_state.gpu.crtc_state.interlaced_field = 0;
        }
        
        // Log VBlank periodically
        if (vblank_count <= 5 || vblank_count % 60 == 0) {
            uint32_t i_stat = irq_read_i_stat(&interconnect_state.irq_state);
            uint32_t i_mask = irq_read_i_mask(&interconnect_state.irq_state);
            LOG_SYSTEM_INFO("[VBlank] Frame #%u, I_STAT=0x%04x, I_MASK=0x%04x, Disp=%s, Area=(%u,%u) %ux%u", 
                           vblank_count, i_stat, i_mask,
                           interconnect_state.gpu.display_disabled ? "OFF" : "ON",
                           interconnect_state.gpu.display_vram_x_start,
                           interconnect_state.gpu.display_vram_y_start,
                           interconnect_state.gpu.display_width_hint,
                           interconnect_state.gpu.display_height_hint);
        }
        
        // Final CDROM check and rendering
        interconnect_check_cdrom_events(&interconnect_state);
        interconnect_check_bios_boot(&interconnect_state);
        
        // If GPU thread is enabled, sync it (renderer_draw happens in GPU thread)
        if (enable_gpu_thread) {
            // Sync ensures GPU thread processes all pending commands AND draws
            gpu_thread_sync(&interconnect_state.gpu_thread_state, true);
            // GPU thread released the context, re-acquire it for SwapWindow
            SDL_GL_MakeCurrent(window, gl_context);
        } else {
            // Single-threaded: draw on main thread
            renderer_draw(&interconnect_state.gpu.renderer);
            
            // Upload VRAM to texture
            renderer_upload_vram(&interconnect_state.gpu.renderer, (const uint16_t*)interconnect_state.gpu.vram.data); // Use modular GPU VRAM
            
            // Display VRAM framebuffer if display is enabled
            // if (!interconnect_state.gpu.display_disabled) {
                // GUIDE STEP: Display entire VRAM to see the boot logo being drawn
                // regardless of display area settings.
                renderer_display_vram(&interconnect_state.gpu.renderer, 0, 0, 1024, 512);

                /*
                renderer_display_vram(&interconnect_state.gpu.renderer,
                    (uint16_t)interconnect_state.gpu.display_vram_x_start,
                    (uint16_t)interconnect_state.gpu.display_vram_y_start,
                    interconnect_state.gpu.display_width_hint,
                    interconnect_state.gpu.display_height_hint);
                */
            // }
        }
        
        // Swap buffers (works for both threaded and non-threaded modes)
        SDL_GL_SwapWindow(window);
        check_gl_error("After SwapWindow");
        total_cycles += CYCLES_PER_FRAME;
    }

    // --- Cleanup ---
    LOG_SYSTEM_INFO("Emulation loop finished. Cleaning up...");

    // Shutdown GPU thread
    LOG_SYSTEM_INFO("Shutting down GPU thread...");
    gpu_thread_shutdown(&interconnect_state.gpu_thread_state);
    LOG_SYSTEM_INFO("GPU thread shutdown complete.");

    // Destroy GPU VRAM mutex
    mutex_destroy(&interconnect_state.gpu.vram_mutex);

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