#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>

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
#include "lua_debug.h"
#include "system.h"

/* From debug_ui.cpp — returns ImDrawData* after ImGui::Render() */
extern void* debug_ui_get_draw_data(void);

/* Emulated-frame length and host frame pacing both follow the GPU's video mode
 * (gpu_cycles_per_frame): 566203 cy / 59.82 Hz NTSC, 680823 cy / 49.75 Hz PAL.
 * Both were previously pinned to NTSC 60 Hz, which ran PAL discs ~20% fast.
 * PSX_SYSCLK_HZ comes from cdrom_disc.h. */

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
    /* Resizable so the window manager offers a maximise button; maximised
     * after the GL context is up (see below). Alt+Enter toggles fullscreen. */
    out->win = SDL_CreateWindow("ZoniStation One",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        1280, 720, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
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

// --- SDL2 audio ---
static Spu* g_spu_for_audio = NULL;
static SDL_AudioDeviceID g_audio_dev = 0;

// --- Exec trace on forced shutdown ---
static const Cpu* g_cpu_for_trace = NULL;

static void sighandler_dump_trace(int sig) {
    (void)sig;
    if (g_cpu_for_trace) cpu_dump_exec_trace(g_cpu_for_trace, "logs/exec_trace.log");
    _exit(1);
}

static void audio_callback(void* userdata, Uint8* stream, int len) {
    (void)userdata;
    int num_samples = len / (2 * sizeof(int16_t)); // stereo int16
    if (g_spu_for_audio)
        spu_fill_audio(g_spu_for_audio, (int16_t*)stream, num_samples);
    else
        memset(stream, 0, (size_t)len);
}

static void audio_init(Spu* spu) {
    g_spu_for_audio = spu;
    SDL_AudioSpec want = {0}, got = {0};
    want.freq     = 44100;
    want.format   = AUDIO_S16SYS;
    want.channels = 2;
    want.samples  = 512;
    want.callback = audio_callback;
    g_audio_dev = SDL_OpenAudioDevice(NULL, 0, &want, &got, 0);
    if (g_audio_dev == 0) {
        LOG_SYSTEM_WARN("[SYSTEM] SDL audio open failed: %s — no sound", SDL_GetError());
        return;
    }
    LOG_SYSTEM_INFO("[SYSTEM] Audio: %d Hz ch=%d fmt=0x%x buf=%d",
                    got.freq, got.channels, got.format, got.samples);
    SDL_PauseAudioDevice(g_audio_dev, 0);
}

static void audio_shutdown(void) {
    if (g_audio_dev) {
        SDL_CloseAudioDevice(g_audio_dev);
        g_audio_dev = 0;
    }
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
    if (getenv("ZS1_LOG_STDERR")) log_set_stderr_quiet(false);
    if (getenv("ZS1_LOG_TRACE"))  log_set_level(LOG_LEVEL_TRACE);
    /* ZS1_LOG_LEVEL=silent|error|warn|info|debug|trace — the level is a real
     * performance knob (DEBUG formats thousands of lines per FMV frame), so it
     * is settable per run instead of only at compile time. */
    {
        const char* lvl = getenv("ZS1_LOG_LEVEL");
        if (lvl) {
            if      (!strcmp(lvl, "silent")) log_set_level(LOG_LEVEL_SILENT);
            else if (!strcmp(lvl, "error"))  log_set_level(LOG_LEVEL_ERROR);
            else if (!strcmp(lvl, "warn"))   log_set_level(LOG_LEVEL_WARN);
            else if (!strcmp(lvl, "info"))   log_set_level(LOG_LEVEL_INFO);
            else if (!strcmp(lvl, "debug"))  log_set_level(LOG_LEVEL_DEBUG);
            else if (!strcmp(lvl, "trace"))  log_set_level(LOG_LEVEL_TRACE);
        }
    }
    LOG_SYSTEM_INFO("[SYSTEM] ZoniStation One starting — BIOS: %s", args.bios_path);

    SdlCtx sdl;
    if (!init_sdl(&sdl)) return 1;

    debug_ui_init(sdl.win, sdl.ctx);

    // --- Component init ---
    // static: these structs are multi-MB (Bios 512KB, Ram 2MB, Interconnect ~3.4MB) —
    // too large for the 8MB default thread stack alongside the rest of main()'s frame
    // and its callees; a single process-lifetime instance of each, so static (BSS) is
    // the natural home, not the stack (and not heap, per this project's no-malloc convention).
    static Bios       bios;
    static Ram        ram;
    static Interconnect inter;
    static Cpu        cpu;
    Controller gamepad;

    ram_init(&ram);

    if (!bios_load(&bios, args.bios_path)) { shutdown_sdl(&sdl); return 1; }

    interconnect_init(&inter, &bios, &ram);

    if (!renderer_init(&inter.gpu.renderer)) {
        LOG_SYSTEM_ERROR("[SYSTEM] Renderer init failed");
        shutdown_sdl(&sdl);
        return 1;
    }
    /* Release GL context from main thread — GPU thread will acquire it */
    SDL_GL_MakeCurrent(sdl.win, NULL);
    renderer_start_gpu_thread(&inter.gpu.renderer, sdl.win, sdl.ctx);

    /* Fill the screen while keeping the titlebar's minimise/maximise/close
     * buttons. Window-manager call only — no GL. */
    SDL_MaximizeWindow(sdl.win);

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
    lua_debug_init(&inter, &cpu);
    if (getenv("ZS1_LUA_SCRIPT")) lua_debug_run_file(getenv("ZS1_LUA_SCRIPT"));

    if (args.exe_path && !load_exe(args.exe_path, &cpu, &inter)) {
        shutdown_sdl(&sdl);
        return 1;
    }

    controller_init(&gamepad);
    sio_set_controller_connected(&inter.sio, true);

    audio_init(&inter.spu);

    system_init(&inter, &cpu);

    g_cpu_for_trace = &cpu;
    signal(SIGINT,  sighandler_dump_trace);
    signal(SIGTERM, sighandler_dump_trace);
    signal(SIGABRT, sighandler_dump_trace);

    LOG_SYSTEM_INFO("[SYSTEM] All components initialised — starting loop");

    /* Machine-bar identity for the debug UI: the two paths' basenames. */
    {
        const char* bn = args.bios_path;
        const char* dn = args.game_path ? args.game_path : "n/a";
        for (const char* p = args.bios_path; *p; p++)
            if (*p == '/' || *p == '\\') bn = p + 1;
        if (args.game_path)
            for (const char* p = args.game_path; *p; p++)
                if (*p == '/' || *p == '\\') dn = p + 1;
        debug_ui_set_machine_info(bn, dn);
    }

    // --- Main loop ---
    bool quit = false;
    SDL_Event ev;

    /* Live vitals for the machine bar. Cheap: two perf-counter reads and one
     * SPU sample delta per frame, no logging — the instrumentation that once
     * produced a bogus speed figure was the logging, not the counters. */
    uint64_t vit_prev         = SDL_GetPerformanceCounter();
    uint32_t vit_prev_samples = inter.spu.total_samples_generated;
    double   vit_drift_ema    = 0.0;

    /* Re-derived each frame: a game may switch video mode via GP1(08) at any
     * point (and the BIOS boots NTSC before a PAL disc's shell switches it). */
    uint32_t cycles_per_frame = gpu_cycles_per_frame(&inter.gpu);
    uint64_t frame_ticks = (uint64_t)((double)SDL_GetPerformanceFrequency() *
                                      (double)cycles_per_frame / (double)PSX_SYSCLK_HZ);
    uint64_t next_frame = SDL_GetPerformanceCounter() + frame_ticks;

    while (!quit) {
        while (SDL_PollEvent(&ev)) {
            debug_ui_process_event(&ev);
            if (ev.type == SDL_QUIT) quit = true;
            else if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE) quit = true;
            /* Alt+Enter toggles borderless desktop fullscreen. */
            else if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_RETURN &&
                     (ev.key.keysym.mod & KMOD_ALT)) {
                Uint32 fs = SDL_GetWindowFlags(sdl.win) & SDL_WINDOW_FULLSCREEN_DESKTOP;
                SDL_SetWindowFullscreen(sdl.win, fs ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
            }
        }

        sio_set_button_state(&inter.sio, controller_update_from_keyboard(&gamepad));
        inject_tty_keys(&inter);

        /* Wait for GPU to finish the previous frame's rendering. */
        renderer_wait_frame_done(&inter.gpu.renderer);

        /* Host-side frame pacing budget follows the GPU's video mode. */
        cycles_per_frame = gpu_cycles_per_frame(&inter.gpu);
        frame_ticks = (uint64_t)((double)SDL_GetPerformanceFrequency() *
                                 (double)cycles_per_frame / (double)PSX_SYSCLK_HZ);

        /* ZS1_FRAME_PROFILE=1: where the frame's wall-clock time actually goes.
         * The emulator has to deliver 44100 audio samples per real second, so
         * any millisecond spent outside emulation is a millisecond of audio the
         * device will not get. */
        static int s_prof = -1;
        if (s_prof < 0) s_prof = getenv("ZS1_FRAME_PROFILE") ? 1 : 0;
        uint64_t t_freq = SDL_GetPerformanceFrequency(), t0 = 0, t1 = 0, t2 = 0, t3 = 0, t4 = 0;
        if (s_prof) t0 = SDL_GetPerformanceCounter();

        /* Run the machine for one video frame (or one debugger step). */
        system_run_frame(&inter, &cpu);
        if (s_prof) t1 = SDL_GetPerformanceCounter();

        /* Update machine-bar vitals from the counters we already hold. Wall
         * time is the full loop iteration (work + pacing), so Speed reads 100%
         * when the emulator keeps real time and drops below it when it cannot. */
        {
            uint64_t vit_now  = SDL_GetPerformanceCounter();
            double   vit_freq = (double)SDL_GetPerformanceFrequency();
            double   frame_ms = (double)(vit_now - vit_prev) * 1000.0 / vit_freq;
            double   budget_ms = (double)cycles_per_frame * 1000.0 / (double)PSX_SYSCLK_HZ;
            uint32_t cur_samples = inter.spu.total_samples_generated;
            uint32_t produced = cur_samples - vit_prev_samples;
            double   drift = 0.0;
            double   wall_s = frame_ms / 1000.0;
            if (wall_s > 0.0005) {
                double expected = 44100.0 * wall_s;   /* device consumption rate */
                if (expected > 1.0) drift = ((double)produced / expected - 1.0) * 100.0;
            }
            vit_drift_ema = vit_drift_ema * 0.9 + drift * 0.1;
            debug_ui_set_vitals(frame_ms, budget_ms, spu_ring_used(&inter.spu),
                                SPU_RING_TARGET_SAMPLES, vit_drift_ema);
            vit_prev = vit_now;
            vit_prev_samples = cur_samples;
        }

        /* Build ImGui for this frame (SDL + widget code, no GL) */
        debug_ui_render(&cpu, &inter);

        /* Full VRAM upload at end of frame — ensures vram_texture matches vram.data even when
         * GP0(A0) sprite loads cleared vram_dirty (preventing upload_vram_if_dirty in draw cmds).
         * Processed BEFORE draw batches in GPU thread so all sprite/CLUT data is current. */
        renderer_upload_vram(&inter.gpu.renderer, (const uint16_t*)inter.gpu.vram.data);
        if (s_prof) t2 = SDL_GetPerformanceCounter();

        /* Snapshot VRAM into viewer texture before submitting. Converting the
         * whole 1024x512 buffer to RGBA8 costs 2 MB of staging pool per frame,
         * so only pay it while the viewer window is actually open. */
        if (debug_ui_vram_viewer_open())
            renderer_update_vram_viewer(&inter.gpu.renderer, (const uint8_t*)inter.gpu.vram.data);
        if (s_prof) t3 = SDL_GetPerformanceCounter();

        /* Submit frame to GPU thread: swap buffers, wake renderer */
        renderer_submit_frame(&inter.gpu.renderer, debug_ui_get_draw_data());
        if (s_prof) {
            t4 = SDL_GetPerformanceCounter();
            extern uint64_t g_spu_gen_ticks;
            static int frames = 0; static double a_emu, a_up, a_view, a_sub, a_spu;
            a_spu += (double)g_spu_gen_ticks * 1000.0 / (double)t_freq; g_spu_gen_ticks = 0;
            a_emu  += (double)(t1 - t0) * 1000.0 / (double)t_freq;
            a_up   += (double)(t2 - t1) * 1000.0 / (double)t_freq;
            a_view += (double)(t3 - t2) * 1000.0 / (double)t_freq;
            a_sub  += (double)(t4 - t3) * 1000.0 / (double)t_freq;
            if (++frames == 60) {
                LOG_SYSTEM_INFO("[PROF] per frame: emu=%.2fms (spu %.2fms) vram_upload=%.2fms viewer=%.2fms submit=%.2fms total=%.2fms",
                                a_emu / 60, a_spu / 60, a_up / 60, a_view / 60, a_sub / 60,
                                (a_emu + a_up + a_view + a_sub) / 60);
                frames = 0; a_emu = a_up = a_view = a_sub = a_spu = 0;
            }
        }

        /* Pacing.
         *
         * With an audio device open the sound queue is the clock: the emulator
         * runs ahead only until the SPU ring holds SPU_RING_TARGET_SAMPLES, then
         * waits for the device to drain some. Production and consumption are
         * then matched by construction — the emulator cannot outrun the device
         * (dropped samples) or fall behind it (silence in the callback), and
         * frame-pacing jitter never reaches the audio.
         *
         * Without a device there is nothing to synchronise to, so fall back to
         * pacing frames against the emulated refresh rate. */
        if (g_audio_dev) {
            while (!quit && spu_ring_used(&inter.spu) > SPU_RING_TARGET_SAMPLES)
                SDL_Delay(1);
            next_frame = SDL_GetPerformanceCounter() + frame_ticks;
        } else {
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
    }

    // --- Cleanup ---
    cpu_dump_exec_trace(&cpu, "logs/exec_trace.log");
    audio_shutdown();
    renderer_stop_gpu_thread(&inter.gpu.renderer);
    /* Re-acquire GL context for cleanup calls (destroy, ImGui shutdown) */
    SDL_GL_MakeCurrent(sdl.win, sdl.ctx);
    debug_ui_shutdown();
    lua_debug_shutdown();
    cdrom_eject_disc(&inter.cdrom);
    renderer_destroy(&inter.gpu.renderer);
    shutdown_sdl(&sdl);
    LOG_SYSTEM_INFO("[SYSTEM] Shutdown complete");
    return 0;
}
