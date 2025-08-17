
#ifndef ZONI_BUS_H
#define ZONI_BUS_H

#include "zoni_common.h"
#include "zoni_gpu.h"

// Bus system structure
typedef struct {
    zoni_gpu_t gpu;    // GPU instance (not pointer)
    bool initialized;   // Initialization status
} zoni_bus_t;

// Function prototypes
zoni_error_t zoni_bus_init(zoni_bus_t* bus);
void zoni_bus_reset(zoni_bus_t* bus);

// Hardware register access
u32 zoni_bus_read32(zoni_bus_t* bus, u32 addr);
void zoni_bus_write32(zoni_bus_t* bus, u32 addr, u32 value);

// Global bus instance access
extern zoni_bus_t g_bus;

#endif // ZONI_BUS_H 