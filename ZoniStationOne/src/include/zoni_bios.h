/**
 * @file zoni_bios.h
 * @brief BIOS management for ZoniStationOne
 * 
 * This file provides BIOS loading and management functions
 * for the PlayStation emulator.
 */

#ifndef ZONI_BIOS_H
#define ZONI_BIOS_H

#include "zoni_common.h"
#include "zoni_memory.h"
#include "zoni_cpu.h"

// BIOS file information
typedef struct {
    char filename[256];
    u32 size;
    bool loaded;
    bool valid;
    char version[64];
    char region[16];
    char date[32];
} zoni_bios_info_t;

// BIOS management structure
typedef struct {
    zoni_bios_info_t info;
    bool hle_mode;              // High-Level Emulation mode
    bool slow_boot;             // Slow boot mode for compatibility
    u32 boot_pc;                // Boot PC address
    u32 boot_gp;                // Boot GP register
    u32 boot_sp;                // Boot SP register
} zoni_bios_t;

// BIOS functions
zoni_error_t zoni_bios_init(zoni_bios_t* bios);
void zoni_bios_shutdown(zoni_bios_t* bios);
void zoni_bios_reset(zoni_bios_t* bios);

// BIOS loading functions
zoni_error_t zoni_bios_load_file(zoni_bios_t* bios, zoni_memory_t* memory, const char* filename);
zoni_error_t zoni_bios_load_default(zoni_bios_t* bios, zoni_memory_t* memory);
zoni_error_t zoni_bios_validate_file(const char* filename, zoni_bios_info_t* info);

// BIOS execution functions
zoni_error_t zoni_bios_setup_boot_state(zoni_bios_t* bios, zoni_cpu_regs_t* cpu);
zoni_error_t zoni_bios_execute(zoni_bios_t* bios, zoni_cpu_regs_t* cpu, zoni_memory_t* memory);
bool zoni_bios_execution_ended(zoni_cpu_regs_t* cpu);

// BIOS information functions
const char* zoni_bios_get_version(zoni_bios_t* bios);
const char* zoni_bios_get_region(zoni_bios_t* bios);
bool zoni_bios_is_loaded(zoni_bios_t* bios);
bool zoni_bios_is_hle(zoni_bios_t* bios);

// BIOS configuration
void zoni_bios_set_hle_mode(zoni_bios_t* bios, bool enabled);
void zoni_bios_set_slow_boot(zoni_bios_t* bios, bool enabled);

#endif // ZONI_BIOS_H 