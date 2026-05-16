#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include <SDL2/SDL.h>
#define GLEW_STATIC
#include <GL/glew.h>

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

#define VBLANK_CYCLES 564480
#define CYCLES_PER_FRAME (33868800 / 60)

// --- Argument config ---
typedef struct {
    const char* bios_path;
    const char* game_path;
    const char* exe_path;
} EmuArgs;

static bool parse_args(int argc, char** argv, EmuArgs* out) {
    out->bios_path = NULL;
    out->game_path = NULL;
    out->exe_path  = NULL;

    for (int i = 1; i < argc; ++i) {
        if      (strncmp(argv[i], "--game=", 7) == 0) out->game_path = argv[i] + 7;
        else if (strncmp(argv[i], "--exe=",  6) == 0) out->exe_path  = argv[i] + 6;
        else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("Usage: %s [--game=<path>] [--exe=<path>] <BIOS_PATH>\n", argv[0]);
            return false;
        } else if (!out->bios_path) {
            out->bios_path = argv[i];
        } else {
            fprintf(stderr, "Unknown argument: %s\n", argv[i]);
            return false;
        }
    }
    if (!out->bios_path) out->bios_path = "roms/SCPH1001.BIN";
    return true;
}

// --- SDL / OpenGL window init ---
typedef struct { SDL_Window* win; SDL_GLContext ctx; } SdlCtx;

static bool init_sdl(SdlCtx* out) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
        LOG_SYSTEM_ERROR("[SYSTEM] SDL_Init: %s", SDL_GetError());
        return false;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    out->win = SDL_CreateWindow("ZoniStation One",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        1024, 512, SDL_WINDOW_OPENGL);
    if (!out->win) {
        LOG_SYSTEM_ERROR("[SYSTEM] SDL_CreateWindow: %s", SDL_GetError());
        SDL_Quit();
        return false;
    }
    out->ctx = SDL_GL_CreateContext(out->win);
    if (!out->ctx) {
        LOG_SYSTEM_ERROR("[SYSTEM] SDL_GL_CreateContext: %s", SDL_GetError());
        SDL_DestroyWindow(out->win);
        SDL_Quit();
        return false;
    }
    glewExperimental = GL_TRUE;
    GLenum err = glewInit();
    if (err != GLEW_OK) {
        LOG_SYSTEM_ERROR("[SYSTEM] GLEW init: %s", glewGetErrorString(err));
        SDL_GL_DeleteContext(out->ctx);
        SDL_DestroyWindow(out->win);
        SDL_Quit();
        return false;
    }
    LOG_SYSTEM_INFO("[SYSTEM] OpenGL %s", glGetString(GL_VERSION));
    check_gl_error("GLEW init");
    return true;
}

static void shutdown_sdl(SdlCtx* s) {
    SDL_GL_DeleteContext(s->ctx);
    SDL_DestroyWindow(s->win);
    SDL_Quit();
}

// --- PS-X EXE loader ---
static bool load_exe(const char* path, Cpu* cpu, Interconnect* inter) {
    // Warm-up: let BIOS init kernel before sideloading
    const uint32_t shell_pc = 0x80030000;
    uint64_t warmup = 0;
    while (cpu->pc != shell_pc && warmup < 20000000ULL) {
        cpu_run_next_instruction(cpu);
        warmup++;
    }
    if (cpu->pc == shell_pc)
        LOG_SYSTEM_INFO("[SYSTEM] BootEXE warmup done after %llu insns", (unsigned long long)warmup);
    else
        LOG_SYSTEM_WARN("[SYSTEM] BootEXE warmup timed out at PC=0x%08x", cpu->pc);

    FILE* f = fopen(path, "rb");
    if (!f) { LOG_SYSTEM_ERROR("[SYSTEM] Cannot open EXE: %s", path); return false; }

    PSEXEHeader hdr;
    if (fread(&hdr, 1, sizeof(hdr), f) < sizeof(hdr)) {
        LOG_SYSTEM_ERROR("[SYSTEM] EXE too small"); fclose(f); return false;
    }
    if (memcmp(hdr.id, "PS-X EXE", 8) != 0) {
        LOG_SYSTEM_ERROR("[SYSTEM] Invalid PS-X EXE magic"); fclose(f); return false;
    }

    if (hdr.memfill_size > 0) {
        uint32_t off = hdr.memfill_start & 0x1FFFFF;
        uint32_t len = hdr.memfill_size & ~3u;
        if (off + len <= 0x200000)
            memset((uint8_t*)inter->ram->data + off, 0, len);
    }

    if (hdr.file_size > 0) {
        if (fseek(f, 0x800, SEEK_SET) != 0 ||
            hdr.load_address < 0x80000000 ||
            hdr.load_address + hdr.file_size > 0x80200000) {
            LOG_SYSTEM_ERROR("[SYSTEM] Invalid EXE load range"); fclose(f); return false;
        }
        uint32_t off = hdr.load_address & 0x1FFFFF;
        size_t got = fread((uint8_t*)inter->ram->data + off, 1, hdr.file_size, f);
        if (got != hdr.file_size) {
            LOG_SYSTEM_ERROR("[SYSTEM] EXE read short: %zu/%u", got, hdr.file_size);
            fclose(f); return false;
        }
        LOG_SYSTEM_INFO("[SYSTEM] EXE loaded: %u bytes @ 0x%08x", hdr.file_size, hdr.load_address);
    }
    fclose(f);

    uint32_t sp = hdr.initial_sp_base + hdr.initial_sp_offset;
    cpu->out_regs[28] = hdr.initial_gp;
    cpu->out_regs[29] = sp;
    cpu->out_regs[30] = sp;
    cpu->regs[28]     = hdr.initial_gp;
    cpu->regs[29]     = sp;
    cpu->regs[30]     = sp;
    cpu->pc           = hdr.initial_pc;
    cpu->next_pc      = hdr.initial_pc + 4;
    cpu->current_pc   = hdr.initial_pc;
    LOG_SYSTEM_INFO("[SYSTEM] EXE entry: 0x%08x", hdr.initial_pc);
    return true;
}

// --- TTY keyboard injection ---
static void inject_tty_keys(Interconnect* inter) {
    const uint8_t* keys = SDL_GetKeyboardState(NULL);
    static bool prev[SDL_NUM_SCANCODES];

    static const struct { SDL_Scancode sc; char ch; } map[] = {
        { SDL_SCANCODE_W,      'w'  },
        { SDL_SCANCODE_S,      's'  },
        { SDL_SCANCODE_D,      'd'  },
        { SDL_SCANCODE_A,      'a'  },
        { SDL_SCANCODE_SPACE,  '\n' },
        { SDL_SCANCODE_RETURN, '\n' },
        { SDL_SCANCODE_E,      'e'  },
        { SDL_SCANCODE_C,      'c'  },
        { SDL_SCANCODE_X,      'x'  },
        { SDL_SCANCODE_Z,      'z'  },
    };
    for (size_t i = 0; i < sizeof(map)/sizeof(map[0]); ++i)
        if (keys[map[i].sc] && !prev[map[i].sc])
            interconnect_tty_input_add(inter, map[i].ch);
    memcpy(prev, keys, sizeof(prev));
}

// --- Main ---
int main(int argc, char* argv[]) {
    EmuArgs args;
    if (!parse_args(argc, argv, &args)) return 1;

    log_init();
    LOG_SYSTEM_INFO("[SYSTEM] ZoniStation One starting — BIOS: %s", args.bios_path);

    SdlCtx sdl;
    if (!init_sdl(&sdl)) return 1;

    debug_ui_init(sdl.win, sdl.ctx);

    // --- Component init ---
    Bios       bios;
    Ram        ram;
    Interconnect inter;
    Cpu        cpu;
    Controller gamepad;

    ram_init(&ram);

    if (!bios_load(&bios, args.bios_path)) { shutdown_sdl(&sdl); return 1; }
    bios_print_bootstrap_strings(&bios);

    interconnect_init(&inter, &bios, &ram);

    if (!renderer_init(&inter.gpu.renderer)) {
        LOG_SYSTEM_ERROR("[SYSTEM] Renderer init failed");
        shutdown_sdl(&sdl);
        return 1;
    }

    cdrom_audio_sdl_open(&inter.cdrom.audio_fifo);
    cdrom_audio_set_spu(&inter.spu);

    if (args.game_path) {
        if (!cdrom_load_disc(&inter.cdrom, args.game_path))
            LOG_SYSTEM_WARN("[SYSTEM] Disc load failed: %s — BIOS-only mode", args.game_path);
        else
            LOG_SYSTEM_INFO("[SYSTEM] Disc loaded: %s", args.game_path);
    } else {
        inter.cdrom.disc_present  = false;
        inter.cdrom.drive_state   = DRIVE_IDLE;
        LOG_SYSTEM_INFO("[SYSTEM] No disc — BIOS-only mode");
    }

    cpu_init(&cpu, &inter);
    interconnect_set_cpu(&inter, &cpu);

    if (args.exe_path && !load_exe(args.exe_path, &cpu, &inter)) {
        shutdown_sdl(&sdl);
        return 1;
    }

    controller_init(&gamepad);
    sio_set_controller_connected(&inter.sio, true);

    eventq_schedule(&inter, EVQ_VBLANK, VBLANK_CYCLES);
    eventq_schedule(&inter, EVQ_TIMER0, 1024);
    eventq_schedule(&inter, EVQ_SPU, CPU_TICKS_PER_SPU_TICK);
    LOG_SPU_INFO("[SPU] EVQ_SPU scheduled at 44100 Hz (768 cy/sample)");

    LOG_SYSTEM_INFO("[SYSTEM] All components initialised — starting loop");

    // --- Main loop ---
    bool quit = false;
    SDL_Event ev;

    const uint64_t frame_ticks = (uint64_t)((double)SDL_GetPerformanceFrequency() / 60.0);
    uint64_t next_frame = SDL_GetPerformanceCounter() + frame_ticks;

    while (!quit) {
        while (SDL_PollEvent(&ev)) {
            debug_ui_process_event(&ev);
            if (ev.type == SDL_QUIT) quit = true;
            else if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE) quit = true;
        }

        sio_set_button_state(&inter.sio, controller_update_from_keyboard(&gamepad));
        inject_tty_keys(&inter);

        Debugger* dbg = &inter.debugger;
        if (!dbg->paused) {
            uint32_t cycles_run = 0;
            while (cycles_run < CYCLES_PER_FRAME) {
                uint32_t left  = CYCLES_PER_FRAME - cycles_run;
                uint32_t chunk = eventq_cycles_until_next(&inter);
                if (chunk == 0) chunk = 1;
                if (chunk > left) chunk = left;

                for (uint32_t i = 0; i < chunk; ++i) {
                    cpu_run_next_instruction(&cpu);
                    if (dbg->paused) goto frame_done;
                }
                timers_step(&inter.timers_state, chunk);
                cycles_run += chunk;
            }
        } else if (debug_ui_step_requested()) {
            dbg->step_skip_bp = true;
            dbg->paused = false;
            cpu_run_next_instruction(&cpu);
            dbg->paused = true;
        }
        frame_done:;

        renderer_draw(&inter.gpu.renderer);

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        debug_ui_render(&cpu, &inter);
        SDL_GL_SwapWindow(sdl.win);
        check_gl_error("SwapWindow");
        glBindFramebuffer(GL_FRAMEBUFFER, inter.gpu.renderer.display_fbo);

        // 60 Hz cap
        uint64_t now = SDL_GetPerformanceCounter();
        if (now < next_frame) {
            uint64_t ms = ((next_frame - now) * 1000ULL) / SDL_GetPerformanceFrequency();
            if (ms > 1) SDL_Delay((uint32_t)(ms - 1));
            while (SDL_GetPerformanceCounter() < next_frame) {}
            next_frame += frame_ticks;
        } else {
            next_frame = now + frame_ticks;
        }
    }

    // --- Cleanup ---
    debug_ui_shutdown();
    cdrom_audio_sdl_close();
    cdrom_eject_disc(&inter.cdrom);
    renderer_destroy(&inter.gpu.renderer);
    shutdown_sdl(&sdl);
    LOG_SYSTEM_INFO("[SYSTEM] Shutdown complete");
    return 0;
}
