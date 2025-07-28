/**
 * @file zoni_emulator.h
 * @brief Main emulator structure for ZoniStationOne
 * 
 * This file defines the main emulator structure and provides
 * the primary interface for the ZoniStationOne emulator.
 */

#ifndef ZONI_EMULATOR_H
#define ZONI_EMULATOR_H

#include "zoni_common.h"
#include "zoni_cpu.h"
#include "zoni_memory.h"

// Emulator configuration
typedef struct {
    // CPU configuration
    zoni_cpu_config_t cpu_config;
    
    // Memory configuration
    bool enable_bios;
    bool enable_hle_bios;
    char bios_path[256];
    
    // Video configuration
    u32 screen_width;
    u32 screen_height;
    bool fullscreen;
    bool vsync;
    
    // Audio configuration
    u32 audio_sample_rate;
    u32 audio_buffer_size;
    bool audio_enabled;
    
    // Input configuration
    bool controller_enabled;
    char controller_config[256];
    
    // CD-ROM configuration
    bool cdrom_enabled;
    char cdrom_path[256];
    
    // Debug configuration
    bool debug_enabled;
    bool trace_enabled;
    char debug_log_path[256];
    
    // Performance configuration
    u32 frame_rate_limit;
    bool turbo_mode;
    u32 turbo_speed;
} zoni_emulator_config_t;

// Emulator state
typedef enum {
    ZONI_EMU_STATE_STOPPED = 0,
    ZONI_EMU_STATE_RUNNING = 1,
    ZONI_EMU_STATE_PAUSED = 2,
    ZONI_EMU_STATE_RESETTING = 3,
    ZONI_EMU_STATE_LOADING = 4
} zoni_emulator_state_t;

// Main emulator structure
typedef struct {
    // Configuration
    zoni_emulator_config_t config;
    
    // State
    zoni_emulator_state_t state;
    bool initialized;
    
    // Core components
    zoni_cpu_regs_t cpu;
    zoni_memory_t memory;
    
    // Plugin interfaces
    struct {
        void* gpu_plugin;
        void* spu_plugin;
        void* input_plugin;
        void* cdrom_plugin;
    } plugins;
    
    // Timing and performance
    u64 frame_count;
    u64 cycle_count;
    u64 total_cycles;
    double frame_rate;
    double cpu_usage;
    
    // Statistics
    struct {
        u32 frames_rendered;
        u32 audio_samples;
        u32 input_events;
        u32 memory_accesses;
        u32 exceptions_triggered;
    } stats;
    
    // Callbacks
    struct {
        void (*frame_callback)(void* user_data);
        void (*audio_callback)(void* user_data, const void* buffer, u32 size);
        void (*input_callback)(void* user_data, u32 event, u32 data);
        void (*log_callback)(void* user_data, zoni_log_level_t level, const char* message);
    } callbacks;
    
    // User data for callbacks
    void* user_data;
    
} zoni_emulator_t;

// Emulator functions
zoni_error_t zoni_emulator_init(zoni_emulator_t* emu, const zoni_emulator_config_t* config);
void zoni_emulator_shutdown(zoni_emulator_t* emu);
void zoni_emulator_reset(zoni_emulator_t* emu);

// Emulator control
zoni_error_t zoni_emulator_run(zoni_emulator_t* emu);
zoni_error_t zoni_emulator_pause(zoni_emulator_t* emu);
zoni_error_t zoni_emulator_stop(zoni_emulator_t* emu);
zoni_error_t zoni_emulator_step(zoni_emulator_t* emu);

// Game loading
zoni_error_t zoni_emulator_load_bios(zoni_emulator_t* emu, const char* bios_path);
zoni_error_t zoni_emulator_load_game(zoni_emulator_t* emu, const char* game_path);
zoni_error_t zoni_emulator_load_state(zoni_emulator_t* emu, const char* state_path);
zoni_error_t zoni_emulator_save_state(zoni_emulator_t* emu, const char* state_path);

// Plugin management
zoni_error_t zoni_emulator_load_plugin(zoni_emulator_t* emu, const char* plugin_path, const char* plugin_type);
zoni_error_t zoni_emulator_unload_plugin(zoni_emulator_t* emu, const char* plugin_type);

// Configuration
zoni_error_t zoni_emulator_set_config(zoni_emulator_t* emu, const zoni_emulator_config_t* config);
zoni_error_t zoni_emulator_get_config(zoni_emulator_t* emu, zoni_emulator_config_t* config);

// Callback management
zoni_error_t zoni_emulator_set_frame_callback(zoni_emulator_t* emu, void (*callback)(void* user_data), void* user_data);
zoni_error_t zoni_emulator_set_audio_callback(zoni_emulator_t* emu, void (*callback)(void* user_data, const void* buffer, u32 size), void* user_data);
zoni_error_t zoni_emulator_set_input_callback(zoni_emulator_t* emu, void (*callback)(void* user_data, u32 event, u32 data), void* user_data);
zoni_error_t zoni_emulator_set_log_callback(zoni_emulator_t* emu, void (*callback)(void* user_data, zoni_log_level_t level, const char* message), void* user_data);

// Statistics and debugging
void zoni_emulator_get_stats(zoni_emulator_t* emu, void* stats);
void zoni_emulator_dump_debug_info(zoni_emulator_t* emu);
void zoni_emulator_enable_debug(zoni_emulator_t* emu, bool enabled);

// Utility functions
const char* zoni_emulator_get_version(void);
const char* zoni_emulator_get_state_string(zoni_emulator_state_t state);
bool zoni_emulator_is_running(zoni_emulator_t* emu);
bool zoni_emulator_is_paused(zoni_emulator_t* emu);

// Default configuration
zoni_error_t zoni_emulator_get_default_config(zoni_emulator_config_t* config);

// Error handling
const char* zoni_emulator_get_error_string(zoni_error_t error);

#endif // ZONI_EMULATOR_H 