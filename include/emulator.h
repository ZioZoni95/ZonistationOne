#ifndef EMULATOR_H
#define EMULATOR_H

#include "psx_types.h"

// Main emulator interface

// Initialize emulator system
psx_result_t emulator_init(psx_emulator_t* emu);

// Shutdown emulator and free resources  
void emulator_shutdown(psx_emulator_t* emu);

// Load BIOS file
psx_result_t emulator_load_bios(psx_emulator_t* emu, const char* bios_path);

// Reset emulator to initial state
void emulator_reset(psx_emulator_t* emu);

// Main emulation loop
void emulator_run(psx_emulator_t* emu);

// Single step execution (for debugging)
psx_result_t emulator_step(psx_emulator_t* emu);

// Emulator control
void emulator_pause(psx_emulator_t* emu);
void emulator_resume(psx_emulator_t* emu);
void emulator_stop(psx_emulator_t* emu);

// Debug features
void emulator_set_debug_mode(psx_emulator_t* emu, bool enabled);
void emulator_print_state(const psx_emulator_t* emu);

// Configuration
typedef struct {
    bool debug_mode;
    bool verbose_logging;
    const char* bios_path;
} emulator_config_t;

psx_result_t emulator_configure(psx_emulator_t* emu, const emulator_config_t* config);

#endif // EMULATOR_H