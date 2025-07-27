#ifndef ZONISTATION_CORE_H
#define ZONISTATION_CORE_H

#include "zonistation_common.h"
#include "zonistation_config.h"

#ifdef __cplusplus
extern "C" {
#endif

// Forward declarations
typedef struct zs_cpu_t zs_cpu_t;
typedef struct zs_memory_t zs_memory_t;
typedef struct zs_gpu_t zs_gpu_t;
typedef struct zs_spu_t zs_spu_t;
typedef struct zs_cdrom_t zs_cdrom_t;
typedef struct zs_bios_t zs_bios_t;
typedef struct zs_plugin_manager_t zs_plugin_manager_t;

// Core emulator structure
typedef struct zs_core_t {
    // Configuration
    zs_config_t config;
    
    // Core components
    zs_cpu_t* cpu;
    zs_memory_t* memory;
    zs_gpu_t* gpu;
    zs_spu_t* spu;
    zs_cdrom_t* cdrom;
    zs_bios_t* bios;
    zs_plugin_manager_t* plugin_manager;
    
    // State
    zs_bool initialized;
    zs_bool game_loaded;
    zs_bool running;
    zs_u64 frame_count;
    zs_u64 cycle_count;
    
    // Timing
    zs_u32 frame_rate;
    zs_u32 frame_time_us;
    zs_u64 last_frame_time;
    
    // Performance
    zs_u32 frames_per_second;
    zs_u32 cycles_per_second;
    zs_u64 performance_counter;
    
} zs_core_t;

// Core initialization and shutdown
zs_error_t zs_core_init(zs_core_t** core, const zs_config_t* config);
zs_error_t zs_core_shutdown(zs_core_t* core);
zs_error_t zs_core_reset(zs_core_t* core);

// Game loading and management
zs_error_t zs_core_load_game(zs_core_t* core, const char* game_path);
zs_error_t zs_core_unload_game(zs_core_t* core);
zs_error_t zs_core_save_state(zs_core_t* core, const char* filename);
zs_error_t zs_core_load_state(zs_core_t* core, const char* filename);

// Emulation control
zs_error_t zs_core_run_frame(zs_core_t* core);
zs_error_t zs_core_run_cycles(zs_core_t* core, zs_u32 cycles);
zs_error_t zs_core_pause(zs_core_t* core);
zs_error_t zs_core_resume(zs_core_t* core);
zs_error_t zs_core_stop(zs_core_t* core);

// Configuration management
zs_error_t zs_core_set_config(zs_core_t* core, const zs_config_t* config);
zs_error_t zs_core_get_config(zs_core_t* core, zs_config_t* config);
zs_error_t zs_core_apply_config(zs_core_t* core);

// Component access
zs_cpu_t* zs_core_get_cpu(zs_core_t* core);
zs_memory_t* zs_core_get_memory(zs_core_t* core);
zs_gpu_t* zs_core_get_gpu(zs_core_t* core);
zs_spu_t* zs_core_get_spu(zs_core_t* core);
zs_cdrom_t* zs_core_get_cdrom(zs_core_t* core);
zs_bios_t* zs_core_get_bios(zs_core_t* core);

// State information
zs_bool zs_core_is_initialized(const zs_core_t* core);
zs_bool zs_core_is_game_loaded(const zs_core_t* core);
zs_bool zs_core_is_running(const zs_core_t* core);
zs_u64 zs_core_get_frame_count(const zs_core_t* core);
zs_u64 zs_core_get_cycle_count(const zs_core_t* core);
zs_u32 zs_core_get_fps(const zs_core_t* core);

// Debugging and development
zs_error_t zs_core_enable_debugger(zs_core_t* core);
zs_error_t zs_core_disable_debugger(zs_core_t* core);
zs_error_t zs_core_set_breakpoint(zs_core_t* core, zs_u32 address);
zs_error_t zs_core_clear_breakpoint(zs_core_t* core, zs_u32 address);
zs_error_t zs_core_step_instruction(zs_core_t* core);

// Error handling
const char* zs_core_get_last_error(const zs_core_t* core);
zs_error_t zs_core_clear_error(zs_core_t* core);

#ifdef __cplusplus
}
#endif

#endif // ZONISTATION_CORE_H 