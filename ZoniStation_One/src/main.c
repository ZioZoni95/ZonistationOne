#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <SDL2/SDL.h>

#include "zonistation_common.h"
#include "zonistation_config.h"
#include "zonistation_core.h"

// Global variables
static zs_core_t* g_core = NULL;
static zs_bool g_running = ZS_TRUE;

// Signal handler for graceful shutdown
static void signal_handler(int signal) {
    ZS_LOG_INFO("Received signal %d, shutting down...", signal);
    g_running = ZS_FALSE;
}

// Print usage information
static void print_usage(const char* program_name) {
    printf("ZoniStation One - PlayStation One Emulator v%s\n\n", ZONISTATION_VERSION);
    printf("Usage: %s [options] <game_file>\n\n", program_name);
    printf("Options:\n");
    printf("  --bios <file>           Specify BIOS file\n");
    printf("  --memcard <file>        Specify memory card file\n");
    printf("  --gpu <plugin>          Select GPU plugin\n");
    printf("  --spu <plugin>          Select SPU plugin\n");
    printf("  --input <plugin>        Select input plugin\n");
    printf("  --cdrom <plugin>        Select CD-ROM plugin\n");
    printf("  --config <file>         Load configuration from file\n");
    printf("  --region <ntsc|pal|ntsc-j> Set region\n");
    printf("  --cpu-mode <interpreter|dynarec> Set CPU mode\n");
    printf("  --width <pixels>        Set screen width\n");
    printf("  --height <pixels>       Set screen height\n");
    printf("  --fullscreen            Start in fullscreen mode\n");
    printf("  --no-vsync              Disable vertical sync\n");
    printf("  --frame-limit <fps>     Set frame rate limit\n");
    printf("  --log-level <level>     Set log level (error|warn|info|debug|trace)\n");
    printf("  --debug                 Enable debug mode\n");
    printf("  --help                  Show this help message\n");
    printf("  --version               Show version information\n\n");
    printf("Supported file formats:\n");
    printf("  - .bin/.cue (CD image)\n");
    printf("  - .iso (ISO image)\n");
    printf("  - .chd (Compressed Hunks of Data)\n");
    printf("  - .pbp (PSP EBOOT)\n\n");
    printf("Examples:\n");
    printf("  %s game.bin\n", program_name);
    printf("  %s --bios bios.bin --fullscreen game.cue\n", program_name);
    printf("  %s --region pal --cpu-mode dynarec game.iso\n", program_name);
}

// Print version information
static void print_version(void) {
    printf("ZoniStation One v%s\n", ZONISTATION_VERSION);
    printf("Build date: %s\n", ZONISTATION_BUILD_DATE);
    printf("Platform: %s\n", ZS_PLATFORM_STRING);
    printf("Compiler: %s\n", ZS_COMPILER_STRING);
}

// Parse command line arguments
static zs_error_t parse_arguments(int argc, char* argv[], zs_config_t* config) {
    if (argc < 2) {
        print_usage(argv[0]);
        return ZS_ERROR_INVALID_PARAMETER;
    }

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return ZS_ERROR_INVALID_PARAMETER;
        }
        else if (strcmp(argv[i], "--version") == 0) {
            print_version();
            return ZS_ERROR_INVALID_PARAMETER;
        }
        else if (strcmp(argv[i], "--bios") == 0) {
            if (++i >= argc) {
                ZS_LOG_ERROR("Missing BIOS file path");
                return ZS_ERROR_INVALID_PARAMETER;
            }
            strncpy(config->bios_path, argv[i], sizeof(config->bios_path) - 1);
        }
        else if (strcmp(argv[i], "--memcard") == 0) {
            if (++i >= argc) {
                ZS_LOG_ERROR("Missing memory card file path");
                return ZS_ERROR_INVALID_PARAMETER;
            }
            strncpy(config->memory_card_1_path, argv[i], sizeof(config->memory_card_1_path) - 1);
        }
        else if (strcmp(argv[i], "--gpu") == 0) {
            if (++i >= argc) {
                ZS_LOG_ERROR("Missing GPU plugin path");
                return ZS_ERROR_INVALID_PARAMETER;
            }
            strncpy(config->gpu_plugin_path, argv[i], sizeof(config->gpu_plugin_path) - 1);
        }
        else if (strcmp(argv[i], "--spu") == 0) {
            if (++i >= argc) {
                ZS_LOG_ERROR("Missing SPU plugin path");
                return ZS_ERROR_INVALID_PARAMETER;
            }
            strncpy(config->spu_plugin_path, argv[i], sizeof(config->spu_plugin_path) - 1);
        }
        else if (strcmp(argv[i], "--input") == 0) {
            if (++i >= argc) {
                ZS_LOG_ERROR("Missing input plugin path");
                return ZS_ERROR_INVALID_PARAMETER;
            }
            strncpy(config->input_plugin_path, argv[i], sizeof(config->input_plugin_path) - 1);
        }
        else if (strcmp(argv[i], "--cdrom") == 0) {
            if (++i >= argc) {
                ZS_LOG_ERROR("Missing CD-ROM plugin path");
                return ZS_ERROR_INVALID_PARAMETER;
            }
            strncpy(config->cdrom_plugin_path, argv[i], sizeof(config->cdrom_plugin_path) - 1);
        }
        else if (strcmp(argv[i], "--config") == 0) {
            if (++i >= argc) {
                ZS_LOG_ERROR("Missing config file path");
                return ZS_ERROR_INVALID_PARAMETER;
            }
            zs_error_t result = zs_config_load_from_file(config, argv[i]);
            if (result != ZS_SUCCESS) {
                ZS_LOG_ERROR("Failed to load config file: %s", argv[i]);
                return result;
            }
        }
        else if (strcmp(argv[i], "--region") == 0) {
            if (++i >= argc) {
                ZS_LOG_ERROR("Missing region specification");
                return ZS_ERROR_INVALID_PARAMETER;
            }
            if (strcmp(argv[i], "ntsc") == 0) {
                config->region = ZS_REGION_NTSC;
            } else if (strcmp(argv[i], "pal") == 0) {
                config->region = ZS_REGION_PAL;
            } else if (strcmp(argv[i], "ntsc-j") == 0) {
                config->region = ZS_REGION_NTSC_J;
            } else {
                ZS_LOG_ERROR("Invalid region: %s", argv[i]);
                return ZS_ERROR_INVALID_PARAMETER;
            }
        }
        else if (strcmp(argv[i], "--cpu-mode") == 0) {
            if (++i >= argc) {
                ZS_LOG_ERROR("Missing CPU mode specification");
                return ZS_ERROR_INVALID_PARAMETER;
            }
            if (strcmp(argv[i], "interpreter") == 0) {
                config->cpu_mode = ZS_CPU_MODE_INTERPRETER;
            } else if (strcmp(argv[i], "dynarec") == 0) {
                config->cpu_mode = ZS_CPU_MODE_DYNAMIC_RECOMPILER;
            } else {
                ZS_LOG_ERROR("Invalid CPU mode: %s", argv[i]);
                return ZS_ERROR_INVALID_PARAMETER;
            }
        }
        else if (strcmp(argv[i], "--width") == 0) {
            if (++i >= argc) {
                ZS_LOG_ERROR("Missing width value");
                return ZS_ERROR_INVALID_PARAMETER;
            }
            config->screen_width = (zs_u32)atoi(argv[i]);
        }
        else if (strcmp(argv[i], "--height") == 0) {
            if (++i >= argc) {
                ZS_LOG_ERROR("Missing height value");
                return ZS_ERROR_INVALID_PARAMETER;
            }
            config->screen_height = (zs_u32)atoi(argv[i]);
        }
        else if (strcmp(argv[i], "--fullscreen") == 0) {
            config->fullscreen = ZS_TRUE;
        }
        else if (strcmp(argv[i], "--no-vsync") == 0) {
            config->enable_vsync = ZS_FALSE;
        }
        else if (strcmp(argv[i], "--frame-limit") == 0) {
            if (++i >= argc) {
                ZS_LOG_ERROR("Missing frame limit value");
                return ZS_ERROR_INVALID_PARAMETER;
            }
            config->frame_limit = (zs_u32)atoi(argv[i]);
        }
        else if (strcmp(argv[i], "--log-level") == 0) {
            if (++i >= argc) {
                ZS_LOG_ERROR("Missing log level");
                return ZS_ERROR_INVALID_PARAMETER;
            }
            if (strcmp(argv[i], "error") == 0) {
                config->log_level = ZS_LOG_LEVEL_ERROR;
            } else if (strcmp(argv[i], "warn") == 0) {
                config->log_level = ZS_LOG_LEVEL_WARN;
            } else if (strcmp(argv[i], "info") == 0) {
                config->log_level = ZS_LOG_LEVEL_INFO;
            } else if (strcmp(argv[i], "debug") == 0) {
                config->log_level = ZS_LOG_LEVEL_DEBUG;
            } else if (strcmp(argv[i], "trace") == 0) {
                config->log_level = ZS_LOG_LEVEL_TRACE;
            } else {
                ZS_LOG_ERROR("Invalid log level: %s", argv[i]);
                return ZS_ERROR_INVALID_PARAMETER;
            }
        }
        else if (strcmp(argv[i], "--debug") == 0) {
            config->enable_debugger = ZS_TRUE;
            config->log_level = ZS_LOG_LEVEL_DEBUG;
        }
        else if (argv[i][0] == '-') {
            ZS_LOG_ERROR("Unknown option: %s", argv[i]);
            return ZS_ERROR_INVALID_PARAMETER;
        }
        else {
            // This should be the game file
            strncpy(config->game_path, argv[i], sizeof(config->game_path) - 1);
        }
    }

    // Validate that we have a game file
    if (strlen(config->game_path) == 0) {
        ZS_LOG_ERROR("No game file specified");
        return ZS_ERROR_INVALID_PARAMETER;
    }

    return ZS_SUCCESS;
}

// Main function
int main(int argc, char* argv[]) {
    zs_error_t result;
    zs_config_t config;

    // Initialize configuration with defaults
    result = zs_config_init(&config);
    if (result != ZS_SUCCESS) {
        fprintf(stderr, "Failed to initialize configuration\n");
        return EXIT_FAILURE;
    }

    // Parse command line arguments
    result = parse_arguments(argc, argv, &config);
    if (result != ZS_ERROR_INVALID_PARAMETER) {
        if (result != ZS_SUCCESS) {
            fprintf(stderr, "Failed to parse arguments\n");
            return EXIT_FAILURE;
        }
    } else {
        // Help or version was requested
        return EXIT_SUCCESS;
    }

    // Set log level
    zs_set_log_level(config.log_level);

    // Log startup information
    ZS_LOG_INFO("ZoniStation One v%s starting up", ZONISTATION_VERSION);
    ZS_LOG_INFO("Game file: %s", config.game_path);
    ZS_LOG_INFO("Region: %s", config.region == ZS_REGION_NTSC ? "NTSC" : 
                               config.region == ZS_REGION_PAL ? "PAL" : "NTSC-J");
    ZS_LOG_INFO("CPU mode: %s", config.cpu_mode == ZS_CPU_MODE_INTERPRETER ? "Interpreter" : "Dynamic Recompiler");

    // Set up signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // Initialize SDL
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) < 0) {
        ZS_LOG_ERROR("Failed to initialize SDL: %s", SDL_GetError());
        return EXIT_FAILURE;
    }

    // Initialize emulator core
    result = zs_core_init(&g_core, &config);
    if (result != ZS_SUCCESS) {
        ZS_LOG_ERROR("Failed to initialize emulator core");
        SDL_Quit();
        return EXIT_FAILURE;
    }

    // Load game
    result = zs_core_load_game(g_core, config.game_path);
    if (result != ZS_SUCCESS) {
        ZS_LOG_ERROR("Failed to load game: %s", config.game_path);
        zs_core_shutdown(g_core);
        SDL_Quit();
        return EXIT_FAILURE;
    }

    // Main emulation loop
    ZS_LOG_INFO("Starting emulation...");
    while (g_running) {
        result = zs_core_run_frame(g_core);
        if (result != ZS_SUCCESS) {
            ZS_LOG_ERROR("Emulation error occurred");
            break;
        }
    }

    // Cleanup
    ZS_LOG_INFO("Shutting down...");
    zs_core_shutdown(g_core);
    SDL_Quit();

    ZS_LOG_INFO("ZoniStation One shutdown complete");
    return EXIT_SUCCESS;
} 