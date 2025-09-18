#ifndef PSX_TYPES_H
#define PSX_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// PlayStation 1 fundamental data types
typedef uint8_t  u8;
typedef uint16_t u16; 
typedef uint32_t u32;
typedef uint64_t u64;

typedef int8_t   s8;
typedef int16_t  s16;
typedef int32_t  s32;
typedef int64_t  s64;

// PlayStation 1 memory map constants (based on PCSX-Redux)
#define PSX_RAM_SIZE        (2 * 1024 * 1024)  // 2MB main RAM
#define PSX_BIOS_SIZE       (512 * 1024)       // 512KB BIOS
#define PSX_SCRATCHPAD_SIZE (1024)             // 1KB scratchpad RAM

// Memory addresses (physical)
#define PSX_RAM_BASE        0x00000000
#define PSX_RAM_END         0x001FFFFF
#define PSX_EXPANSION1_BASE 0x1F000000
#define PSX_SCRATCHPAD_BASE 0x1F800000
#define PSX_IO_BASE         0x1F801000
#define PSX_EXPANSION2_BASE 0x1F802000
#define PSX_BIOS_BASE       0x1FC00000
#define PSX_BIOS_END        0x1FC7FFFF

// Memory masks for address translation
#define PSX_RAM_MASK        0x1FFFFF
#define PSX_BIOS_MASK       0x7FFFF
#define PSX_SCRATCHPAD_MASK 0x3FF

// MIPS R3000A CPU constants
#define MIPS_REG_COUNT      32
#define MIPS_PC_RESET       0xBFC00000  // BIOS entry point
#define MIPS_SP_RESET       0x801FFF00  // Initial stack pointer

// CPU register names (for debugging)
typedef enum {
    REG_ZERO = 0,  // Always zero
    REG_AT,        // Assembler temporary  
    REG_V0, REG_V1,     // Function return values
    REG_A0, REG_A1, REG_A2, REG_A3,  // Function arguments
    REG_T0, REG_T1, REG_T2, REG_T3, REG_T4, REG_T5, REG_T6, REG_T7,  // Temporary
    REG_S0, REG_S1, REG_S2, REG_S3, REG_S4, REG_S5, REG_S6, REG_S7,  // Saved
    REG_T8, REG_T9,     // More temporary
    REG_K0, REG_K1,     // Kernel reserved
    REG_GP,             // Global pointer
    REG_SP,             // Stack pointer
    REG_FP,             // Frame pointer
    REG_RA              // Return address
} mips_register_t;

// CPU state structure
typedef struct {
    u32 gpr[MIPS_REG_COUNT];    // General purpose registers
    u32 pc;                     // Program counter
    u32 next_pc;                // Next PC (for branch delay slots)
    u32 hi, lo;                 // Multiply/divide result registers
    
    // Coprocessor 0 (system control) registers  
    u32 cop0_regs[32];
    
    // CPU state
    bool in_branch_delay_slot;
    bool exception_pending;
    
    // Cycle counting
    u64 cycle_count;
} mips_cpu_t;

// Memory subsystem structure
typedef struct {
    u8 ram[PSX_RAM_SIZE];
    u8 bios[PSX_BIOS_SIZE];
    u8 scratchpad[PSX_SCRATCHPAD_SIZE];
    bool bios_loaded;
} psx_memory_t;

// Main emulator state
typedef struct {
    mips_cpu_t cpu;
    psx_memory_t memory;
    bool running;
    bool debug_mode;
} psx_emulator_t;

// Function result types
typedef enum {
    PSX_OK = 0,
    PSX_ERROR_INVALID_ADDRESS,
    PSX_ERROR_BIOS_NOT_LOADED,
    PSX_ERROR_FILE_NOT_FOUND,
    PSX_ERROR_INVALID_INSTRUCTION,
    PSX_ERROR_OUT_OF_MEMORY
} psx_result_t;

#endif // PSX_TYPES_H