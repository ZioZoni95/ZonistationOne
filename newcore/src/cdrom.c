#include "../include/cdrom.h"
#include "../include/log.h"
#include <string.h>

// Initialize CDROM state
void nc_cdrom_init(NcCdrom* cdrom, struct NcInterconnect* inter) {
    cdrom->status = 0;
    cdrom->disc_present = false;
    cdrom->inter = inter;
    NC_LOGI("CDROM initialized (default state)");
} 