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
#include <string.h>
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
#include "cdrom.h"
#include "cdrom_audio.h"
#include "log.h"
#include "debug_ui.h"
#include "event_scheduler.h"
#include "controller.h"
#include "debugger.h"
#include "spu.h"


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
    const char* game_path = NULL;
    const char* exe_path = NULL;
    bool show_help = false;

    for (int i = 1; i < argc; ++i) {
        if (strncmp(argv[i], "--game=", 7) == 0) {
            game_path = argv[i] + 7;
        } else if (strncmp(argv[i], "--exe=", 6) == 0) {
            exe_path = argv[i] + 6;
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
        printf("PS1 Emulator\n");
        printf("Usage: %s [options] <BIOS_PATH>\n\n", argv[0]);
        printf("Game Loading:\n");
        printf("  --game=<path>      Load a game disc (.cue or .bin) & boot via BIOS\n");
        printf("  --exe=<path>       Boot PS-X EXE directly (skips BIOS entirely)\n\n");
        printf("  --help, -h         Show this help message\n");
        printf("  <BIOS_PATH>        Path to PS1 BIOS image (default: roms/SCPH1001.BIN)\n");
        return 0;
    }
    
    // Initialize new logging system
    log_init();
    
    if (!bios_path) {
        bios_path = "roms/SCPH1001.BIN";
    }
    
    LOG_SYSTEM_WARN("[SYSTEM] Emulator started");

    // --- File Logging Setup ---
    // (Removed CLI-based log file setup; ImGui handles logging now)

    // --- Configuration ---
    // Define a number of CPU cycles to run per frame. This helps pace the emulation.
    // This value might need tuning for performance vs. accuracy.
    const uint32_t cycles_per_frame = 33868800 / 60; // PSX CPU speed / NTSC refresh rate

         LOG_SYSTEM_INFO("[SYSTEM] --- ZoniStation One Emulator ---");
     LOG_SYSTEM_INFO("[SYSTEM] Attempting to load BIOS from: %s", bios_path);

    // --- SDL & OpenGL Initialization ---
         LOG_SYSTEM_INFO("[SYSTEM] Initializing SDL Video...");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
                 LOG_SYSTEM_ERROR("[SYSTEM] SDL_Init Error: %s", SDL_GetError());
        return 1;
    }

    // Set OpenGL context attributes for a modern OpenGL 3.3 Core Profile.
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);

         LOG_SYSTEM_INFO("[SYSTEM] Creating SDL Window (1024x512, OpenGL)...");
    SDL_Window* window = SDL_CreateWindow(
        "ZoniStation One",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        1024, 512, // Full VRAM size for black border (classic PSX look)
        SDL_WINDOW_OPENGL
    );
    if (!window) {
                 LOG_SYSTEM_ERROR("[SYSTEM] SDL_CreateWindow Error: %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }

         LOG_SYSTEM_INFO("[SYSTEM] Creating OpenGL Context...");
    SDL_GLContext gl_context = SDL_GL_CreateContext(window);
    if (!gl_context) {
                 LOG_SYSTEM_ERROR("[SYSTEM] SDL_GL_CreateContext Error: %s", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    // Initialize GLEW *after* the OpenGL context is created.
    LOG_SYSTEM_INFO("[SYSTEM] Initializing GLEW...");
    glewExperimental = GL_TRUE;
    GLenum glewError = glewInit();
    if (glewError != GLEW_OK) {
        LOG_SYSTEM_ERROR("[SYSTEM] Error initializing GLEW! %s", glewGetErrorString(glewError));
        SDL_GL_DeleteContext(gl_context);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    LOG_SYSTEM_INFO("[SYSTEM] GLEW Initialized. OpenGL Version: %s", glGetString(GL_VERSION));
    check_gl_error("After GLEW Init");

    // Initialize ImGui Debug UI
    debug_ui_init(window, gl_context);

    // --- Emulator Component Initialization ---
    LOG_SYSTEM_WARN("[SYSTEM] Initializing Emulator Components...");

    Bios bios_data;
    Ram ram_memory;
    Interconnect interconnect_state;
    Cpu cpu_state;
    Controller gamepad;  // <<< ADDED: Game controller state

    // 1. Initialize RAM
    LOG_SYSTEM_DEBUG("[SYSTEM]   Initializing RAM...");
    ram_init(&ram_memory);

    // 2. Load BIOS
    LOG_SYSTEM_DEBUG("[SYSTEM]   Loading BIOS...");
    if (!bios_load(&bios_data, bios_path)) {
        // Cleanup on failure
        SDL_GL_DeleteContext(gl_context);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    // Print the "hidden text" bootstrap strings from the BIOS ROM.
    bios_print_bootstrap_strings(&bios_data);

    // 3. Initialize Interconnect (connects all hardware components)
    LOG_SYSTEM_DEBUG("[SYSTEM]   Initializing Interconnect...");
    interconnect_init(&interconnect_state, &bios_data, &ram_memory);

    // 4. Initialize the Renderer (using the instance inside the GPU)
    LOG_SYSTEM_DEBUG("[SYSTEM]   Initializing Renderer...");
    if (!renderer_init(&interconnect_state.gpu.renderer)) {
        LOG_SYSTEM_ERROR("[SYSTEM] Failed to initialize renderer!");
        // Cleanup on failure
        SDL_GL_DeleteContext(gl_context);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    
    // 5. Load a game disc into the CD-ROM drive (OPTIONAL)
    // NOTE: The emulator can run BIOS-only without a game disc
    // If no disc is loaded, the emulator will just run the BIOS to its menu.
    /* Open SDL audio device for CD audio + SPU output */
    cdrom_audio_sdl_open(&interconnect_state.cdrom.audio_fifo);
    cdrom_audio_set_spu(&interconnect_state.spu);

    LOG_SYSTEM_INFO("[SYSTEM] Attempting to load game disc (optional)...");
    if (game_path != NULL) {
        if (!cdrom_load_disc(&interconnect_state.cdrom, game_path)) {
            LOG_SYSTEM_WARN("[SYSTEM] Failed to load game disc: %s. Running BIOS-only mode.", game_path);
            interconnect_state.cdrom.disc_present = false;
            interconnect_state.cdrom.drive_state = DRIVE_IDLE;
            LOG_SYSTEM_INFO("[SYSTEM] CD-ROM initialized in no-disc state for BIOS menu.");
        } else {
            LOG_SYSTEM_INFO("[SYSTEM] Game disc loaded successfully: %s", game_path);
        }
    } else {
        LOG_SYSTEM_WARN("[SYSTEM] No game disc specified. Running BIOS-only mode.");
        interconnect_state.cdrom.disc_present = false;
        interconnect_state.cdrom.drive_state = DRIVE_IDLE;
        LOG_SYSTEM_INFO("[SYSTEM] CD-ROM initialized in no-disc state for BIOS menu.");
    }


    // 6. Initialize CPU (pass it the fully connected interconnect)
    LOG_SYSTEM_DEBUG("[SYSTEM]   Initializing CPU...");
    cpu_init(&cpu_state, &interconnect_state);

    // 7. Set CPU pointer in interconnect for direct exception triggering
    interconnect_set_cpu(&interconnect_state, &cpu_state);

    // 6.5 Boot EXE Injection (HLE BootEXE mode, bypasses BIOS entirely)
    if (exe_path != NULL) {
        // Let BIOS initialize kernel and jump to shell entry before sideloading.
        // This mirrors common emulator behavior and avoids missing BIOS init state.
        const uint32_t shell_entry_pc = 0x80030000;
        const uint64_t max_warmup_instructions = 20000000ULL;
        uint64_t warmup_count = 0;
        while (cpu_state.pc != shell_entry_pc && warmup_count < max_warmup_instructions) {
            cpu_run_next_instruction(&cpu_state);
            warmup_count++;
        }
        if (cpu_state.pc == shell_entry_pc) {
            LOG_SYSTEM_INFO("[SYSTEM] BootEXE warmup reached BIOS shell entry after %llu instructions @ 0x%08x",
                            shell_entry_pc, (unsigned long long)warmup_count);
        } else {
            LOG_SYSTEM_WARN("[SYSTEM] BootEXE warmup timeout before BIOS shell entry (last @ 0x%08x", cpu_state.pc);
        }

        LOG_SYSTEM_INFO("[SYSTEM] === BootEXE Mode (HLE) ===");
        LOG_SYSTEM_INFO("[SYSTEM] Loading EXE directly: %s", exe_path);

        FILE *exe_file = fopen(exe_path, "rb");
        if (exe_file == NULL) {
            LOG_SYSTEM_ERROR("[SYSTEM] Failed to open EXE file: %s", exe_path);
            SDL_GL_DeleteContext(gl_context);
            SDL_DestroyWindow(window);
            SDL_Quit();
            return 1;
        }

        PSEXEHeader exe_header;
        size_t header_read = fread(&exe_header, 1, sizeof(exe_header), exe_file);
        if (header_read < sizeof(exe_header)) {
            LOG_SYSTEM_ERROR("[SYSTEM] EXE file too small (got %zu bytes, need 2048)", header_read);
            fclose(exe_file);
            SDL_GL_DeleteContext(gl_context);
            SDL_DestroyWindow(window);
            SDL_Quit();
            return 1;
        }

        if (memcmp(exe_header.id, "PS-X EXE", 8) != 0) {
            LOG_SYSTEM_ERROR("[SYSTEM] Invalid PSX EXE magic signature");
            fclose(exe_file);
            SDL_GL_DeleteContext(gl_context);
            SDL_DestroyWindow(window);
            SDL_Quit();
            return 1;
        }

        uint32_t entry_point = exe_header.initial_pc;
        uint32_t initial_gp = exe_header.initial_gp;
        uint32_t load_address = exe_header.load_address;
        uint32_t file_size = exe_header.file_size;
        uint32_t memfill_start = exe_header.memfill_start;
        uint32_t memfill_size = exe_header.memfill_size;
        uint32_t initial_sp_base = exe_header.initial_sp_base;
        uint32_t initial_sp_offset = exe_header.initial_sp_offset;

        LOG_SYSTEM_INFO("[SYSTEM] PSX EXE Header:");
        LOG_SYSTEM_INFO("[SYSTEM]   Entry Point: 0x%08x", entry_point);
        LOG_SYSTEM_INFO("[SYSTEM]   Initial GP: 0x%08x", initial_gp);
        LOG_SYSTEM_INFO("[SYSTEM]   Load Address: 0x%08x", load_address);
        LOG_SYSTEM_INFO("[SYSTEM]   File Size: %u bytes", file_size);

        if (memfill_size > 0) {
            uint32_t memfill_offset = memfill_start & 0x1FFFFF;
            uint32_t memfill_bytes = memfill_size & ~3u;
            if (memfill_offset + memfill_bytes > 0x200000) {
                LOG_SYSTEM_ERROR("[SYSTEM] Invalid EXE memfill range: 0x%08x + %u", memfill_start, memfill_size);
                fclose(exe_file);
                SDL_GL_DeleteContext(gl_context);
                SDL_DestroyWindow(window);
                SDL_Quit();
                return 1;
            }
            memset((uint8_t*)interconnect_state.ram->data + memfill_offset, 0, memfill_bytes);
            LOG_SYSTEM_INFO("[SYSTEM] Memfill cleared: 0x%08x + %u", memfill_start, memfill_bytes);
        }

        if (file_size > 0) {
            if (fseek(exe_file, 0x800, SEEK_SET) != 0) {
                LOG_SYSTEM_ERROR("[SYSTEM] Failed to seek EXE payload");
                fclose(exe_file);
                SDL_GL_DeleteContext(gl_context);
                SDL_DestroyWindow(window);
                SDL_Quit();
                return 1;
            }

            if (load_address < 0x80000000 || load_address + file_size > 0x80200000) {
                LOG_SYSTEM_ERROR("[SYSTEM] Invalid EXE load address range: 0x%08x + %u", load_address, file_size);
                fclose(exe_file);
                SDL_GL_DeleteContext(gl_context);
                SDL_DestroyWindow(window);
                SDL_Quit();
                return 1;
            }

            uint32_t ram_offset = load_address & 0x1FFFFF;
            uint8_t *ram_ptr = (uint8_t*)interconnect_state.ram->data + ram_offset;

            size_t bytes_read = fread(ram_ptr, 1, file_size, exe_file);
            if (bytes_read != file_size) {
                LOG_SYSTEM_ERROR("[SYSTEM] Failed to read EXE data: got %zu/%u bytes", bytes_read, file_size);
                fclose(exe_file);
                SDL_GL_DeleteContext(gl_context);
                SDL_DestroyWindow(window);
                SDL_Quit();
                return 1;
            }

            LOG_SYSTEM_INFO("[SYSTEM] Loaded %u bytes at RAM[0x%08x]", file_size, ram_offset);
        }

        cpu_state.out_regs[28] = initial_gp;
        cpu_state.out_regs[29] = initial_sp_base + initial_sp_offset;
        cpu_state.out_regs[30] = cpu_state.out_regs[29];
        cpu_state.regs[28] = cpu_state.out_regs[28];
        cpu_state.regs[29] = cpu_state.out_regs[29];
        cpu_state.regs[30] = cpu_state.out_regs[30];
        cpu_state.pc = entry_point;
        cpu_state.next_pc = entry_point + 4;
        cpu_state.current_pc = entry_point;
        LOG_SYSTEM_WARN("[SYSTEM] CPU PC set to EXE entry point: 0x%08x (bypassing BIOS)", entry_point);

        fclose(exe_file);
    }

    // 8. Initialize Controller (keyboard input) <<< ADDED
    LOG_SYSTEM_DEBUG("[SYSTEM]   Initializing Controller...");
    controller_init(&gamepad);
    sio_set_controller_connected(&interconnect_state.sio, true);  // Enable in SIO

    // --- Schedule Initial Events (using new event system) ---
    #define VBLANK_CYCLES 564480
    // Only schedule VBlank event at startup. Timer0 events are scheduled by timer logic when needed.
    eventq_schedule(&interconnect_state, EVQ_VBLANK, VBLANK_CYCLES);
    // PCSX ReARMed-style: Schedule Timer0 event at boot so it is always in the event queue
    eventq_schedule(&interconnect_state, EVQ_TIMER0, 1024); // Initial guess: 1024 cycles

    LOG_SYSTEM_WARN("[SYSTEM] All Emulator Components Initialized.");

    // --- Main Emulation Loop ---
    LOG_SYSTEM_WARN("[SYSTEM] === Main emulation loop starting ===");
    bool should_quit = false;
    SDL_Event event;
    uint64_t total_cycles = 0;

    const uint64_t frame_ticks = (uint64_t)((double)SDL_GetPerformanceFrequency() / 60.0);
    uint64_t next_frame_tick = SDL_GetPerformanceCounter() + frame_ticks;

    while (!should_quit) {
        // --- Handle Input/Window Events ---
        while (SDL_PollEvent(&event)) {
            debug_ui_process_event(&event);
            if (event.type == SDL_QUIT) {
                LOG_SYSTEM_INFO("[SYSTEM] SDL_QUIT event received.");
                should_quit = true;
            } else if (event.type == SDL_KEYDOWN) {
                if (event.key.keysym.sym == SDLK_ESCAPE) {
                    LOG_SYSTEM_INFO("[SYSTEM] Escape key pressed. Quitting.");
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
        Debugger* dbg = &interconnect_state.debugger;

        if (!dbg->paused) {
            while (cycles_run < cycles_per_frame) {
                uint32_t cycles_remaining = cycles_per_frame - cycles_run;
                uint32_t to_next_event = eventq_cycles_until_next(&interconnect_state);
                uint32_t run_chunk = (to_next_event == 0) ? 1 : to_next_event;
                if (run_chunk > cycles_remaining) run_chunk = cycles_remaining;

                for (uint32_t i = 0; i < run_chunk; ++i) {
                    cpu_run_next_instruction(&cpu_state);
                    if (dbg->paused) goto emulation_paused;
                }

                timers_step(&interconnect_state.timers_state, run_chunk);
                spu_step(&interconnect_state.spu, run_chunk);
                cycles_run += run_chunk;
            }
        } else if (debug_ui_step_requested()) {
            // Single-step: execute exactly one instruction from the current PC
            dbg->step_skip_bp = true;
            dbg->paused = false;
            cpu_run_next_instruction(&cpu_state);
            dbg->paused = true;
        }
        emulation_paused:;

        // --- Render and Display Frame ---
        // Flush any remaining primitives to the GPU
        renderer_draw(&interconnect_state.gpu.renderer);
        
        // --- Render Debug UI to Main Window ---
        // Bind default framebuffer (0) for the main SDL window
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f); // Dark grey background for the ImGui host window
        glClear(GL_COLOR_BUFFER_BIT);

        // Render Debug UI
        debug_ui_render(&cpu_state, &interconnect_state);

        // Swap buffers to display the frame
        SDL_GL_SwapWindow(window);
        check_gl_error("After SwapWindow");
        
        // Re-bind the display FBO so PSX rendering goes back to the texture!
        glBindFramebuffer(GL_FRAMEBUFFER, interconnect_state.gpu.renderer.display_fbo);

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
    LOG_SYSTEM_INFO("[SYSTEM] Emulation loop finished. Cleaning up...");
    
    debug_ui_shutdown();
    cdrom_audio_sdl_close();
    cdrom_eject_disc(&interconnect_state.cdrom);
    renderer_destroy(&interconnect_state.gpu.renderer);
    LOG_SYSTEM_INFO("[SYSTEM] Destroying SDL GL Context and Window...");
    SDL_GL_DeleteContext(gl_context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    LOG_SYSTEM_INFO("[SYSTEM] SDL Quit.");

    LOG_SYSTEM_INFO("[SYSTEM] --- ZoniStation One Emulator Finished ---");
    LOG_SYSTEM_INFO("[SYSTEM] Emulator stopped");
    return 0;
}