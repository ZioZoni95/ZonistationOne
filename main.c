#include "include/psx_types.h"
#include "include/psx_cpu.h"
#include "include/psx_memory.h"
#include "include/psx_dma.h"
#include "include/psx_gpu.h"
#include "include/psx_timer.h"
#include "include/psx_irq.h"
#include "include/psx_spu.h"
#include "include/psx_cdrom.h"
#include "include/psx_sio.h"
#include "include/psx_mdec.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <signal.h>
#include <string.h>

// Guide.tex: PlayStation 1 Emulator - Fresh Start
// Following guide.tex structure with PSX-SPX compliance

// Global emulator state
static psx_cpu_t cpu;
static bool running = true;
static bool debug_mode = false;
static u32 instruction_count = 0;

// Signal handler for clean exit
void signal_handler(int sig) {
    printf("\n[MAIN] Caught signal %d, shutting down...\n", sig);
    running = false;
}

void print_usage(const char* program_name) {
    printf("Usage: %s [options]\n", program_name);
    printf("Options:\n");
    printf("  --help          Show this help\n");
    printf("  --debug         Enable debug mode\n");
    printf("  --bios <file>   Specify BIOS file (default: roms/SCPH1001.BIN)\n");
    printf("\nGuide.tex: PlayStation 1 Emulator - Clean Implementation\n");
}

int main(int argc, char* argv[]) {
    printf("=== PlayStation 1 Emulator - Fresh Start ===\n");
    printf("Following guide.tex with PSX-SPX compliance\n");
    printf("Build date: %s %s\n\n", __DATE__, __TIME__);
    
    // Parse command line arguments
    const char* bios_file = "roms/SCPH1001.BIN";
    
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        }
        else if (strcmp(argv[i], "--debug") == 0) {
            debug_mode = true;
            printf("[MAIN] Debug mode enabled\n");
        }
        else if (strcmp(argv[i], "--bios") == 0 && i + 1 < argc) {
            bios_file = argv[++i];
            printf("[MAIN] Using BIOS: %s\n", bios_file);
        }
        else {
            printf("[MAIN] ERROR: Unknown argument: %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }
    
    // Set up signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // Guide.tex: Initialize all subsystems in order
    printf("\n=== Initializing PlayStation Hardware ===\n");
    
    // Initialize memory first (needed by all components)
    memory_init();
    
    // Load BIOS
    if (!memory_load_bios(bios_file)) {
        printf("[MAIN] ERROR: Failed to load BIOS\n");
        return 1;
    }
    
    // Initialize hardware components
    dma_init();
    gpu_init();
    timer_init();
    irq_init();
    spu_init();
    cdrom_init();
    sio_init();
    mdec_init();
    
    // Initialize CPU last
    cpu_init(&cpu);
    
    printf("\n=== PlayStation Hardware Initialized ===\n");
    printf("Starting emulation...\n\n");
    
    // Guide.tex: Main emulation loop
    while (running) {
        // Step CPU (executes one instruction)
        cpu_step(&cpu);
        instruction_count++;
        
        // Step hardware components (simplified timing for now)
        u32 cycles = 1; // TODO: Implement proper cycle counting
        
        timer_step(cycles);
        gpu_step();
        dma_step();
        irq_step();
        spu_step();
        cdrom_step();
        sio_step();
        mdec_step();
        
        // Debug output
        if (debug_mode && (instruction_count % 1000) == 0) {
            printf("[DEBUG] Instructions: %u, PC: 0x%08X\n", 
                   instruction_count, cpu.pc);
        }
        
        // Simple exit condition for now
        if (instruction_count > 100000) {
            printf("[MAIN] Instruction limit reached, stopping\n");
            break;
        }
        
        // Check for exceptions or special conditions
        if (cpu.exception_pending) {
            printf("[MAIN] CPU exception pending, handling...\n");
            // TODO: Handle exceptions
            break;
        }
    }
    
    printf("\n=== Shutting Down ===\n");
    printf("Total instructions executed: %u\n", instruction_count);
    
    // Cleanup
    memory_shutdown();
    
    printf("PlayStation 1 Emulator shutdown complete.\n");
    return 0;
}