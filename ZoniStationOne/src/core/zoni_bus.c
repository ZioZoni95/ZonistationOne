#include "zoni_bus.h"
#include "zoni_gpu.h"
#include "zoni_timer.h"
#include "zoni_log.h"

// Global bus instance
zoni_bus_t g_bus;

zoni_error_t zoni_bus_init(zoni_bus_t* bus) {
    if (!bus) {
        return ZONI_ERROR_INVALID_PARAMETER;
    }

    // Initialize GPU
    zoni_error_t gpu_result = zoni_gpu_init(&bus->gpu);
    if (gpu_result != ZONI_SUCCESS) {
        return gpu_result;
    }

    // Initialize timer system
    zoni_error_t timer_result = zoni_timer_init(zoni_timer_get_system());
    if (timer_result != ZONI_SUCCESS) {
        return timer_result;
    }

    bus->initialized = true;
    zoni_log(ZONI_LOG_INFO, "Bus system initialized");
    return ZONI_SUCCESS;
}

void zoni_bus_reset(zoni_bus_t* bus) {
    if (!bus || !bus->initialized) {
        return;
    }

    zoni_gpu_reset(&bus->gpu);
    zoni_timer_reset(zoni_timer_get_system());
    zoni_log(ZONI_LOG_INFO, "Bus system reset");
}

u32 zoni_bus_read32(zoni_bus_t* bus, u32 addr) {
    u32 value = 0;

    switch (addr) {
        // --- GPU ---
        case 0x1F801810:  // GP0 read
            value = zoni_gpu_read_gp0(bus->gpu);
            break;
        case 0x1F801814:  // GP1 status
            value = zoni_gpu_read_gp1(bus->gpu);  // Return actual GPU status
            break;

        // --- Timer System (0x1F801100-0x1F80112F) ---
        case 0x1F801100:  // Timer 0 count
            value = zoni_timer_read_count(zoni_timer_get_system(), 0);
            break;
        case 0x1F801104:  // Timer 0 mode
            value = zoni_timer_read_mode(zoni_timer_get_system(), 0);
            break;
        case 0x1F801108:  // Timer 0 target
            value = zoni_timer_read_target(zoni_timer_get_system(), 0);
            break;
        case 0x1F801110:  // Timer 1 count
            value = zoni_timer_read_count(zoni_timer_get_system(), 1);
            break;
        case 0x1F801114:  // Timer 1 mode
            value = zoni_timer_read_mode(zoni_timer_get_system(), 1);
            break;
        case 0x1F801118:  // Timer 1 target
            value = zoni_timer_read_target(zoni_timer_get_system(), 1);
            break;
        case 0x1F801120:  // Timer 2 count
            value = zoni_timer_read_count(zoni_timer_get_system(), 2);
            break;
        case 0x1F801124:  // Timer 2 mode
            value = zoni_timer_read_mode(zoni_timer_get_system(), 2);
            break;
        case 0x1F801128:  // Timer 2 target
            value = zoni_timer_read_target(zoni_timer_get_system(), 2);
            break;

        // --- DMA Controller ---
        case 0x1F8010F0:  // DMA Control
            value = 0x07654321; // fake "ready"
            break;
        case 0x1F8010F4:  // DMA Interrupt
            value = 0;
            break;

        // --- Hardware Control Registers (BIOS initialization) ---
        case 0x1F801010:  // Hardware control register
            value = 0x0013243F; // Default value from PCSX ReARMed
            break;
        case 0x1F801060:  // Hardware control register
            value = 0x00000B88; // Default value from PCSX ReARMed
            break;

        // --- Cache Control Region (0x1FFE0000-0x1FFEFFFF) ---
        case 0x1FFE0130:  // Cache Control Register
            value = 0; // Read returns 0 (write-only register)
            break;

        default:
            value = 0;
            break;
    }

    // Only log important hardware reads
    if (addr == 0x1F801814 || addr == 0x1FFE0130 || 
        (addr >= 0x1F801100 && addr <= 0x1F801128)) {
        zoni_log(ZONI_LOG_INFO, "HW READ  [0x%08X] -> 0x%08X", addr, value);
    }
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
    // Only log important hardware writes
    if (addr == 0x1F801810 || addr == 0x1FFE0130 || 
        (addr >= 0x1F801100 && addr <= 0x1F801128)) {
        zoni_log(ZONI_LOG_INFO, "HW WRITE [0x%08X] <- 0x%08X", addr, value);
    }

    switch (addr) {
        // --- GPU ---
        case 0x1F801810:  // GP0
            zoni_gpu_write_gp0(bus->gpu, value);
            break;
        case 0x1F801814:  // GP1
            zoni_gpu_write_gp1(bus->gpu, value);
            break;

        // --- Timer System (0x1F801100-0x1F80112F) ---
        case 0x1F801100:  // Timer 0 count
            zoni_timer_write_count(zoni_timer_get_system(), 0, value);
            break;
        case 0x1F801104:  // Timer 0 mode
            zoni_timer_write_mode(zoni_timer_get_system(), 0, value);
            break;
        case 0x1F801108:  // Timer 0 target
            zoni_timer_write_target(zoni_timer_get_system(), 0, value);
            break;
        case 0x1F801110:  // Timer 1 count
            zoni_timer_write_count(zoni_timer_get_system(), 1, value);
            break;
        case 0x1F801114:  // Timer 1 mode
            zoni_timer_write_mode(zoni_timer_get_system(), 1, value);
            break;
        case 0x1F801118:  // Timer 1 target
            zoni_timer_write_target(zoni_timer_get_system(), 1, value);
            break;
        case 0x1F801120:  // Timer 2 count
            zoni_timer_write_count(zoni_timer_get_system(), 2, value);
            break;
        case 0x1F801124:  // Timer 2 mode
            zoni_timer_write_mode(zoni_timer_get_system(), 2, value);
            break;
        case 0x1F801128:  // Timer 2 target
            zoni_timer_write_target(zoni_timer_get_system(), 2, value);
            break;

        // --- Cache Control Region (0x1FFE0000-0x1FFEFFFF) ---
        case 0x1FFE0130:  // Cache Control Register
            // BIOS writes 0x00000804 here - acknowledge it
            zoni_log(ZONI_LOG_INFO, "Cache control register set: 0x%08X", value);
            break;

        // --- DMA, Timers, Controller, CD-ROM ---
        default:
            // Stub: per ora ignora
            break;
    }
}
