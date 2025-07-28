/**
 * BIOS Disassembler Tool
 * 
 * This tool loads PlayStation BIOS files and disassembles them
 * using our existing instruction decode function.
 * 
 * Features:
 * - Load PlayStation BIOS files (.bin format)
 * - Disassemble MIPS instructions
 * - Analyze instruction patterns
 * - Identify system calls and exceptions
 * - Generate detailed analysis report
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h> // for strdup

// Include our emulator headers
#include "../include/zoni_common.h"
#include "../include/zoni_cpu.h"

// BIOS file structure
typedef struct {
    char* filename;
    uint8_t* data;
    size_t size;
    uint32_t load_address;
} bios_file_t;

// Disassembly statistics
typedef struct {
    uint32_t total_instructions;
    uint32_t unknown_instructions;
    uint32_t syscall_count;
    uint32_t exception_count;
    uint32_t jump_count;
    uint32_t branch_count;
    uint32_t load_store_count;
    uint32_t arithmetic_count;
    uint32_t logical_count;
} disasm_stats_t;

// Function prototypes
bios_file_t* load_bios_file(const char* filename);
void free_bios_file(bios_file_t* bios);
void disassemble_bios(bios_file_t* bios, const char* output_file);
void analyze_instruction_patterns(bios_file_t* bios, disasm_stats_t* stats);
void print_disassembly_report(disasm_stats_t* stats, const char* filename);
void find_system_calls(bios_file_t* bios);
void find_exception_vectors(bios_file_t* bios);

// Main function
int main(int argc, char* argv[]) {
    printf("ZoniStationOne BIOS Disassembler v0.1.0\n");
    printf("=========================================\n\n");
    
    if (argc < 2) {
        printf("Usage: %s <bios_file.bin> [output_file.txt]\n", argv[0]);
        printf("\nThis tool disassembles PlayStation BIOS files and analyzes:\n");
        printf("- Instruction patterns and frequencies\n");
        printf("- System call locations and patterns\n");
        printf("- Exception vector handling\n");
        printf("- Memory access patterns\n");
        printf("- Boot sequence analysis\n\n");
        return 1;
    }
    
    const char* bios_filename = argv[1];
    const char* output_filename = (argc > 2) ? argv[2] : "bios_disassembly.txt";
    
    printf("Loading BIOS file: %s\n", bios_filename);
    
    // Load the BIOS file
    bios_file_t* bios = load_bios_file(bios_filename);
    if (!bios) {
        printf("ERROR: Failed to load BIOS file: %s\n", bios_filename);
        return 1;
    }
    
    printf("BIOS loaded successfully:\n");
    printf("- Size: %zu bytes\n", bios->size);
    printf("- Load address: 0x%08X\n", bios->load_address);
    printf("- Instructions: %zu\n\n", bios->size / 4);
    
    // Disassemble the BIOS
    printf("Disassembling BIOS...\n");
    disassemble_bios(bios, output_filename);
    
    // Analyze instruction patterns
    printf("Analyzing instruction patterns...\n");
    disasm_stats_t stats = {0};
    analyze_instruction_patterns(bios, &stats);
    
    // Print analysis report
    print_disassembly_report(&stats, bios_filename);
    
    // Find system calls
    printf("\nFinding system calls...\n");
    find_system_calls(bios);
    
    // Find exception vectors
    printf("\nFinding exception vectors...\n");
    find_exception_vectors(bios);
    
    // Cleanup
    free_bios_file(bios);
    
    printf("\nDisassembly complete! Output saved to: %s\n", output_filename);
    return 0;
}

// Load BIOS file from disk
bios_file_t* load_bios_file(const char* filename) {
    FILE* file = fopen(filename, "rb");
    if (!file) {
        printf("ERROR: Cannot open file: %s\n", filename);
        return NULL;
    }
    
    // Get file size
    fseek(file, 0, SEEK_END);
    size_t size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    if (size == 0) {
        printf("ERROR: File is empty: %s\n", filename);
        fclose(file);
        return NULL;
    }
    
    // Allocate memory for BIOS data
    uint8_t* data = malloc(size);
    if (!data) {
        printf("ERROR: Failed to allocate memory for BIOS data\n");
        fclose(file);
        return NULL;
    }
    
    // Read BIOS data
    size_t bytes_read = fread(data, 1, size, file);
    fclose(file);
    
    if (bytes_read != size) {
        printf("ERROR: Failed to read BIOS file completely\n");
        free(data);
        return NULL;
    }
    
    // Create BIOS file structure
    bios_file_t* bios = malloc(sizeof(bios_file_t));
    if (!bios) {
        printf("ERROR: Failed to allocate BIOS file structure\n");
        free(data);
        return NULL;
    }
    
    bios->filename = strdup(filename);
    bios->data = data;
    bios->size = size;
    bios->load_address = 0x1FC00000; // PlayStation BIOS load address
    
    return bios;
}

// Free BIOS file resources
void free_bios_file(bios_file_t* bios) {
    if (bios) {
        if (bios->filename) free(bios->filename);
        if (bios->data) free(bios->data);
        free(bios);
    }
}

// Disassemble BIOS instructions
void disassemble_bios(bios_file_t* bios, const char* output_file) {
    FILE* out = fopen(output_file, "w");
    if (!out) {
        printf("ERROR: Cannot create output file: %s\n", output_file);
        return;
    }
    
    fprintf(out, "PlayStation BIOS Disassembly\n");
    fprintf(out, "============================\n\n");
    fprintf(out, "File: %s\n", bios->filename);
    fprintf(out, "Size: %zu bytes\n", bios->size);
    fprintf(out, "Load Address: 0x%08X\n", bios->load_address);
    fprintf(out, "Instructions: %zu\n\n", bios->size / 4);
    fprintf(out, "Address     | Instruction | Disassembly\n");
    fprintf(out, "------------|-------------|-------------\n");
    
    // Process each 4-byte instruction
    for (size_t i = 0; i < bios->size; i += 4) {
        uint32_t address = bios->load_address + i;
        
        // Read instruction (little-endian)
        uint32_t instruction = 
            (bios->data[i+3] << 24) |
            (bios->data[i+2] << 16) |
            (bios->data[i+1] << 8) |
            (bios->data[i+0]);
        
        // Create instruction structure for our decode function
        zoni_instruction_t inst;
        inst.raw = instruction;
        
        // Decode the instruction
        char disasm[256];
        zoni_error_t result = zoni_cpu_decode_instruction(&inst, disasm, sizeof(disasm));
        
        if (result == ZONI_SUCCESS) {
            fprintf(out, "0x%08X | 0x%08X | %s\n", address, instruction, disasm);
        } else {
            fprintf(out, "0x%08X | 0x%08X | UNKNOWN\n", address, instruction);
        }
    }
    
    fclose(out);
}

// Analyze instruction patterns and frequencies
void analyze_instruction_patterns(bios_file_t* bios, disasm_stats_t* stats) {
    memset(stats, 0, sizeof(disasm_stats_t));
    
    for (size_t i = 0; i < bios->size; i += 4) {
        // Read instruction (little-endian)
        uint32_t instruction = 
            (bios->data[i+3] << 24) |
            (bios->data[i+2] << 16) |
            (bios->data[i+1] << 8) |
            (bios->data[i+0]);
        
        // Extract opcode
        uint8_t opcode = (instruction >> 26) & 0x3F;
        uint8_t funct = instruction & 0x3F;
        
        stats->total_instructions++;
        
        // Categorize instructions
        switch (opcode) {
            case 0x00: // SPECIAL
                switch (funct) {
                    case 0x0C: // SYSCALL
                        stats->syscall_count++;
                        break;
                    case 0x0D: // BREAK
                        stats->exception_count++;
                        break;
                    case 0x08: // JR
                    case 0x09: // JALR
                        stats->jump_count++;
                        break;
                    default:
                        stats->arithmetic_count++;
                        break;
                }
                break;
                
            case 0x02: // J
            case 0x03: // JAL
                stats->jump_count++;
                break;
                
            case 0x04: // BEQ
            case 0x05: // BNE
            case 0x06: // BLEZ
            case 0x07: // BGTZ
                stats->branch_count++;
                break;
                
            case 0x20: // LB
            case 0x21: // LH
            case 0x23: // LW
            case 0x24: // LBU
            case 0x25: // LHU
            case 0x28: // SB
            case 0x29: // SH
            case 0x2B: // SW
                stats->load_store_count++;
                break;
                
            case 0x08: // ADDI
            case 0x09: // ADDIU
            case 0x0A: // SLTI
            case 0x0B: // SLTIU
            case 0x0C: // ANDI
            case 0x0D: // ORI
            case 0x0E: // XORI
            case 0x0F: // LUI
                stats->arithmetic_count++;
                break;
                
            default:
                stats->unknown_instructions++;
                break;
        }
    }
}

// Print disassembly analysis report
void print_disassembly_report(disasm_stats_t* stats, const char* filename) {
    printf("\nBIOS Disassembly Analysis Report\n");
    printf("================================\n");
    printf("File: %s\n\n", filename);
    
    printf("Instruction Statistics:\n");
    printf("- Total Instructions: %u\n", stats->total_instructions);
    printf("- Unknown Instructions: %u (%.1f%%)\n", 
           stats->unknown_instructions, 
           (float)stats->unknown_instructions / stats->total_instructions * 100);
    
    printf("\nInstruction Categories:\n");
    printf("- System Calls (SYSCALL): %u (%.1f%%)\n", 
           stats->syscall_count, 
           (float)stats->syscall_count / stats->total_instructions * 100);
    printf("- Exceptions (BREAK): %u (%.1f%%)\n", 
           stats->exception_count, 
           (float)stats->exception_count / stats->total_instructions * 100);
    printf("- Jumps (J, JAL, JR, JALR): %u (%.1f%%)\n", 
           stats->jump_count, 
           (float)stats->jump_count / stats->total_instructions * 100);
    printf("- Branches (BEQ, BNE, etc.): %u (%.1f%%)\n", 
           stats->branch_count, 
           (float)stats->branch_count / stats->total_instructions * 100);
    printf("- Load/Store: %u (%.1f%%)\n", 
           stats->load_store_count, 
           (float)stats->load_store_count / stats->total_instructions * 100);
    printf("- Arithmetic/Logical: %u (%.1f%%)\n", 
           stats->arithmetic_count, 
           (float)stats->arithmetic_count / stats->total_instructions * 100);
}

// Find system call locations
void find_system_calls(bios_file_t* bios) {
    printf("System Call Analysis:\n");
    printf("--------------------\n");
    
    int syscall_count = 0;
    
    for (size_t i = 0; i < bios->size; i += 4) {
        // Read instruction (little-endian)
        uint32_t instruction = 
            (bios->data[i+3] << 24) |
            (bios->data[i+2] << 16) |
            (bios->data[i+1] << 8) |
            (bios->data[i+0]);
        
        // Check for SYSCALL (opcode 0x00, funct 0x0C)
        uint8_t opcode = (instruction >> 26) & 0x3F;
        uint8_t funct = instruction & 0x3F;
        
        if (opcode == 0x00 && funct == 0x0C) {
            uint32_t address = bios->load_address + i;
            printf("- SYSCALL at 0x%08X (instruction 0x%08X)\n", address, instruction);
            syscall_count++;
        }
    }
    
    printf("Total SYSCALL instructions found: %d\n", syscall_count);
}

// Find exception vector locations
void find_exception_vectors(bios_file_t* bios) {
    printf("Exception Vector Analysis:\n");
    printf("-------------------------\n");
    
    // Common exception vector addresses
    uint32_t vectors[] = {
        0x80000000, // General exception vector
        0x80000040, // SYSCALL vector
        0x80000048, // BREAK vector
        0x80000080, // Interrupt vector
        0x80000100, // TLB miss vector
        0x80000180  // XTLB miss vector
    };
    
    for (int i = 0; i < 6; i++) {
        uint32_t vector_addr = vectors[i];
        uint32_t bios_offset = vector_addr - bios->load_address;
        
        if (bios_offset < bios->size) {
            // Read instruction at vector
            uint32_t instruction = 
                (bios->data[bios_offset+3] << 24) |
                (bios->data[bios_offset+2] << 16) |
                (bios->data[bios_offset+1] << 8) |
                (bios->data[bios_offset+0]);
            
            printf("- Vector 0x%08X: instruction 0x%08X\n", vector_addr, instruction);
        }
    }
} 