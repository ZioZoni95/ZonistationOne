#include "emulator.h"
#include "cpu.h"
#include "memory.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

psx_result_t emulator_init(psx_emulator_t* emu) {
    memset(emu, 0, sizeof(psx_emulator_t));
    
    // Initialize memory system
    psx_result_t result = memory_init(&emu->memory);
    if (result != PSX_OK) {
        printf("[Emulator] Failed to initialize memory system\n");
        return result;
    }
    
    // Initialize CPU
    cpu_init(&emu->cpu);
    
    // Set initial state
    emu->running = false;
    emu->debug_mode = false;
    
    printf("[Emulator] ZonistationOne PlayStation 1 Emulator initialized\n");
    return PSX_OK;
}

void emulator_shutdown(psx_emulator_t* emu) {
    emu->running = false;
    printf("[Emulator] Shutdown complete\n");
}

psx_result_t emulator_load_bios(psx_emulator_t* emu, const char* bios_path) {
    return memory_load_bios(&emu->memory, bios_path);
}

void emulator_reset(psx_emulator_t* emu) {
    cpu_reset(&emu->cpu);
    emu->running = false;
    printf("[Emulator] Reset complete\n");
}

void emulator_run(psx_emulator_t* emu) {
    if (!emu->memory.bios_loaded) {
        printf("[Emulator] Error: BIOS not loaded, cannot start emulation\n");
        return;
    }
    
    emu->running = true;
    printf("[Emulator] Starting emulation...\n");
    
    u64 instructions_executed = 0;
    const u64 debug_print_interval = 1000000;  // Print debug info every 1M instructions
    
    while (emu->running) {
        // Execute one CPU instruction
        psx_result_t result = cpu_step(&emu->cpu, &emu->memory);
        
        if (result != PSX_OK) {
            printf("[Emulator] CPU error: %d at PC=0x%08X\n", result, emu->cpu.pc);
            break;
        }
        
        instructions_executed++;
        
        // Debug mode: single step with user input
        if (emu->debug_mode) {
            emulator_print_state(emu);
            
            printf("[Debug] Press Enter to continue, 'q' to quit, 'r' for registers: ");
            char input[10];
            if (fgets(input, sizeof(input), stdin)) {
                if (input[0] == 'q') {
                    emu->running = false;
                    break;
                } else if (input[0] == 'r') {
                    cpu_print_registers(&emu->cpu);
                }
            }
        }
        // Normal mode: periodic debug output
        else if (instructions_executed % debug_print_interval == 0) {
            printf("[Emulator] Executed %llu instructions, PC=0x%08X\n",
                   (unsigned long long)instructions_executed, emu->cpu.pc);
        }
        
        // Safety check: prevent infinite loops in early development
        if (instructions_executed > 10000000) {  // 10M instruction limit
            printf("[Emulator] Instruction limit reached, stopping\n");
            break;
        }
    }
    
    printf("[Emulator] Emulation stopped. Executed %llu instructions.\n",
           (unsigned long long)instructions_executed);
}

psx_result_t emulator_step(psx_emulator_t* emu) {
    if (!emu->memory.bios_loaded) {
        return PSX_ERROR_BIOS_NOT_LOADED;
    }
    
    return cpu_step(&emu->cpu, &emu->memory);
}

void emulator_pause(psx_emulator_t* emu) {
    emu->running = false;
    printf("[Emulator] Paused\n");
}

void emulator_resume(psx_emulator_t* emu) {
    if (emu->memory.bios_loaded) {
        emu->running = true;
        printf("[Emulator] Resumed\n");
    } else {
        printf("[Emulator] Cannot resume: BIOS not loaded\n");
    }
}

void emulator_stop(psx_emulator_t* emu) {
    emu->running = false;
    printf("[Emulator] Stopped\n");
}

void emulator_set_debug_mode(psx_emulator_t* emu, bool enabled) {
    emu->debug_mode = enabled;
    printf("[Emulator] Debug mode %s\n", enabled ? "enabled" : "disabled");
}

void emulator_print_state(const psx_emulator_t* emu) {
    printf("[Emulator] State: PC=0x%08X, Cycles=%llu, Running=%s, Debug=%s\n",
           emu->cpu.pc, 
           (unsigned long long)emu->cpu.cycle_count,
           emu->running ? "true" : "false",
           emu->debug_mode ? "true" : "false");
}

psx_result_t emulator_configure(psx_emulator_t* emu, const emulator_config_t* config) {
    psx_result_t result = PSX_OK;
    
    if (config->bios_path) {
        result = emulator_load_bios(emu, config->bios_path);
        if (result != PSX_OK) {
            return result;
        }
    }
    
    emulator_set_debug_mode(emu, config->debug_mode);
    
    if (config->verbose_logging) {
        printf("[Emulator] Verbose logging enabled\n");
    }
    
    return PSX_OK;
}