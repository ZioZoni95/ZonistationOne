#ifndef NEWCORE_EMULATOR_H
#define NEWCORE_EMULATOR_H

#include "cpu.h"
struct NcInterconnect;
#include "event_scheduler.h"
#include "renderer_plugin.h"
#include "ram.h"
#include "vram.h"
#include "bios.h"
#include "gpu.h"
#include "timers.h"
#include "cdrom.h"
#include "gte.h"
#include "debugger.h"
#include "sio.h"
#include "spu.h"
#include "dma.h"
#include "event_scheduler.h"
// ... add other subsystem includes as needed

// Emulator context holding all subsystem state
typedef struct {
    NcCpu cpu;
    struct NcInterconnect* interconnect;
    NcRam ram;
    NcVram vram;
    NcBios bios;
    NcGpu gpu;
    NcTimers timers;
    NcCdrom cdrom;
    NcGte gte;
    NcDebugger debugger;
    NcSio sio;
    NcSpu spu;
    NcDma dma;
    NcEventQueue eventq;
    // Add more as needed
} EmulatorContext;

// Main entry point for the emulator core
int emulator_run(EmulatorContext* ctx);

#endif // NEWCORE_EMULATOR_H 