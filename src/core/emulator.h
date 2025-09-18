/*
 * ZonistationOne - PlayStation One Emulator
 * Main Emulator Core Interface
 */

#ifndef PSX_EMULATOR_H
#define PSX_EMULATOR_H

#include "system.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Emulator lifecycle functions */
psx_emulator_t *emulator_create(void);
void emulator_destroy(psx_emulator_t *emu);

/* Initialization and configuration */
int emulator_init(psx_emulator_t *emu);
void emulator_shutdown(psx_emulator_t *emu);

/* ROM/BIOS loading */
int emulator_load_bios(psx_emulator_t *emu, const char *path);
int emulator_load_rom(psx_emulator_t *emu, const char *path);

/* Emulation control */
void emulator_start(psx_emulator_t *emu);
void emulator_stop(psx_emulator_t *emu);
void emulator_pause(psx_emulator_t *emu);
void emulator_resume(psx_emulator_t *emu);

/* Execution */
int emulator_step(psx_emulator_t *emu);
int emulator_is_running(psx_emulator_t *emu);

/* Configuration */
void emulator_set_debug_mode(psx_emulator_t *emu, int enabled);

#ifdef __cplusplus
}
#endif

#endif /* PSX_EMULATOR_H */