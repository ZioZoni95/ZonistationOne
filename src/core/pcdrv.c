/* SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 ZioZoni95
 *
 * Part of ZoniStation One, a PlayStation 1 emulator.
 * See LICENSE for the full licence text and THIRD-PARTY.md for the
 * components of this project that have other authors.
 */
#include "pcdrv.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "log.h" 
#include "interconnect.h"

#define MAX_FILES 100
static FILE* s_files[MAX_FILES];
static bool s_initialized = false;

void PCDrv_Initialize(void) {
    for (int i = 0; i < MAX_FILES; i++) {
        s_files[i] = NULL;
    }
    s_initialized = true;
}

void PCDrv_Shutdown(void) {
    if (!s_initialized) return;
    for (int i = 0; i < MAX_FILES; i++) {
        if (s_files[i]) {
            fclose(s_files[i]);
            s_files[i] = NULL;
        }
    }
    s_initialized = false;
}

void PCDrv_Reset(void) {
    PCDrv_Shutdown();
    PCDrv_Initialize();
}

static int GetFreeFileHandle(void) {
    for (int i = 0; i < MAX_FILES; i++) {
        if (!s_files[i]) return i;
    }
    return -1;
}

// Helper to read string from emulated memory
static void ReadString(Cpu* cpu, uint32_t addr, char* buffer, int max_len) {
    int i;
    for (i = 0; i < max_len - 1; i++) {
        uint8_t c = interconnect_load8(cpu->inter, addr + i);
        if (c == 0) break;
        buffer[i] = (char)c;
    }
    buffer[i] = '\0';
}

bool PCDrv_HandleSyscall(Cpu* cpu, uint32_t instruction) {
    if (!s_initialized) PCDrv_Initialize();

    // instruction contains the break code in bits 25-6 (20 bits)
    // op=000000 (6 bits) | code (20 bits) | funct=001101 (6 bits)
    // Actually MIPS break is: 000000 | code (20 bits) | 001101
    // The `code` variable in DuckStation is extracted as (instruction >> 6) & 0xfffff
    uint32_t code = (instruction >> 6) & 0xfffff;
    
    // Check if it's a PCDrv syscall (0x101 - 0x107)
    if (code < 0x101 || code > 0x107) return false;

    // Registers: a0=$4, a1=$5, a2=$6, a3=$7
    // Returns: v0=$2, v1=$3
    
    // Default error handling macro/block
    // Sets v0 and v1 to -1 (0xffffffff)
    #define RETURN_ERROR() do { \
        cpu_set_reg(cpu, 2, 0xffffffff); \
        cpu_set_reg(cpu, 3, 0xffffffff); \
        return true; \
    } while(0)

    switch (code) {
        case 0x101: // PCinit
            PCDrv_Reset(); // Close all files
            cpu_set_reg(cpu, 2, 0); // v0
            cpu_set_reg(cpu, 3, 0); // v1
            return true;

        case 0x102: // PCcreat
        case 0x103: // PCopen
        {
            uint32_t name_addr = cpu_reg(cpu, 5); // a1
            uint32_t mode = cpu_reg(cpu, 6);      // a2
            char filename[256];
            ReadString(cpu, name_addr, filename, sizeof(filename));
            
            const char* mode_str;
            if (code == 0x103) { // PCopen
                 // mode: 0=READ, 1=WRITE, 2=RW
                 if (mode == 0) mode_str = "rb";
                 else if (mode == 1) mode_str = "w+b"; 
                 else mode_str = "r+b";
            } else { // PCcreat
                mode_str = "w+b";
            }
            
            // Simple logging
            printf("PCDrv: %s '%s' mode %d (%s)\n", (code==0x102)?"PCcreat":"PCopen", filename, mode, mode_str);

            int handle = GetFreeFileHandle();
            if (handle < 0) RETURN_ERROR();

            s_files[handle] = fopen(filename, mode_str);
            if (!s_files[handle]) {
                printf("PCDrv: Failed to open '%s'\n", filename);
                RETURN_ERROR();
            }

            cpu_set_reg(cpu, 2, 0);
            cpu_set_reg(cpu, 3, handle);
            return true;
        }
        
        case 0x104: // PCclose
        {
            uint32_t handle = cpu_reg(cpu, 5); // a1
            if (handle >= MAX_FILES || !s_files[handle]) RETURN_ERROR();
            
            fclose(s_files[handle]);
            s_files[handle] = NULL;
            cpu_set_reg(cpu, 2, 0);
            cpu_set_reg(cpu, 3, 0);
            return true;
        }

        case 0x105: // PCread
        {
            uint32_t handle = cpu_reg(cpu, 5); // a1
            uint32_t count = cpu_reg(cpu, 6);  // a2
            uint32_t dst_addr = cpu_reg(cpu, 7); // a3

            if (handle >= MAX_FILES || !s_files[handle]) RETURN_ERROR();
            
            uint32_t i;
            for (i = 0; i < count; i++) {
                uint8_t val;
                if (fread(&val, 1, 1, s_files[handle]) != 1) {
                    if (ferror(s_files[handle])) RETURN_ERROR();
                    break; // EOF
                }
                interconnect_store8(cpu->inter, dst_addr + i, val);
            }
            
            cpu_set_reg(cpu, 2, 0);
            cpu_set_reg(cpu, 3, i); // bytes read
            return true;
        }

        case 0x106: // PCwrite
        {
            uint32_t handle = cpu_reg(cpu, 5); // a1
            uint32_t count = cpu_reg(cpu, 6);  // a2
            uint32_t src_addr = cpu_reg(cpu, 7); // a3

             if (handle >= MAX_FILES || !s_files[handle]) RETURN_ERROR();

            uint32_t i;
            for (i = 0; i < count; i++) {
                uint8_t val = interconnect_load8(cpu->inter, src_addr + i);
                if (fwrite(&val, 1, 1, s_files[handle]) != 1) {
                    RETURN_ERROR();
                }
            }
            cpu_set_reg(cpu, 2, 0);
            cpu_set_reg(cpu, 3, i);
            return true;
        }

        case 0x107: // PClseek
        {
             uint32_t handle = cpu_reg(cpu, 5); // a1
             int32_t offset = (int32_t)cpu_reg(cpu, 6); // a2
             uint32_t mode = cpu_reg(cpu, 7); // a3
             
             if (handle >= MAX_FILES || !s_files[handle]) RETURN_ERROR();

            int origin = SEEK_SET;
            if (mode == 1) origin = SEEK_CUR;
            if (mode == 2) origin = SEEK_END;

            if (fseek(s_files[handle], offset, origin) != 0) RETURN_ERROR();
            
            long pos = ftell(s_files[handle]);
            cpu_set_reg(cpu, 2, 0);
            cpu_set_reg(cpu, 3, (uint32_t)pos);
            return true;
        }
    }
    return false;
}
