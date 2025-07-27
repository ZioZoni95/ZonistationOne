#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

#include "zonistation_common.h"
#include "zonistation_config.h"

zs_error_t zs_config_init(zs_config_t* config) {
    if (config == NULL) {
        return ZS_ERROR_INVALID_PARAMETER;
    }
    
    // Set all defaults
    return zs_config_set_defaults(config);
}

zs_error_t zs_config_set_defaults(zs_config_t* config) {
    if (config == NULL) {
        return ZS_ERROR_INVALID_PARAMETER;
    }
    
    // System settings
    config->region = ZS_CONFIG_DEFAULT_REGION;
    config->cpu_mode = ZS_CONFIG_DEFAULT_CPU_MODE;
    config->enable_hle_bios = ZS_CONFIG_DEFAULT_ENABLE_HLE_BIOS;
    config->enable_debugger = ZS_CONFIG_DEFAULT_ENABLE_DEBUGGER;
    config->enable_cheats = ZS_CONFIG_DEFAULT_ENABLE_CHEATS;
    
    // Performance settings
    config->frame_limit = ZS_CONFIG_DEFAULT_FRAME_LIMIT;
    config->cpu_cycle_multiplier = ZS_CONFIG_DEFAULT_CPU_CYCLE_MULTIPLIER;
    config->enable_frame_skip = ZS_CONFIG_DEFAULT_ENABLE_FRAME_SKIP;
    config->enable_vsync = ZS_CONFIG_DEFAULT_ENABLE_VSYNC;
    
    // Graphics settings
    config->screen_width = ZS_CONFIG_DEFAULT_SCREEN_WIDTH;
    config->screen_height = ZS_CONFIG_DEFAULT_SCREEN_HEIGHT;
    config->fullscreen = ZS_CONFIG_DEFAULT_FULLSCREEN;
    config->enable_bilinear_filtering = ZS_CONFIG_DEFAULT_ENABLE_BILINEAR_FILTERING;
    config->enable_perspective_correction = ZS_CONFIG_DEFAULT_ENABLE_PERSPECTIVE_CORRECTION;
    config->enable_gpu_timing_hacks = ZS_CONFIG_DEFAULT_ENABLE_GPU_TIMING_HACKS;
    
    // Audio settings
    config->audio_sample_rate = ZS_CONFIG_DEFAULT_AUDIO_SAMPLE_RATE;
    config->audio_buffer_size = ZS_CONFIG_DEFAULT_AUDIO_BUFFER_SIZE;
    config->enable_audio_interpolation = ZS_CONFIG_DEFAULT_ENABLE_AUDIO_INTERPOLATION;
    config->enable_reverb = ZS_CONFIG_DEFAULT_ENABLE_REVERB;
    
    // CD-ROM settings
    config->enable_cdda_audio = ZS_CONFIG_DEFAULT_ENABLE_CDDA_AUDIO;
    config->enable_subchannel_reading = ZS_CONFIG_DEFAULT_ENABLE_SUBCHANNEL_READING;
    config->enable_async_reading = ZS_CONFIG_DEFAULT_ENABLE_ASYNC_READING;
    config->cdrom_read_speed = ZS_CONFIG_DEFAULT_CDROM_READ_SPEED;
    
    // Memory card settings
    config->enable_memory_card_1 = ZS_CONFIG_DEFAULT_ENABLE_MEMORY_CARD_1;
    config->enable_memory_card_2 = ZS_CONFIG_DEFAULT_ENABLE_MEMORY_CARD_2;
    strcpy(config->memory_card_1_path, "memcard1.mcr");
    strcpy(config->memory_card_2_path, "memcard2.mcr");
    
    // Controller settings
    config->enable_analog_controller = ZS_CONFIG_DEFAULT_ENABLE_ANALOG_CONTROLLER;
    config->enable_dualshock_vibration = ZS_CONFIG_DEFAULT_ENABLE_DUALSHOCK_VIBRATION;
    config->enable_guncon_support = ZS_CONFIG_DEFAULT_ENABLE_GUNCON_SUPPORT;
    
    // Plugin paths
    strcpy(config->gpu_plugin_path, "");
    strcpy(config->spu_plugin_path, "");
    strcpy(config->input_plugin_path, "");
    strcpy(config->cdrom_plugin_path, "");
    
    // File paths - try to auto-detect BIOS file
    zs_error_t bios_result = zs_config_detect_bios_file(config);
    if (bios_result != ZS_SUCCESS) {
        // Fallback to default path if auto-detection fails
        strcpy(config->bios_path, "../roms/SCPH1001.BIN");
        ZS_LOG_WARN("BIOS auto-detection failed, using default path: %s", config->bios_path);
    }
    
    strcpy(config->game_path, "");
    strcpy(config->save_state_path, "saves/");
    strcpy(config->log_file_path, "zonistation_one.log");
    
    // Debug settings
    config->log_level = ZS_CONFIG_DEFAULT_LOG_LEVEL;
    config->enable_trace_logging = ZS_CONFIG_DEFAULT_ENABLE_TRACE_LOGGING;
    config->enable_memory_logging = ZS_CONFIG_DEFAULT_ENABLE_MEMORY_LOGGING;
    config->enable_instruction_logging = ZS_CONFIG_DEFAULT_ENABLE_INSTRUCTION_LOGGING;
    
    // Advanced settings
    config->enable_icache_emulation = ZS_CONFIG_DEFAULT_ENABLE_ICACHE_EMULATION;
    config->enable_precise_exceptions = ZS_CONFIG_DEFAULT_ENABLE_PRECISE_EXCEPTIONS;
    config->enable_stall_emulation = ZS_CONFIG_DEFAULT_ENABLE_STALL_EMULATION;
    config->enable_branch_delay_slots = ZS_CONFIG_DEFAULT_ENABLE_BRANCH_DELAY_SLOTS;
    
    return ZS_SUCCESS;
}

zs_error_t zs_config_validate(const zs_config_t* config) {
    if (config == NULL) {
        return ZS_ERROR_INVALID_PARAMETER;
    }
    
    // Validate region
    if (!zs_config_is_valid_region(config->region)) {
        return ZS_ERROR_INVALID_PARAMETER;
    }
    
    // Validate CPU mode
    if (!zs_config_is_valid_cpu_mode(config->cpu_mode)) {
        return ZS_ERROR_INVALID_PARAMETER;
    }
    
    // Validate screen resolution
    if (!zs_config_is_valid_screen_resolution(config->screen_width, config->screen_height)) {
        return ZS_ERROR_INVALID_PARAMETER;
    }
    
    // Validate audio sample rate
    if (!zs_config_is_valid_audio_sample_rate(config->audio_sample_rate)) {
        return ZS_ERROR_INVALID_PARAMETER;
    }
    
    return ZS_SUCCESS;
}

zs_error_t zs_config_load_from_file(zs_config_t* config, const char* filename) {
    if (config == NULL || filename == NULL) {
        return ZS_ERROR_INVALID_PARAMETER;
    }
    
    // Placeholder implementation - just set defaults for now
    return zs_config_set_defaults(config);
}

zs_error_t zs_config_save_to_file(const zs_config_t* config, const char* filename) {
    if (config == NULL || filename == NULL) {
        return ZS_ERROR_INVALID_PARAMETER;
    }
    
    // Placeholder implementation - just return success for now
    return ZS_SUCCESS;
}

zs_bool zs_config_is_valid_region(zs_region_t region) {
    return (region >= ZS_REGION_NTSC && region <= ZS_REGION_NTSC_J);
}

zs_bool zs_config_is_valid_cpu_mode(zs_cpu_mode_t mode) {
    return (mode >= ZS_CPU_MODE_INTERPRETER && mode <= ZS_CPU_MODE_DYNAMIC_RECOMPILER);
}

zs_bool zs_config_is_valid_screen_resolution(zs_u32 width, zs_u32 height) {
    return (width >= 256 && width <= ZS_GPU_MAX_WIDTH &&
            height >= 192 && height <= ZS_GPU_MAX_HEIGHT);
}

zs_bool zs_config_is_valid_audio_sample_rate(zs_u32 sample_rate) {
    return (sample_rate == 22050 || sample_rate == 44100 || sample_rate == 48000);
} 

// BIOS file detection function
zs_error_t zs_config_detect_bios_file(zs_config_t* config) {
    if (config == NULL) {
        return ZS_ERROR_INVALID_PARAMETER;
    }
    
    const char* roms_dir = "../roms";
    const char* bios_files[] = {
        "SCPH1001.BIN",      // NTSC-U BIOS
        "SCPH1001 (1) (1).BIN", // Alternative NTSC-U BIOS
        "scph1001 (2).bin",  // Alternative NTSC-U BIOS (lowercase)
        "SCPH5500.BIN",      // NTSC-J BIOS
        "SCPH5501.BIN",      // NTSC-J BIOS
        "SCPH5502.BIN",      // PAL BIOS
        "SCPH5552.BIN",      // PAL BIOS
        NULL
    };
    
    DIR* dir = opendir(roms_dir);
    if (dir == NULL) {
        ZS_LOG_WARN("Could not open roms directory: %s", roms_dir);
        return ZS_ERROR_FILE_NOT_FOUND;
    }
    
    struct dirent* entry;
    zs_bool found_bios = ZS_FALSE;
    
    // First, try to find any of the known BIOS files
    for (int i = 0; bios_files[i] != NULL; i++) {
        char full_path[512];
        snprintf(full_path, sizeof(full_path), "%s/%s", roms_dir, bios_files[i]);
        
        struct stat st;
        if (stat(full_path, &st) == 0 && S_ISREG(st.st_mode)) {
            // Check if file size is reasonable for a BIOS (512KB)
            if (st.st_size >= 512 * 1024 && st.st_size <= 1024 * 1024) {
                strcpy(config->bios_path, full_path);
                ZS_LOG_INFO("Found BIOS file: %s (size: %ld bytes)", full_path, st.st_size);
                found_bios = ZS_TRUE;
                break;
            }
        }
    }
    
    // If no known BIOS found, look for any .BIN file that might be a BIOS
    if (!found_bios) {
        rewinddir(dir);
        while ((entry = readdir(dir)) != NULL) {
            if (entry->d_type == DT_REG) {  // Regular file
                char* filename = entry->d_name;
                char* ext = strrchr(filename, '.');
                
                // Check if it's a .BIN file
                if (ext != NULL && (strcasecmp(ext, ".BIN") == 0 || strcasecmp(ext, ".bin") == 0)) {
                    char full_path[512];
                    snprintf(full_path, sizeof(full_path), "%s/%s", roms_dir, filename);
                    
                    struct stat st;
                    if (stat(full_path, &st) == 0) {
                        // Check if file size is reasonable for a BIOS (512KB)
                        if (st.st_size >= 512 * 1024 && st.st_size <= 1024 * 1024) {
                            strcpy(config->bios_path, full_path);
                            ZS_LOG_INFO("Found potential BIOS file: %s (size: %ld bytes)", full_path, st.st_size);
                            found_bios = ZS_TRUE;
                            break;
                        }
                    }
                }
            }
        }
    }
    
    closedir(dir);
    
    if (!found_bios) {
        ZS_LOG_WARN("No BIOS file found in roms directory: %s", roms_dir);
        ZS_LOG_INFO("Please place a PlayStation BIOS file (SCPH1001.BIN, SCPH5500.BIN, etc.) in the roms/ directory");
        return ZS_ERROR_FILE_NOT_FOUND;
    }
    
    return ZS_SUCCESS;
} 