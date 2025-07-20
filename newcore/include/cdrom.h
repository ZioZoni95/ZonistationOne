#ifndef NEWCORE_CDROM_H
#define NEWCORE_CDROM_H

#include <stdint.h>
#include <stdbool.h>

struct NcInterconnect;

// CDROM state structure for newcore
typedef struct {
    uint8_t status;
    bool disc_present;
    struct NcInterconnect* inter;
    // TODO: Add more CDROM state fields as needed
} NcCdrom;

// Initialize CDROM
void nc_cdrom_init(NcCdrom* cdrom, struct NcInterconnect* inter);

#endif // NEWCORE_CDROM_H 