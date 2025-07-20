#ifndef NEWCORE_SPU_H
#define NEWCORE_SPU_H

#include <stdint.h>
#include <stdbool.h>

// SPU (Sound Processing Unit) state structure for newcore
typedef struct {
    uint8_t status;
    // TODO: Add more SPU state fields as needed
} NcSpu;

// Initialize SPU
void nc_spu_init(NcSpu* spu);

#endif // NEWCORE_SPU_H 