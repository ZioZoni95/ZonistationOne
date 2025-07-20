#include "../include/sio.h"
#include "../include/log.h"
 
// Initialize SIO state
void nc_sio_init(NcSio* sio) {
    sio->status = 0;
    NC_LOGI("SIO initialized (default state)");
} 