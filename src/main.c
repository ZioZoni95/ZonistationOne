#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>

#include "emulator.h"
#include "psx_types.h"

#define VERSION "0.1.0"

static void print_usage(const char* program_name) {
    printf("ZonistationOne PlayStation 1 Emulator v%s\n", VERSION);
    printf("Usage: %s [options]\n\n", program_name);
    printf("Options:\n");
    printf("  -b, --bios PATH     Path to PlayStation BIOS file (required)\n");
    printf("  -d, --debug         Enable debug mode (single-step execution)\n");
    printf("  -v, --verbose       Enable verbose logging\n");
    printf("  -h, --help          Show this help message\n");
    printf("      --version       Show version information\n");
    printf("\nExample:\n");
    printf("  %s --bios roms/SCPH1001.BIN --debug\n", program_name);
    printf("\nBased on PCSX-Redux reference implementation.\n");
    printf("This is an educational emulator for learning purposes.\n");
}

static void print_version(void) {
    printf("ZonistationOne PlayStation 1 Emulator\n");
    printf("Version: %s\n", VERSION);
    printf("Built: %s %s\n", __DATE__, __TIME__);
    printf("Reference: PCSX-Redux (https://github.com/grumpycoders/pcsx-redux)\n");
}

int main(int argc, char* argv[]) {
    // Default configuration
    emulator_config_t config = {
        .debug_mode = false,
        .verbose_logging = false,
        .bios_path = NULL
    };
    
    // Command line option parsing
    static struct option long_options[] = {
        {"bios",    required_argument, 0, 'b'},
        {"debug",   no_argument,       0, 'd'},
        {"verbose", no_argument,       0, 'v'},
        {"help",    no_argument,       0, 'h'},
        {"version", no_argument,       0, 0},
        {0, 0, 0, 0}
    };
    
    int option_index = 0;
    int c;
    
    while ((c = getopt_long(argc, argv, "b:dvh", long_options, &option_index)) != -1) {
        switch (c) {
            case 'b':
                config.bios_path = optarg;
                break;
            case 'd':
                config.debug_mode = true;
                break;
            case 'v':
                config.verbose_logging = true;
                break;
            case 'h':
                print_usage(argv[0]);
                return 0;
            case 0:
                if (strcmp(long_options[option_index].name, "version") == 0) {
                    print_version();
                    return 0;
                }
                break;
            case '?':
                printf("Use --help for usage information\n");
                return 1;
            default:
                abort();
        }
    }
    
    // Check for required BIOS path
    if (!config.bios_path) {
        // Try default BIOS location
        if (access("roms/SCPH1001.BIN", R_OK) == 0) {
            config.bios_path = "roms/SCPH1001.BIN";
            printf("[Main] Using default BIOS: %s\n", config.bios_path);
        } else {
            printf("Error: BIOS file not specified and default not found\n");
            printf("Use --bios to specify BIOS file path, or place SCPH1001.BIN in roms/\n");
            return 1;
        }
    }
    
    // Verify BIOS file exists
    if (access(config.bios_path, R_OK) != 0) {
        printf("Error: Cannot read BIOS file: %s\n", config.bios_path);
        return 1;
    }
    
    printf("=== ZonistationOne PlayStation 1 Emulator v%s ===\n", VERSION);
    
    // Initialize emulator
    psx_emulator_t emulator;
    psx_result_t result = emulator_init(&emulator);
    if (result != PSX_OK) {
        printf("Error: Failed to initialize emulator (code %d)\n", result);
        return 1;
    }
    
    // Configure emulator
    result = emulator_configure(&emulator, &config);
    if (result != PSX_OK) {
        printf("Error: Failed to configure emulator (code %d)\n", result);
        emulator_shutdown(&emulator);
        return 1;
    }
    
    // Print initial state
    if (config.verbose_logging) {
        printf("\n=== Initial System State ===\n");
        emulator_print_state(&emulator);
        cpu_print_registers(&emulator.cpu);
        
        printf("\n=== BIOS Memory Dump (first 64 bytes) ===\n");
        memory_dump_region(&emulator.memory, 0xBFC00000, 64);
    }
    
    printf("\n=== Starting Emulation ===\n");
    if (config.debug_mode) {
        printf("Debug mode enabled - emulator will single-step\n");
        printf("Commands: Enter=step, 'q'=quit, 'r'=show registers\n");
    }
    
    // Start emulation
    emulator_run(&emulator);
    
    // Cleanup
    printf("\n=== Final System State ===\n");
    emulator_print_state(&emulator);
    
    if (config.verbose_logging) {
        printf("\n=== Final CPU Registers ===\n");
        cpu_print_registers(&emulator.cpu);
    }
    
    emulator_shutdown(&emulator);
    
    printf("\n=== Emulation Complete ===\n");
    return 0;
}