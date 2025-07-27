#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL2/SDL.h>

#include "zonistation_common.h"
#include "zonistation_config.h"
#include "zonistation_core.h"
#include "zonistation_cpu.h"
#include "zonistation_memory.h"
#include "zonistation_hardware.h"
#include "zonistation_bios.h"
#include "zonistation_plugins.h"

// Core implementation
zs_error_t zs_core_init(zs_core_t** core_ptr, const zs_config_t* config) {
    ZS_ASSERT(core_ptr != NULL);
    ZS_ASSERT(config != NULL);
    
    zs_core_t* core = (zs_core_t*)malloc(sizeof(zs_core_t));
    if (core == NULL) {
        ZS_LOG_ERROR("Failed to allocate core structure");
        return ZS_ERROR_OUT_OF_MEMORY;
    }
    
    // Initialize core structure
    memset(core, 0, sizeof(zs_core_t));
    memcpy(&core->config, config, sizeof(zs_config_t));
    
    // Initialize components
    zs_error_t result;
    
    // Initialize memory first
    result = zs_memory_init(&core->memory);
    if (result != ZS_SUCCESS) {
        ZS_LOG_ERROR("Failed to initialize memory");
        free(core);
        return result;
    }
    
    // Initialize CPU
    result = zs_cpu_init(&core->cpu, core->memory, &core->config);
    if (result != ZS_SUCCESS) {
        ZS_LOG_ERROR("Failed to initialize CPU");
        zs_memory_shutdown(core->memory);
        free(core);
        return result;
    }
    
    // Initialize BIOS
    result = zs_bios_init(&core->bios, &core->config);
    if (result != ZS_SUCCESS) {
        ZS_LOG_ERROR("Failed to initialize BIOS");
        zs_cpu_shutdown(core->cpu);
        zs_memory_shutdown(core->memory);
        free(core);
        return result;
    }
    
    // Initialize hardware components
    result = zs_hardware_init(&core->gpu, &core->spu, &core->cdrom, &core->config);
    if (result != ZS_SUCCESS) {
        ZS_LOG_ERROR("Failed to initialize hardware");
        zs_bios_shutdown(core->bios);
        zs_cpu_shutdown(core->cpu);
        zs_memory_shutdown(core->memory);
        free(core);
        return result;
    }
    
    // Initialize plugin manager
    result = zs_plugin_manager_init(&core->plugin_manager, &core->config);
    if (result != ZS_SUCCESS) {
        ZS_LOG_ERROR("Failed to initialize plugin manager");
        zs_hardware_shutdown(core->gpu, core->spu, core->cdrom);
        zs_bios_shutdown(core->bios);
        zs_cpu_shutdown(core->cpu);
        zs_memory_shutdown(core->memory);
        free(core);
        return result;
    }
    
    // Set up timing
    core->frame_rate = (core->config.region == ZS_REGION_PAL) ? 50 : 60;
    core->frame_time_us = 1000000 / core->frame_rate;
    core->last_frame_time = SDL_GetPerformanceCounter();
    
    core->initialized = ZS_TRUE;
    *core_ptr = core;
    
    ZS_LOG_INFO("Core initialized successfully");
    return ZS_SUCCESS;
}

zs_error_t zs_core_shutdown(zs_core_t* core) {
    if (core == NULL) {
        return ZS_ERROR_INVALID_PARAMETER;
    }
    
    ZS_LOG_INFO("Shutting down core...");
    
    // Shutdown components in reverse order
    if (core->plugin_manager) {
        zs_plugin_manager_shutdown(core->plugin_manager);
    }
    
    if (core->gpu || core->spu || core->cdrom) {
        zs_hardware_shutdown(core->gpu, core->spu, core->cdrom);
    }
    
    if (core->bios) {
        zs_bios_shutdown(core->bios);
    }
    
    if (core->cpu) {
        zs_cpu_shutdown(core->cpu);
    }
    
    if (core->memory) {
        zs_memory_shutdown(core->memory);
    }
    
    free(core);
    ZS_LOG_INFO("Core shutdown complete");
    return ZS_SUCCESS;
}

zs_error_t zs_core_reset(zs_core_t* core) {
    if (core == NULL || !core->initialized) {
        return ZS_ERROR_INVALID_PARAMETER;
    }
    
    ZS_LOG_INFO("Resetting core...");
    
    // Reset all components
    zs_cpu_reset(core->cpu);
    zs_memory_reset(core->memory);
    zs_bios_reset(core->bios);
    zs_hardware_reset(core->gpu, core->spu, core->cdrom);
    
    // Reset core state
    core->frame_count = 0;
    core->cycle_count = 0;
    core->frames_per_second = 0;
    core->cycles_per_second = 0;
    core->performance_counter = 0;
    core->last_frame_time = SDL_GetPerformanceCounter();
    
    ZS_LOG_INFO("Core reset complete");
    return ZS_SUCCESS;
}

zs_error_t zs_core_load_game(zs_core_t* core, const char* game_path) {
    if (core == NULL || !core->initialized || game_path == NULL) {
        return ZS_ERROR_INVALID_PARAMETER;
    }
    
    ZS_LOG_INFO("Loading game: %s", game_path);
    
    // Load game into CD-ROM
    zs_error_t result = zs_cdrom_load_game(core->cdrom, game_path);
    if (result != ZS_SUCCESS) {
        ZS_LOG_ERROR("Failed to load game: %s", game_path);
        return result;
    }
    
    // Load BIOS if specified
    if (strlen(core->config.bios_path) > 0) {
        result = zs_bios_load_file(core->bios, core->config.bios_path);
        if (result != ZS_SUCCESS) {
            ZS_LOG_WARN("Failed to load BIOS file, using HLE BIOS");
        }
    }
    
    // Reset the system
    result = zs_core_reset(core);
    if (result != ZS_SUCCESS) {
        return result;
    }
    
    core->game_loaded = ZS_TRUE;
    ZS_LOG_INFO("Game loaded successfully");
    return ZS_SUCCESS;
}

zs_error_t zs_core_unload_game(zs_core_t* core) {
    if (core == NULL || !core->initialized) {
        return ZS_ERROR_INVALID_PARAMETER;
    }
    
    ZS_LOG_INFO("Unloading game...");
    
    // Stop emulation
    core->running = ZS_FALSE;
    
    // Unload from CD-ROM
    if (core->cdrom) {
        zs_cdrom_unload_game(core->cdrom);
    }
    
    // Reset system
    zs_core_reset(core);
    
    core->game_loaded = ZS_FALSE;
    ZS_LOG_INFO("Game unloaded");
    return ZS_SUCCESS;
}

zs_error_t zs_core_run_frame(zs_core_t* core) {
    if (core == NULL || !core->initialized || !core->game_loaded) {
        return ZS_ERROR_INVALID_PARAMETER;
    }
    
    // Frame timing
    zs_u64 current_time = SDL_GetPerformanceCounter();
    zs_u64 elapsed_us = (current_time - core->last_frame_time) * 1000000 / SDL_GetPerformanceFrequency();
    
    if (elapsed_us < core->frame_time_us) {
        // Wait for next frame
        SDL_Delay((core->frame_time_us - elapsed_us) / 1000);
    }
    
    // Run one frame worth of cycles
    zs_u32 cycles_per_frame = ZS_PSX_CLOCK_FREQUENCY / core->frame_rate;
    zs_error_t result = zs_core_run_cycles(core, cycles_per_frame);
    if (result != ZS_SUCCESS) {
        return result;
    }
    
    // Update frame counter and timing
    core->frame_count++;
    core->last_frame_time = SDL_GetPerformanceCounter();
    
    // Update performance counters
    core->performance_counter++;
    if (core->performance_counter % 60 == 0) {
        // Update FPS counter every second
        core->frames_per_second = 60;
        core->cycles_per_second = cycles_per_frame * 60;
    }
    
    return ZS_SUCCESS;
}

zs_error_t zs_core_run_cycles(zs_core_t* core, zs_u32 cycles) {
    if (core == NULL || !core->initialized) {
        return ZS_ERROR_INVALID_PARAMETER;
    }
    
    // Run CPU for specified cycles
    zs_error_t result = zs_cpu_run_cycles(core->cpu, cycles);
    if (result != ZS_SUCCESS) {
        return result;
    }
    
    // Update cycle counter
    core->cycle_count += cycles;
    
    // Update hardware components
    zs_hardware_update(core->gpu, core->spu, core->cdrom, cycles);
    
    return ZS_SUCCESS;
}

zs_error_t zs_core_pause(zs_core_t* core) {
    if (core == NULL || !core->initialized) {
        return ZS_ERROR_INVALID_PARAMETER;
    }
    
    core->running = ZS_FALSE;
    ZS_LOG_INFO("Emulation paused");
    return ZS_SUCCESS;
}

zs_error_t zs_core_resume(zs_core_t* core) {
    if (core == NULL || !core->initialized) {
        return ZS_ERROR_INVALID_PARAMETER;
    }
    
    core->running = ZS_TRUE;
    ZS_LOG_INFO("Emulation resumed");
    return ZS_SUCCESS;
}

zs_error_t zs_core_stop(zs_core_t* core) {
    if (core == NULL || !core->initialized) {
        return ZS_ERROR_INVALID_PARAMETER;
    }
    
    core->running = ZS_FALSE;
    ZS_LOG_INFO("Emulation stopped");
    return ZS_SUCCESS;
}

// Component access functions
zs_cpu_t* zs_core_get_cpu(zs_core_t* core) {
    return (core && core->initialized) ? core->cpu : NULL;
}

zs_memory_t* zs_core_get_memory(zs_core_t* core) {
    return (core && core->initialized) ? core->memory : NULL;
}

zs_gpu_t* zs_core_get_gpu(zs_core_t* core) {
    return (core && core->initialized) ? core->gpu : NULL;
}

zs_spu_t* zs_core_get_spu(zs_core_t* core) {
    return (core && core->initialized) ? core->spu : NULL;
}

zs_cdrom_t* zs_core_get_cdrom(zs_core_t* core) {
    return (core && core->initialized) ? core->cdrom : NULL;
}

zs_bios_t* zs_core_get_bios(zs_core_t* core) {
    return (core && core->initialized) ? core->bios : NULL;
}

// State information functions
zs_bool zs_core_is_initialized(const zs_core_t* core) {
    return (core != NULL) ? core->initialized : ZS_FALSE;
}

zs_bool zs_core_is_game_loaded(const zs_core_t* core) {
    return (core != NULL) ? core->game_loaded : ZS_FALSE;
}

zs_bool zs_core_is_running(const zs_core_t* core) {
    return (core != NULL) ? core->running : ZS_FALSE;
}

zs_u64 zs_core_get_frame_count(const zs_core_t* core) {
    return (core != NULL) ? core->frame_count : 0;
}

zs_u64 zs_core_get_cycle_count(const zs_core_t* core) {
    return (core != NULL) ? core->cycle_count : 0;
}

zs_u32 zs_core_get_fps(const zs_core_t* core) {
    return (core != NULL) ? core->frames_per_second : 0;
} 