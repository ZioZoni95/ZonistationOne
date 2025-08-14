#include "./../include/zoni_bus.h"
#include "./../include/zoni_cdrom.h" // per zoni_log

void zoni_bus_init(zoni_bus_t* bus, zoni_gpu_t* gpu) {
    bus->gpu = gpu;
}

// Lettura I/O
/*u32 zoni_bus_read32(zoni_bus_t* bus, u32 addr) {
    u32 value = 0;

    switch (addr) {
        // GPU GP0 (data port)
        case 0x1F801810:
            value = zoni_gpu_read_gp0(bus->gpu);
            break;

        // GPU GP1 (status register)
        case 0x1F801814:
            // GPU ready + DMA ready
            value = 0x1C000000;
            break;

        // Controller pad (joypad)
        case 0x1F801040:
            value = 0x0000FFFF; // nessun tasto premuto
            break;

        default:
            value = 0; // Stub di default
            break;
    }

    // Log lettura I/O
    zoni_log(ZONI_LOG_DEBUG, "I/O READ  [0x%08X] -> 0x%08X", addr, value);
    return value;
}*/
u32 zoni_bus_read32(zoni_bus_t* bus, u32 addr) {
    u32 value = 0;

    switch (addr) {
        // --- GPU ---
        case 0x1F801810:  // GP0 read
            value = zoni_gpu_read_gp0(bus->gpu);
            break;
        case 0x1F801814:  // GP1 status
            value = 0x1C000000; // GPU ready + DMA ready
            break;

        // --- DMA Controller ---
        case 0x1F8010F0:  // DMA Control
            value = 0x07654321; // fake "ready"
            break;
        case 0x1F8010F4:  // DMA Interrupt
            value = 0;
            break;

        // --- Timers ---
        case 0x1F801100:
        case 0x1F801104:
        case 0x1F801110:
        case 0x1F801114:
        case 0x1F801120:
        case 0x1F801124:
            value = 0;
            break;

        // --- Controller ---
        case 0x1F801040:  // JOY_DATA
            value = 0x0000FFFF; // no button pressed
            break;
        case 0x1F801044:  // JOY_STAT
            value = 0x0000005C; // "ready"
            break;

        // --- CD-ROM (stub) ---
        case 0x1F801800:
        case 0x1F801801:
        case 0x1F801802:
        case 0x1F801803:
            value = 0;
            break;

        default:
            value = 0;
            break;
    }

    zoni_log(ZONI_LOG_DEBUG, "I/O READ  [0x%08X] -> 0x%08X", addr, value);
    return value;
}


// Scrittura I/O
/*void zoni_bus_write32(zoni_bus_t* bus, u32 addr, u32 value) {
    // Log scrittura I/O
    zoni_log(ZONI_LOG_DEBUG, "I/O WRITE [0x%08X] <- 0x%08X", addr, value);

    switch (addr) {
        // GPU GP0 (data port)
        case 0x1F801810:
            zoni_gpu_write_gp0(bus->gpu, value);
            break;

        // GPU GP1 (command/status)
        case 0x1F801814:
            zoni_gpu_write_gp1(bus->gpu, value);
            break;

        // Stub: ignora gli altri per ora
        default:
            break;
    }
}
*/

void zoni_bus_write32(zoni_bus_t* bus, u32 addr, u32 value) {
    zoni_log(ZONI_LOG_DEBUG, "I/O WRITE [0x%08X] <- 0x%08X", addr, value);

    switch (addr) {
        // --- GPU ---
        case 0x1F801810:  // GP0
            zoni_gpu_write_gp0(bus->gpu, value);
            break;
        case 0x1F801814:  // GP1
            zoni_gpu_write_gp1(bus->gpu, value);
            break;

        // --- DMA, Timers, Controller, CD-ROM ---
        default:
            // Stub: per ora ignora
            break;
    }
}
