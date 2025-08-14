
#ifndef ZONI_BUS_H
#define ZONI_BUS_H

#include "zoni_common.h"
#include "zoni_gpu.h"

// Struttura bus
typedef struct {
    zoni_gpu_t* gpu;   // puntatore alla GPU
    // aggiungerai SPU, CD-ROM, Controller, DMA...
} zoni_bus_t;

void zoni_bus_init(zoni_bus_t* bus, zoni_gpu_t* gpu);
u32   zoni_bus_read32(zoni_bus_t* bus, u32 addr);
void  zoni_bus_write32(zoni_bus_t* bus, u32 addr, u32 value);

#endif // ZONI_BIOS_H 