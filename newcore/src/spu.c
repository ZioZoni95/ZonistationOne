#include "../include/spu.h"
#include "../include/log.h"

// Initialize SPU state
void nc_spu_init(NcSpu* spu) {
    spu->status = 0;
    NC_LOGI("SPU initialized (default state)");
} 