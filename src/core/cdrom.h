/*
 * ZonistationOne - PlayStation One Emulator
 * CD-ROM Interface
 */

#ifndef PSX_CDROM_H
#define PSX_CDROM_H

#include "system.h"

#ifdef __cplusplus
extern "C" {
#endif

/* CD-ROM interface functions */
psx_cdrom_t *cdrom_create(void);
void cdrom_destroy(psx_cdrom_t *cdrom);

int cdrom_init(psx_cdrom_t *cdrom);
void cdrom_shutdown(psx_cdrom_t *cdrom);
void cdrom_reset(psx_cdrom_t *cdrom);

/* Execution */
int cdrom_step(psx_cdrom_t *cdrom, uint32_t cycles);

/* Register access */
uint8_t cdrom_read_register(psx_cdrom_t *cdrom, uint32_t address);
void cdrom_write_register(psx_cdrom_t *cdrom, uint32_t address, uint8_t value);

#ifdef __cplusplus
}
#endif

#endif /* PSX_CDROM_H */