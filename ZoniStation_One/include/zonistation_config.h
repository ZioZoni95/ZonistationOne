#ifndef ZONISTATION_CONFIG_H
#define ZONISTATION_CONFIG_H

#include "zonistation_common.h"

#ifdef __cplusplus
extern "C" {
#endif

// Emulator configuration structure
typedef struct {
    // System settings
    zs_region_t region;
    zs_cpu_mode_t cpu_mode;
    zs_bool enable_hle_bios;
    zs_bool enable_debugger;
    zs_bool enable_cheats;
    
    // Performance settings
    zs_u32 frame_limit;
    zs_u32 cpu_cycle_multiplier;
    zs_bool enable_frame_skip;
    zs_bool enable_vsync;
    
    // Graphics settings
    zs_u32 screen_width;
    zs_u32 screen_height;
    zs_bool fullscreen;
    zs_bool enable_bilinear_filtering;
    zs_bool enable_perspective_correction;
    zs_bool enable_gpu_timing_hacks;
    
    // Audio settings
    zs_u32 audio_sample_rate;
    zs_u32 audio_buffer_size;
    zs_bool enable_audio_interpolation;
    zs_bool enable_reverb;
    
    // CD-ROM settings
    zs_bool enable_cdda_audio;
    zs_bool enable_subchannel_reading;
    zs_bool enable_async_reading;
    zs_u32 cdrom_read_speed;
    
    // Memory card settings
    zs_bool enable_memory_card_1;
    zs_bool enable_memory_card_2;
    char memory_card_1_path[256];
    char memory_card_2_path[256];
    
    // Controller settings
    zs_bool enable_analog_controller;
    zs_bool enable_dualshock_vibration;
    zs_bool enable_guncon_support;
    
    // Plugin paths
    char gpu_plugin_path[256];
    char spu_plugin_path[256];
    char input_plugin_path[256];
    char cdrom_plugin_path[256];
    
    // File paths
    char bios_path[256];
    char game_path[256];
    char save_state_path[256];
    char log_file_path[256];
    
    // Debug settings
    zs_log_level_t log_level;
    zs_bool enable_trace_logging;
    zs_bool enable_memory_logging;
    zs_bool enable_instruction_logging;
    
    // Advanced settings
    zs_bool enable_icache_emulation;
    zs_bool enable_precise_exceptions;
    zs_bool enable_stall_emulation;
    zs_bool enable_branch_delay_slots;
    
} zs_config_t;

// Default configuration values
#define ZS_CONFIG_DEFAULT_REGION                    ZS_REGION_NTSC
#define ZS_CONFIG_DEFAULT_CPU_MODE                  ZS_CPU_MODE_INTERPRETER
#define ZS_CONFIG_DEFAULT_ENABLE_HLE_BIOS           ZS_TRUE
#define ZS_CONFIG_DEFAULT_ENABLE_DEBUGGER           ZS_FALSE
#define ZS_CONFIG_DEFAULT_ENABLE_CHEATS             ZS_FALSE
#define ZS_CONFIG_DEFAULT_FRAME_LIMIT               60
#define ZS_CONFIG_DEFAULT_CPU_CYCLE_MULTIPLIER      100
#define ZS_CONFIG_DEFAULT_ENABLE_FRAME_SKIP         ZS_FALSE
#define ZS_CONFIG_DEFAULT_ENABLE_VSYNC              ZS_TRUE
#define ZS_CONFIG_DEFAULT_SCREEN_WIDTH              320
#define ZS_CONFIG_DEFAULT_SCREEN_HEIGHT             240
#define ZS_CONFIG_DEFAULT_FULLSCREEN                ZS_FALSE
#define ZS_CONFIG_DEFAULT_ENABLE_BILINEAR_FILTERING ZS_TRUE
#define ZS_CONFIG_DEFAULT_ENABLE_PERSPECTIVE_CORRECTION ZS_TRUE
#define ZS_CONFIG_DEFAULT_ENABLE_GPU_TIMING_HACKS   ZS_FALSE
#define ZS_CONFIG_DEFAULT_AUDIO_SAMPLE_RATE         ZS_SPU_SAMPLE_RATE
#define ZS_CONFIG_DEFAULT_AUDIO_BUFFER_SIZE         ZS_SPU_BUFFER_SIZE
#define ZS_CONFIG_DEFAULT_ENABLE_AUDIO_INTERPOLATION ZS_TRUE
#define ZS_CONFIG_DEFAULT_ENABLE_REVERB             ZS_TRUE
#define ZS_CONFIG_DEFAULT_ENABLE_CDDA_AUDIO         ZS_TRUE
#define ZS_CONFIG_DEFAULT_ENABLE_SUBCHANNEL_READING ZS_FALSE
#define ZS_CONFIG_DEFAULT_ENABLE_ASYNC_READING      ZS_TRUE
#define ZS_CONFIG_DEFAULT_CDROM_READ_SPEED          2
#define ZS_CONFIG_DEFAULT_ENABLE_MEMORY_CARD_1      ZS_TRUE
#define ZS_CONFIG_DEFAULT_ENABLE_MEMORY_CARD_2      ZS_TRUE
#define ZS_CONFIG_DEFAULT_ENABLE_ANALOG_CONTROLLER  ZS_TRUE
#define ZS_CONFIG_DEFAULT_ENABLE_DUALSHOCK_VIBRATION ZS_TRUE
#define ZS_CONFIG_DEFAULT_ENABLE_GUNCON_SUPPORT     ZS_FALSE
#define ZS_CONFIG_DEFAULT_LOG_LEVEL                 ZS_LOG_LEVEL_INFO
#define ZS_CONFIG_DEFAULT_ENABLE_TRACE_LOGGING      ZS_FALSE
#define ZS_CONFIG_DEFAULT_ENABLE_MEMORY_LOGGING     ZS_FALSE
#define ZS_CONFIG_DEFAULT_ENABLE_INSTRUCTION_LOGGING ZS_FALSE
#define ZS_CONFIG_DEFAULT_ENABLE_ICACHE_EMULATION   ZS_TRUE
#define ZS_CONFIG_DEFAULT_ENABLE_PRECISE_EXCEPTIONS ZS_TRUE
#define ZS_CONFIG_DEFAULT_ENABLE_STALL_EMULATION    ZS_TRUE
#define ZS_CONFIG_DEFAULT_ENABLE_BRANCH_DELAY_SLOTS ZS_TRUE

// Function declarations
zs_error_t zs_config_init(zs_config_t* config);
zs_error_t zs_config_load_from_file(zs_config_t* config, const char* filename);
zs_error_t zs_config_save_to_file(const zs_config_t* config, const char* filename);
zs_error_t zs_config_set_defaults(zs_config_t* config);
zs_error_t zs_config_validate(const zs_config_t* config);

// Configuration validation
zs_bool zs_config_is_valid_region(zs_region_t region);
zs_bool zs_config_is_valid_cpu_mode(zs_cpu_mode_t mode);
zs_bool zs_config_is_valid_screen_resolution(zs_u32 width, zs_u32 height);
zs_bool zs_config_is_valid_audio_sample_rate(zs_u32 sample_rate);

// BIOS detection function
zs_error_t zs_config_detect_bios_file(zs_config_t* config);

#ifdef __cplusplus
}
#endif

#endif // ZONISTATION_CONFIG_H 