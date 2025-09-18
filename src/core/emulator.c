/*
 * ZonistationOne - PlayStation One Emulator
 * Main Emulator Core Implementation
 */

#include "emulator.h"
#include "system.h"
#include "logger.h"
#include "memory.h"
#include "cpu.h"
#include "gpu.h"
#include "spu.h"
#include "cdrom.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

struct psx_emulator_s {
    psx_status_t status;
    psx_video_mode_t video_mode;
    
    /* Core components */
    psx_memory_t *memory;
    psx_cpu_t *cpu;
    psx_gpu_t *gpu;
    psx_spu_t *spu;
    psx_cdrom_t *cdrom;
    
    /* Timing */
    uint64_t cycles_total;
    uint64_t cycles_per_frame;
    uint64_t frame_count;
    
    /* Configuration */
    int debug_mode;
    char bios_path[256];
    char rom_path[256];
    
    /* State */
    int initialized;
    int bios_loaded;
};

psx_emulator_t *emulator_create(void) {
    psx_emulator_t *emu = calloc(1, sizeof(psx_emulator_t));
    if (!emu) {
        log_error("Failed to allocate emulator structure");
        return NULL;
    }
    
    emu->status = PSX_STATUS_STOPPED;
    emu->video_mode = PSX_VIDEO_NTSC;
    emu->cycles_per_frame = PSX_CPU_CLOCK_NTSC / 60; /* 60 FPS for NTSC */
    
    log_info("Emulator instance created");
    return emu;
}

int emulator_init(psx_emulator_t *emu) {
    if (!emu) {
        log_error("Invalid emulator instance");
        return -1;
    }
    
    if (emu->initialized) {
        log_warn("Emulator already initialized");
        return 0;
    }
    
    log_info("Initializing emulator components...");
    
    /* Initialize memory subsystem */
    emu->memory = memory_create();
    if (!emu->memory) {
        log_error("Failed to create memory subsystem");
        goto error;
    }
    
    if (memory_init(emu->memory) != 0) {
        log_error("Failed to initialize memory subsystem");
        goto error;
    }
    
    /* Initialize CPU */
    emu->cpu = cpu_create();
    if (!emu->cpu) {
        log_error("Failed to create CPU");
        goto error;
    }
    
    if (cpu_init(emu->cpu, emu->memory) != 0) {
        log_error("Failed to initialize CPU");
        goto error;
    }
    
    /* Initialize GPU */
    emu->gpu = gpu_create();
    if (!emu->gpu) {
        log_error("Failed to create GPU");
        goto error;
    }
    
    if (gpu_init(emu->gpu, emu->memory) != 0) {
        log_error("Failed to initialize GPU");
        goto error;
    }
    
    /* Initialize SPU */
    emu->spu = spu_create();
    if (!emu->spu) {
        log_error("Failed to create SPU");
        goto error;
    }
    
    if (spu_init(emu->spu, emu->memory) != 0) {
        log_error("Failed to initialize SPU");
        goto error;
    }
    
    /* Initialize CD-ROM */
    emu->cdrom = cdrom_create();
    if (!emu->cdrom) {
        log_error("Failed to create CD-ROM subsystem");
        goto error;
    }
    
    if (cdrom_init(emu->cdrom) != 0) {
        log_error("Failed to initialize CD-ROM subsystem");
        goto error;
    }
    
    emu->initialized = 1;
    emu->status = PSX_STATUS_STOPPED;
    
    log_info("Emulator initialization complete");
    return 0;
    
error:
    emulator_shutdown(emu);
    return -1;
}

int emulator_load_bios(psx_emulator_t *emu, const char *path) {
    if (!emu || !path) {
        log_error("Invalid parameters for BIOS loading");
        return -1;
    }
    
    FILE *bios_file = fopen(path, "rb");
    if (!bios_file) {
        log_error("Failed to open BIOS file: %s", path);
        return -1;
    }
    
    /* Get file size */
    fseek(bios_file, 0, SEEK_END);
    long size = ftell(bios_file);
    fseek(bios_file, 0, SEEK_SET);
    
    if (size != PSX_BIOS_SIZE) {
        log_error("Invalid BIOS size: %ld bytes (expected %d)", size, PSX_BIOS_SIZE);
        fclose(bios_file);
        return -1;
    }
    
    /* Load BIOS into memory */
    uint8_t *bios_data = malloc(PSX_BIOS_SIZE);
    if (!bios_data) {
        log_error("Failed to allocate BIOS buffer");
        fclose(bios_file);
        return -1;
    }
    
    size_t read_size = fread(bios_data, 1, PSX_BIOS_SIZE, bios_file);
    fclose(bios_file);
    
    if (read_size != PSX_BIOS_SIZE) {
        log_error("Failed to read complete BIOS file");
        free(bios_data);
        return -1;
    }
    
    /* Install BIOS in memory */
    if (emu->memory && memory_load_bios(emu->memory, bios_data, PSX_BIOS_SIZE) != 0) {
        log_error("Failed to install BIOS in memory");
        free(bios_data);
        return -1;
    }
    
    free(bios_data);
    strncpy(emu->bios_path, path, sizeof(emu->bios_path) - 1);
    emu->bios_loaded = 1;
    
    log_info("BIOS loaded successfully: %s", path);
    return 0;
}

int emulator_load_rom(psx_emulator_t *emu, const char *path) {
    if (!emu || !path) {
        log_error("Invalid parameters for ROM loading");
        return -1;
    }
    
    /* TODO: Implement ROM loading (CD images, executables, etc.) */
    strncpy(emu->rom_path, path, sizeof(emu->rom_path) - 1);
    
    log_info("ROM loaded: %s", path);
    return 0;
}

int emulator_step(psx_emulator_t *emu) {
    if (!emu || !emu->initialized) {
        log_error("Emulator not initialized");
        return -1;
    }
    
    if (emu->status != PSX_STATUS_RUNNING) {
        return 0; /* Not running, but not an error */
    }
    
    /* Execute one CPU instruction */
    if (cpu_step(emu->cpu) != 0) {
        log_error("CPU execution failed");
        emu->status = PSX_STATUS_ERROR;
        return -1;
    }
    
    /* Update other components based on CPU cycles */
    uint32_t cpu_cycles = cpu_get_cycles(emu->cpu);
    
    /* GPU updates */
    if (gpu_step(emu->gpu, cpu_cycles) != 0) {
        log_error("GPU update failed");
        emu->status = PSX_STATUS_ERROR;
        return -1;
    }
    
    /* SPU updates */
    if (spu_step(emu->spu, cpu_cycles) != 0) {
        log_error("SPU update failed");
        emu->status = PSX_STATUS_ERROR;
        return -1;
    }
    
    /* CD-ROM updates */
    if (cdrom_step(emu->cdrom, cpu_cycles) != 0) {
        log_error("CD-ROM update failed");
        emu->status = PSX_STATUS_ERROR;
        return -1;
    }
    
    emu->cycles_total += cpu_cycles;
    
    /* Check for frame completion */
    if (emu->cycles_total >= emu->cycles_per_frame) {
        emu->cycles_total -= emu->cycles_per_frame;
        emu->frame_count++;
        
        /* Trigger VBlank */
        cpu_set_interrupt(emu->cpu, PSX_IRQ_VBLANK);
    }
    
    return 0;
}

int emulator_is_running(psx_emulator_t *emu) {
    return emu && emu->status == PSX_STATUS_RUNNING;
}

void emulator_start(psx_emulator_t *emu) {
    if (!emu || !emu->initialized) {
        log_error("Cannot start uninitialized emulator");
        return;
    }
    
    if (!emu->bios_loaded) {
        log_error("Cannot start emulator without BIOS");
        return;
    }
    
    /* Reset all components */
    cpu_reset(emu->cpu);
    gpu_reset(emu->gpu);
    spu_reset(emu->spu);
    cdrom_reset(emu->cdrom);
    memory_reset(emu->memory);
    
    emu->cycles_total = 0;
    emu->frame_count = 0;
    emu->status = PSX_STATUS_RUNNING;
    
    log_info("Emulator started");
}

void emulator_stop(psx_emulator_t *emu) {
    if (emu) {
        emu->status = PSX_STATUS_STOPPED;
        log_info("Emulator stopped");
    }
}

void emulator_pause(psx_emulator_t *emu) {
    if (emu && emu->status == PSX_STATUS_RUNNING) {
        emu->status = PSX_STATUS_PAUSED;
        log_info("Emulator paused");
    }
}

void emulator_resume(psx_emulator_t *emu) {
    if (emu && emu->status == PSX_STATUS_PAUSED) {
        emu->status = PSX_STATUS_RUNNING;
        log_info("Emulator resumed");
    }
}

void emulator_set_debug_mode(psx_emulator_t *emu, int enabled) {
    if (emu) {
        emu->debug_mode = enabled;
        if (emu->cpu) {
            cpu_set_debug_mode(emu->cpu, enabled);
        }
        log_info("Debug mode %s", enabled ? "enabled" : "disabled");
    }
}

void emulator_shutdown(psx_emulator_t *emu) {
    if (!emu) return;
    
    log_info("Shutting down emulator components...");
    
    if (emu->cdrom) {
        cdrom_shutdown(emu->cdrom);
        cdrom_destroy(emu->cdrom);
        emu->cdrom = NULL;
    }
    
    if (emu->spu) {
        spu_shutdown(emu->spu);
        spu_destroy(emu->spu);
        emu->spu = NULL;
    }
    
    if (emu->gpu) {
        gpu_shutdown(emu->gpu);
        gpu_destroy(emu->gpu);
        emu->gpu = NULL;
    }
    
    if (emu->cpu) {
        cpu_shutdown(emu->cpu);
        cpu_destroy(emu->cpu);
        emu->cpu = NULL;
    }
    
    if (emu->memory) {
        memory_shutdown(emu->memory);
        memory_destroy(emu->memory);
        emu->memory = NULL;
    }
    
    emu->initialized = 0;
    emu->status = PSX_STATUS_STOPPED;
    
    log_info("Emulator shutdown complete");
}

void emulator_destroy(psx_emulator_t *emu) {
    if (emu) {
        emulator_shutdown(emu);
        free(emu);
    }
}