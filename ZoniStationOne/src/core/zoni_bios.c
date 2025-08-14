/**
 * @file zoni_bios.c
 * @brief BIOS management implementation for ZoniStationOne
 */

#include "zoni_bios.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// BIOS file validation
static bool zoni_bios_validate_size(u32 size) {
    // PlayStation BIOS files are typically 512KB (524,288 bytes)
    // Some variants might be 4MB, but we'll focus on the standard size
    return (size == 512 * 1024) || (size == 4 * 1024 * 1024);
}

// BIOS version detection
static void zoni_bios_detect_version(zoni_bios_info_t* info, const u8* bios_data) {
    // Extract version information from BIOS data
    // PlayStation BIOS typically has version info at specific offsets
    
    // Default to unknown
    strcpy(info->version, "Unknown");
    strcpy(info->region, "Unknown");
    strcpy(info->date, "Unknown");
    
    // Check for common BIOS signatures
    if (bios_data && info->size >= 0x80000) {
        // Look for version string in BIOS data
        // Common locations: 0x12C, 0x7FF32, etc.
        
        // Check for SCPH-1001 signature
        if (memcmp(bios_data + 0x12C, "CEX-3000/1001", 13) == 0) {
            strcpy(info->version, "SCPH-1001");
            strcpy(info->region, "NTSC-U");
            strcpy(info->date, "12/04/95");
        }
        // Check for SCPH-5501 signature
        else if (memcmp(bios_data + 0x12C, "SCPH-5501", 9) == 0) {
            strcpy(info->version, "SCPH-5501");
            strcpy(info->region, "NTSC-U");
            strcpy(info->date, "08/18/97");
        }
        // Check for SCPH-7001 signature
        else if (memcmp(bios_data + 0x12C, "SCPH-7001", 9) == 0) {
            strcpy(info->version, "SCPH-7001");
            strcpy(info->region, "NTSC-U");
            strcpy(info->date, "12/16/97");
        }
        // Check for SCPH-101 signature
        else if (memcmp(bios_data + 0x12C, "SCPH-101", 8) == 0) {
            strcpy(info->version, "SCPH-101");
            strcpy(info->region, "NTSC-J");
            strcpy(info->date, "11/18/94");
        }
        // Check for SCPH-5500 signature
        else if (memcmp(bios_data + 0x12C, "SCPH-5500", 9) == 0) {
            strcpy(info->version, "SCPH-5500");
            strcpy(info->region, "NTSC-J");
            strcpy(info->date, "08/18/97");
        }
        // Check for SCPH-7000 signature
        else if (memcmp(bios_data + 0x12C, "SCPH-7000", 9) == 0) {
            strcpy(info->version, "SCPH-7000");
            strcpy(info->region, "NTSC-J");
            strcpy(info->date, "12/16/97");
        }
        // Check for SCPH-5502 signature
        else if (memcmp(bios_data + 0x12C, "SCPH-5502", 9) == 0) {
            strcpy(info->version, "SCPH-5502");
            strcpy(info->region, "PAL");
            strcpy(info->date, "08/18/97");
        }
        // Check for SCPH-7002 signature
        else if (memcmp(bios_data + 0x12C, "SCPH-7002", 9) == 0) {
            strcpy(info->version, "SCPH-7002");
            strcpy(info->region, "PAL");
            strcpy(info->date, "12/16/97");
        }
        // Check for SCPH-5503 signature
        else if (memcmp(bios_data + 0x12C, "SCPH-5503", 9) == 0) {
            strcpy(info->version, "SCPH-5503");
            strcpy(info->region, "PAL");
            strcpy(info->date, "08/18/97");
        }
        // Check for SCPH-7003 signature
        else if (memcmp(bios_data + 0x12C, "SCPH-7003", 9) == 0) {
            strcpy(info->version, "SCPH-7003");
            strcpy(info->region, "PAL");
            strcpy(info->date, "12/16/97");
        }
    }
}

zoni_error_t zoni_bios_init(zoni_bios_t* bios) {
    if (!bios) {
        return ZONI_ERROR_INVALID_PARAMETER;
    }
    
    // Initialize BIOS structure
    memset(bios, 0, sizeof(zoni_bios_t));
    
    // Set default values
    bios->hle_mode = false;
    bios->slow_boot = false;
    bios->boot_pc = 0x80030000;  // Default boot PC
    bios->boot_gp = 0x00000000;  // Default GP
    bios->boot_sp = 0x801FFF00;  // Default SP
    
    // Initialize BIOS info
    memset(&bios->info, 0, sizeof(zoni_bios_info_t));
    bios->info.loaded = false;
    bios->info.valid = false;
    
    zoni_log(ZONI_LOG_INFO, "BIOS system initialized");
    return ZONI_SUCCESS;
}

void zoni_bios_shutdown(zoni_bios_t* bios) {
    if (!bios) return;
    
    // Clear BIOS structure
    memset(bios, 0, sizeof(zoni_bios_t));
    
    zoni_log(ZONI_LOG_INFO, "BIOS system shutdown");
}

void zoni_bios_reset(zoni_bios_t* bios) {
    if (!bios) return;
    
    // Reset BIOS state but keep loaded BIOS data
    bios->boot_pc = 0x80030000;
    bios->boot_gp = 0x00000000;
    bios->boot_sp = 0x801FFF00;
    
    zoni_log(ZONI_LOG_INFO, "BIOS system reset");
}

zoni_error_t zoni_bios_validate_file(const char* filename, zoni_bios_info_t* info) {
    if (!filename || !info) {
        return ZONI_ERROR_INVALID_PARAMETER;
    }
    
    FILE* file = fopen(filename, "rb");
    if (!file) {
        zoni_log(ZONI_LOG_ERROR, "Could not open BIOS file: %s", filename);
        return ZONI_ERROR_FILE_NOT_FOUND;
    }
    
    // Get file size
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    // Validate file size
    if (!zoni_bios_validate_size(size)) {
        fclose(file);
        zoni_log(ZONI_LOG_ERROR, "Invalid BIOS file size: %ld bytes", size);
        return ZONI_ERROR_INVALID_FORMAT;
    }
    
    // Read BIOS data for version detection
    u8* bios_data = malloc(size);
    if (!bios_data) {
        fclose(file);
        return ZONI_ERROR_OUT_OF_MEMORY;
    }
    
    if (fread(bios_data, 1, size, file) != (size_t)size) {
        free(bios_data);
        fclose(file);
        return ZONI_ERROR_FILE_NOT_FOUND;
    }
    
    fclose(file);
    
    // Initialize info structure
    memset(info, 0, sizeof(zoni_bios_info_t));
    strncpy(info->filename, filename, sizeof(info->filename) - 1);
    info->size = size;
    info->valid = true;
    
    // Detect BIOS version
    zoni_bios_detect_version(info, bios_data);
    
    free(bios_data);
    
    zoni_log(ZONI_LOG_INFO, "BIOS file validated: %s (%s, %s, %s)", 
             info->filename, info->version, info->region, info->date);
    
    return ZONI_SUCCESS;
}

zoni_error_t zoni_bios_load_file(zoni_bios_t* bios, zoni_memory_t* memory, const char* filename) {
    if (!bios || !memory || !filename) {
        return ZONI_ERROR_INVALID_PARAMETER;
    }
    
    // Validate BIOS file first
    zoni_bios_info_t info;
    zoni_error_t result = zoni_bios_validate_file(filename, &info);
    if (result != ZONI_SUCCESS) {
        return result;
    }
    
    // Open and read BIOS file
    FILE* file = fopen(filename, "rb");
    if (!file) {
        zoni_log(ZONI_LOG_ERROR, "Could not open BIOS file: %s", filename);
        return ZONI_ERROR_FILE_NOT_FOUND;
    }
    
    // Read BIOS data into memory
    size_t bytes_read = fread(memory->bios, 1, PSX_BIOS_SIZE, file);
    fclose(file);
    
    if (bytes_read != PSX_BIOS_SIZE) {
        zoni_log(ZONI_LOG_ERROR, "Failed to read BIOS file: expected %d bytes, got %zu", 
                 PSX_BIOS_SIZE, bytes_read);
        return ZONI_ERROR_FILE_NOT_FOUND;
    }
    
    // Update BIOS info
    bios->info = info;
    bios->info.loaded = true;
    bios->hle_mode = false;
    
    zoni_log(ZONI_LOG_INFO, "BIOS loaded successfully: %s (%s, %s)", 
             info.version, info.region, info.date);
    
    return ZONI_SUCCESS;
}

zoni_error_t zoni_bios_load_default(zoni_bios_t* bios, zoni_memory_t* memory) {
    // Try to load common BIOS filenames
    const char* bios_files[] = {
        "../bios_files/SCPH1001.BIN",
        "../bios_files/SCPH5501.BIN",
        "../bios_files/SCPH7001.BIN",
        "../bios_files/SCPH101.BIN",
        "../bios_files/SCPH5500.BIN",
        "../bios_files/SCPH7000.BIN",
        "../bios_files/SCPH5502.BIN",
        "../bios_files/SCPH7002.BIN",
        "../bios_files/SCPH5503.BIN",
        "../bios_files/SCPH7003.BIN",
        NULL
    };
    
    for (int i = 0; bios_files[i] != NULL; i++) {
        zoni_error_t result = zoni_bios_load_file(bios, memory, bios_files[i]);
        if (result == ZONI_SUCCESS) {
            return ZONI_SUCCESS;
        }
    }
    
    // If no BIOS file found, enable HLE mode
    zoni_log(ZONI_LOG_WARNING, "No BIOS file found, enabling HLE mode");
    bios->hle_mode = true;
    bios->info.loaded = false;
    strcpy(bios->info.version, "HLE");
    strcpy(bios->info.region, "HLE");
    strcpy(bios->info.date, "HLE");
    
    return ZONI_ERROR_FILE_NOT_FOUND;
}

zoni_error_t zoni_bios_setup_boot_state(zoni_bios_t* bios, zoni_cpu_regs_t* cpu) {
    if (!bios || !cpu) {
        return ZONI_ERROR_INVALID_PARAMETER;
    }
    
    if (bios->hle_mode) {
        // HLE mode: Set up basic boot state
        cpu->pc = bios->boot_pc;
        cpu->gpr.r[28] = bios->boot_gp;  // GP register
        cpu->gpr.r[29] = bios->boot_sp;  // SP register
        cpu->gpr.r[30] = bios->boot_sp;  // FP register
        
        // Set up some basic registers for HLE
        cpu->gpr.r[8] = bios->boot_sp;   // T0 register
        cpu->gpr.r[11] = bios->boot_pc;  // T3 register
        
        zoni_log(ZONI_LOG_INFO, "HLE BIOS boot state configured");
    } else {
        // Real BIOS mode: Set up for BIOS execution
        cpu->pc = 0xBFC00000;  // BIOS entry point
        cpu->gpr.r[28] = 0x00000000;  // GP register
        cpu->gpr.r[29] = 0x801FFF00;  // SP register
        cpu->gpr.r[30] = 0x801FFF00;  // FP register
        
        // Set up CP0 registers for BIOS execution
        // Status Register: Enable COP0, BEV=1 (Bootstrap Exception Vectors), TS=1 (TLB Shutdown)
        // Set cache status bits that BIOS expects for cache initialization
        // Use the same Status Register value as the reference implementation
        // 0x10600000: COP0 enabled | BEV = 1 | TS = 1
        cpu->cp0.n.SR = 0x10600000;  // COP0 enabled | BEV = 1 | TS = 1
        cpu->cp0.n.PRid = 0x00000002; // PRevID = Revision ID (R3000A)
        
        zoni_log(ZONI_LOG_INFO, "Real BIOS boot state configured");
    }
    
    return ZONI_SUCCESS;
}

zoni_error_t zoni_bios_execute(zoni_bios_t* bios, zoni_cpu_regs_t* cpu, zoni_memory_t* memory) {
    if (!bios || !cpu || !memory) {
        return ZONI_ERROR_INVALID_PARAMETER;
    }
    
    if (bios->hle_mode) {
        // HLE mode: Skip BIOS execution
        zoni_log(ZONI_LOG_INFO, "HLE BIOS mode - skipping BIOS execution");
        return ZONI_SUCCESS;
    }
    
    // Execute BIOS with reduced timeout for early debugging
    const int max_cycles = 100000;  // 100K cycles for early debugging
    int cycles = 0;
    u32 last_pc = 0;
    int same_pc_count = 0;
    
    zoni_log(ZONI_LOG_INFO, "Executing BIOS (development mode, max %d cycles)...", max_cycles);
    
    while (cycles < max_cycles) {
        zoni_error_t result = zoni_cpu_step(cpu);
        if (result != ZONI_SUCCESS) {
            // For now, ignore hardware register access errors
            // These are expected since we haven't implemented hardware emulation yet
            if (cpu->pc >= 0xBFC00000 && cpu->pc < 0xBFC00000 + 0x80000) {
                // Still in BIOS area, continue execution
                cycles++;
                continue;
            } else {
                zoni_log(ZONI_LOG_ERROR, "BIOS execution failed at cycle %d, PC=0x%08X", cycles, cpu->pc);
                return result;
            }
        }
        
        cycles++;
        
        // Check if BIOS execution has ended
        if (zoni_bios_execution_ended(cpu)) {
            zoni_log(ZONI_LOG_INFO, "✅ BIOS execution completed after %d cycles", cycles);
            return ZONI_SUCCESS;
        }
        
        // Detect if we're stuck in a loop
        if (cpu->pc == last_pc) {
            same_pc_count++;
            if (same_pc_count > 1000) {
                zoni_log(ZONI_LOG_WARNING, "⚠️ BIOS appears stuck at PC=0x%08X for %d cycles", cpu->pc, same_pc_count);
                zoni_log(ZONI_LOG_INFO, "🔍 This suggests missing hardware implementation");
                break;
            }
        } else {
            same_pc_count = 0;
            last_pc = cpu->pc;
        }
        
        // Log progress every 5000 cycles with more detail
        if (cycles % 5000 == 0) {
            zoni_log(ZONI_LOG_INFO, "BIOS: %d cycles, PC=0x%08X, same_pc=%d", cycles, cpu->pc, same_pc_count);
        }
    }
    
    zoni_log(ZONI_LOG_WARNING, "⏰ BIOS execution timeout after %d cycles", cycles);
    zoni_log(ZONI_LOG_INFO, "📊 Final state: PC=0x%08X, cycles=%d", cpu->pc, cycles);
    zoni_log(ZONI_LOG_INFO, "🔍 Next steps: Implement GPU, SPU, CD-ROM, or controller input");
    return ZONI_SUCCESS;
}

bool zoni_bios_execution_ended(zoni_cpu_regs_t* cpu) {
    if (!cpu) return false;
    
    // BIOS execution typically ends when PC reaches RAM area (0x80000000)
    // This indicates the BIOS has finished booting and is ready to load a game
    return (cpu->pc & 0xFF800000) == 0x80000000;
}

const char* zoni_bios_get_version(zoni_bios_t* bios) {
    if (!bios) return "Unknown";
    return bios->info.version;
}

const char* zoni_bios_get_region(zoni_bios_t* bios) {
    if (!bios) return "Unknown";
    return bios->info.region;
}

bool zoni_bios_is_loaded(zoni_bios_t* bios) {
    if (!bios) return false;
    return bios->info.loaded;
}

bool zoni_bios_is_hle(zoni_bios_t* bios) {
    if (!bios) return false;
    return bios->hle_mode;
}

void zoni_bios_set_hle_mode(zoni_bios_t* bios, bool enabled) {
    if (!bios) return;
    bios->hle_mode = enabled;
}

void zoni_bios_set_slow_boot(zoni_bios_t* bios, bool enabled) {
    if (!bios) return;
    bios->slow_boot = enabled;
} 