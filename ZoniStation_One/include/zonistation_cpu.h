#ifndef ZONISTATION_CPU_H
#define ZONISTATION_CPU_H

#include "zonistation_common.h"
#include "zonistation_memory.h"
#include "zonistation_config.h"

#ifdef __cplusplus
extern "C" {
#endif

// Forward declarations
typedef struct zs_memory_t zs_memory_t;

// ============================================================================
// EXCEPTION HANDLING (migrated from PCSX-ReARMed)
// ============================================================================

/**
 * R3000A CPU Exception Codes
 * These correspond to the MIPS R3000A exception types
 */
typedef enum {
    ZS_CPU_EXCEPTION_INT = 0,      // Interrupt exception
    ZS_CPU_EXCEPTION_ADEL = 4,     // Address error (on load/instruction fetch)
    ZS_CPU_EXCEPTION_ADES = 5,     // Address error (on store)
    ZS_CPU_EXCEPTION_IBE = 6,      // Bus error (instruction fetch)
    ZS_CPU_EXCEPTION_DBE = 7,      // Bus error (data load/store)
    ZS_CPU_EXCEPTION_SYSCALL = 8,  // syscall instruction
    ZS_CPU_EXCEPTION_BP = 9,       // Breakpoint - break instruction
    ZS_CPU_EXCEPTION_RI = 10,      // Reserved instruction
    ZS_CPU_EXCEPTION_CPU = 11,     // Coprocessor unusable
    ZS_CPU_EXCEPTION_OV = 12       // Arithmetic overflow
} zs_cpu_exception_t;

/**
 * CPU Notification Types
 * Used for communication between CPU components
 */
typedef enum {
    ZS_CPU_NOTIFY_CACHE_ISOLATED = 0,
    ZS_CPU_NOTIFY_CACHE_UNISOLATED = 1,
    ZS_CPU_NOTIFY_BEFORE_SAVE,     // data arg - HLE if non-null
    ZS_CPU_NOTIFY_AFTER_LOAD,
} zs_cpu_notify_t;

/**
 * Block Execution Caller Types
 * Identifies who is calling CPU execution
 */
typedef enum {
    ZS_CPU_EXEC_CALLER_BOOT = 0,
    ZS_CPU_EXEC_CALLER_HLE,
    ZS_CPU_EXEC_CALLER_OTHER,
} zs_cpu_exec_caller_t;

/**
 * Branch Delay Slot Types
 * Tracks branch delay slot execution state
 */
typedef enum {
    ZS_CPU_BRANCH_NONE_OR_EXCEPTION = 0,  // No branch or exception in delay slot
    ZS_CPU_BRANCH_NOT_TAKEN = 2,          // Branch not taken
    ZS_CPU_BRANCH_TAKEN = 3,              // Branch taken
} zs_cpu_branch_t;

// ============================================================================
// REGISTER STRUCTURES (migrated from PCSX-ReARMed)
// ============================================================================

/**
 * PAIR Union for Byte/Halfword Access
 * Allows accessing 32-bit registers as bytes or halfwords
 * Handles both big-endian and little-endian architectures
 */
typedef union {
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    struct { zs_u8 h3, h2, h, l; } b;     // Big-endian byte access
    struct { zs_s8 h3, h2, h, l; } sb;    // Big-endian signed byte access
    struct { zs_u16 h, l; } w;            // Big-endian halfword access
    struct { zs_s16 h, l; } sw;           // Big-endian signed halfword access
#else
    struct { zs_u8 l, h, h2, h3; } b;     // Little-endian byte access
    struct { zs_s8 l, h, h2, h3; } sb;    // Little-endian signed byte access
    struct { zs_u16 l, h; } w;            // Little-endian halfword access
    struct { zs_s16 l, h; } sw;           // Little-endian signed halfword access
#endif
} zs_cpu_pair_t;

/**
 * General Purpose Registers (GPR)
 * MIPS R3000A has 32 general-purpose registers plus HI/LO for multiply/divide
 */
typedef union {
    struct {
        // Standard MIPS register names
        zs_u32 r0, at, v0, v1, a0, a1, a2, a3,    // r0=zero, at=assembler temp, v0/v1=return values, a0-a3=arguments
                t0, t1, t2, t3, t4, t5, t6, t7,    // t0-t7=temporary registers
                s0, s1, s2, s3, s4, s5, s6, s7,    // s0-s7=saved registers
                t8, t9, k0, k1, gp, sp, fp, ra,    // t8/t9=more temps, k0/k1=kernel, gp=global pointer, sp=stack pointer, fp=frame pointer, ra=return address
                lo, hi;                             // lo/hi=multiply/divide result registers
    } n;                                           // Named access
    zs_u32 r[34];                                  // Array access (r[0] to r[33], where r[32]=lo, r[33]=hi)
    zs_cpu_pair_t p[34];                          // Byte/halfword access
} zs_cpu_gpr_regs_t;

/**
 * Coprocessor 0 (CP0) Registers
 * System control registers for exception handling, memory management, etc.
 */
typedef union {
    struct {
        zs_u32 Reserved0, Reserved1, Reserved2, BPC,    // Reserved, Breakpoint Counter
                Reserved4, BDA, Target, DCIC,           // Reserved, Breakpoint Data Address, Target, Debug Control
                BadVAddr, BDAM, Reserved10, BPCM,       // Bad Virtual Address, Breakpoint Data Address Mask, Reserved, Breakpoint Counter Mask
                SR, Cause, EPC, PRid,                   // Status Register, Cause Register, Exception Program Counter, Processor Revision ID
                Reserved16[16];                         // Reserved registers
    } n;                                                // Named access
    zs_u32 r[32];                                       // Array access
    zs_cpu_pair_t p[32];                               // Byte/halfword access
} zs_cpu_cp0_regs_t;

// ============================================================================
// GEOMETRY TRANSFORM ENGINE (GTE) STRUCTURES (migrated from PCSX-ReARMed)
// ============================================================================

/**
 * 2D Vector Structure
 * Used for screen coordinates in GTE operations
 */
typedef struct {
    zs_s16 x, y;                                       // X and Y coordinates
} zs_gte_vector2d_t;

/**
 * 2D Vector with Z Component
 * Used for 3D coordinates with padding
 */
typedef struct {
    zs_s16 z, pad;                                     // Z coordinate and padding
} zs_gte_vector2dz_t;

/**
 * 3D Vector Structure
 * Used for 3D coordinates in GTE operations
 */
typedef struct {
    zs_s16 x, y, z, pad;                               // X, Y, Z coordinates and padding
} zs_gte_vector3d_t;

/**
 * Long 3D Vector Structure
 * Used for 32-bit 3D coordinates
 */
typedef struct {
    zs_s32 x, y, z, pad;                               // 32-bit X, Y, Z coordinates and padding
} zs_gte_lvector3d_t;

/**
 * Color Structure (BGR format)
 * Used for color data in GTE operations
 */
typedef struct {
    zs_u8 r, g, b, c;                                  // Red, Green, Blue, and control byte
} zs_gte_color_t;

/**
 * 3D Matrix Structure
 * Used for transformation matrices in GTE operations
 */
typedef struct {
    zs_s16 m11, m12, m13, m21, m22, m23, m31, m32, m33, pad;  // 3x3 matrix elements and padding
} zs_gte_matrix3d_t;

/**
 * GTE Data Registers (CP2D)
 * Contains vector data, colors, and intermediate results
 */
typedef union {
    struct {
        zs_gte_vector3d_t v0, v1, v2;                 // Vector registers V0, V1, V2
        zs_gte_color_t rgb;                           // RGB color register
        zs_s32 otz;                                   // Average Z value
        zs_s32 ir0, ir1, ir2, ir3;                    // Interpolation registers
        zs_gte_vector2d_t sxy0, sxy1, sxy2, sxyp;     // Screen coordinates
        zs_gte_vector2dz_t sz0, sz1, sz2, sz3;        // Z coordinates
        zs_gte_color_t rgb0, rgb1, rgb2;              // RGB color registers
        zs_s32 reserved;                              // Reserved
        zs_s32 mac0, mac1, mac2, mac3;                // MAC (Multiply-Accumulate) registers
        zs_u32 irgb, orgb;                            // Interpolated RGB, Output RGB
        zs_s32 lzcs, lzcr;                            // Leading Zero Count Source, Result
    } n;                                              // Named access
    zs_u32 r[32];                                     // Array access
    zs_cpu_pair_t p[32];                              // Byte/halfword access
} zs_gte_data_regs_t;

/**
 * GTE Control Registers (CP2C)
 * Contains transformation matrices and control data
 */
typedef union {
    struct {
        zs_gte_matrix3d_t rMatrix;                    // Rotation matrix
        zs_s32 trX, trY, trZ;                         // Translation vector
        zs_gte_matrix3d_t lMatrix;                    // Light matrix
        zs_s32 rbk, gbk, bbk;                         // Background color
        zs_gte_matrix3d_t cMatrix;                    // Color matrix
        zs_s32 rfc, gfc, bfc;                         // Far color
        zs_s32 ofx, ofy;                              // Offset X, Y
        zs_s32 h;                                     // Projection distance
        zs_s32 dqa, dqb;                              // Depth queuing parameters
        zs_s32 zsf3, zsf4;                            // Z scale factors
        zs_s32 flag;                                  // GTE flag register
    } n;                                              // Named access
    zs_u32 r[32];                                     // Array access
    zs_cpu_pair_t p[32];                              // Byte/halfword access
} zs_gte_control_regs_t;

/**
 * Complete GTE Register Set
 * Combines data and control registers
 */
typedef struct {
    zs_gte_data_regs_t data;                          // GTE data registers
    zs_gte_control_regs_t control;                    // GTE control registers
} zs_gte_regs_t;

// ============================================================================
// MAIN CPU REGISTERS STRUCTURE (migrated from PCSX-ReARMed)
// ============================================================================

/**
 * Complete CPU Register Set
 * Contains all CPU registers including GPR, CP0, and GTE
 */
typedef struct {
    // Core registers
    zs_cpu_gpr_regs_t gpr;                            // General Purpose Registers (R0-R31, LO, HI)
    zs_cpu_cp0_regs_t cp0;                            // Coprocessor 0 (System Control) Registers
    
    // GTE registers (Coprocessor 2)
    union {
        struct {
            zs_gte_data_regs_t data;                  // GTE data registers
            zs_gte_control_regs_t control;             // GTE control registers
        };
        zs_gte_regs_t gte;                            // Complete GTE register set
    };
    
    // Execution state
    zs_u32 pc;                                        // Program Counter
    zs_u32 code;                                      // Current instruction code
    zs_u32 cycle;                                     // Current cycle count
    
    // Interrupt handling
    zs_u32 interrupt;                                 // Interrupt state
    struct { 
        zs_u32 sCycle, cycle; 
    } intCycle[20];                                   // Interrupt cycle tracking
    
    // Event timing
    zs_u32 event_cycles[20];                          // Event cycle tracking
    zs_u32 psxNextCounter;                            // Next counter value
    zs_u32 psxNextsCounter;                           // Next sub-counter value
    zs_u32 next_interrupt;                            // Next interrupt cycle
    
    // Performance and timing
    zs_u32 unused;                                    // Reserved
    zs_u32 gteBusyCycle;                              // GTE busy cycle count
    zs_u32 muldivBusyCycle;                           // Multiply/Divide busy cycle count
    zs_u32 subCycle;                                  // Sub-cycle count (interpreter)
    zs_u32 subCycleStep;                              // Sub-cycle step size
    
    // CPU state
    zs_u32 biuReg;                                    // Bus Interface Unit register
    zs_u8 stop;                                       // Stop flag
    zs_u8 branchSeen;                                 // Branch instruction seen (interpreter)
    zs_u8 branching;                                  // Branch state (R3000A_BRANCH_*)
    zs_u8 dloadSel;                                   // Delay load selector
    
    // Delay slot handling
    zs_u8 dloadReg[2];                                // Delay load registers
    zs_u8 unused2[2];                                 // Reserved
    zs_u32 dloadVal[2];                               // Delay load values
    
    // Advanced features
    zs_u32 biosBranchCheck;                           // BIOS branch checking
    zs_u32 cpuInRecursion;                            // CPU recursion depth
    zs_u32 gpuIdleAfter;                              // GPU idle after cycles
    zs_u32 unused3[2];                                // Reserved
} zs_cpu_registers_t;

// ============================================================================
// CPU STATE STRUCTURE
// ============================================================================

/**
 * CPU State Information
 * Tracks the current execution state of the CPU
 */
typedef struct {
    zs_bool initialized;                              // CPU is initialized
    zs_bool running;                                  // CPU is running
    zs_bool halted;                                   // CPU is halted
    zs_bool in_exception;                             // CPU is handling an exception
    zs_bool in_interrupt;                             // CPU is handling an interrupt
    zs_u32 current_cycles;                            // Current cycle count
    zs_u32 total_cycles;                              // Total cycles executed
    zs_u32 instruction_count;                         // Instructions executed
} zs_cpu_state_t;

// ============================================================================
// MAIN CPU STRUCTURE
// ============================================================================

/**
 * Main CPU Structure
 * Contains all CPU components and state
 */
typedef struct zs_cpu_t {
    // Registers and state
    zs_cpu_registers_t regs;                          // All CPU registers
    zs_cpu_state_t state;                             // CPU state information
    
    // Memory interface
    zs_memory_t* memory;                              // Memory interface
    
    // Configuration
    const zs_config_t* config;                        // Configuration
    
    // Execution mode
    zs_cpu_mode_t mode;                               // CPU execution mode (interpreter/dynarec)
    
    // Pipeline state
    zs_bool delay_slot;                               // Currently in delay slot
    zs_u32 delay_slot_pc;                             // PC for delay slot
    zs_bool branch_taken;                             // Branch was taken
    
    // Cache
    zs_u8 icache[4096];                               // 4KB instruction cache
    zs_u32 icache_tags[256];                          // Cache tags
    zs_bool icache_valid[256];                        // Cache validity flags
    
    // Performance counters
    zs_u64 instructions_executed;                     // Total instructions executed
    zs_u64 branches_taken;                            // Total branches taken
    zs_u64 cache_hits;                                // Cache hits
    zs_u64 cache_misses;                              // Cache misses
    
} zs_cpu_t;

// ============================================================================
// FUNCTION DECLARATIONS
// ============================================================================

// CPU initialization and shutdown
zs_error_t zs_cpu_init(zs_cpu_t** cpu, zs_memory_t* memory, const zs_config_t* config);
zs_error_t zs_cpu_shutdown(zs_cpu_t* cpu);
zs_error_t zs_cpu_reset(zs_cpu_t* cpu);

// CPU execution
zs_error_t zs_cpu_run_cycles(zs_cpu_t* cpu, zs_u32 cycles);
zs_error_t zs_cpu_step_instruction(zs_cpu_t* cpu);
zs_error_t zs_cpu_execute_instruction(zs_cpu_t* cpu);

// Register access
zs_u32 zs_cpu_read_register(zs_cpu_t* cpu, zs_u8 reg);
zs_error_t zs_cpu_write_register(zs_cpu_t* cpu, zs_u8 reg, zs_u32 value);
zs_error_t zs_cpu_get_registers(zs_cpu_t* cpu, zs_cpu_registers_t* regs);
zs_error_t zs_cpu_set_registers(zs_cpu_t* cpu, const zs_cpu_registers_t* regs);

// Memory access
zs_error_t zs_cpu_read_memory(zs_cpu_t* cpu, zs_u32 address, zs_u8* data, zs_size_t size);
zs_error_t zs_cpu_write_memory(zs_cpu_t* cpu, zs_u32 address, const zs_u8* data, zs_size_t size);
zs_u8 zs_cpu_read_byte(zs_cpu_t* cpu, zs_u32 address);
zs_u16 zs_cpu_read_halfword(zs_cpu_t* cpu, zs_u32 address);
zs_u32 zs_cpu_read_word(zs_cpu_t* cpu, zs_u32 address);
zs_error_t zs_cpu_write_byte(zs_cpu_t* cpu, zs_u32 address, zs_u8 value);
zs_error_t zs_cpu_write_halfword(zs_cpu_t* cpu, zs_u32 address, zs_u16 value);
zs_error_t zs_cpu_write_word(zs_cpu_t* cpu, zs_u32 address, zs_u32 value);

// Exception handling
zs_error_t zs_cpu_trigger_exception(zs_cpu_t* cpu, zs_u32 exception_code);
zs_error_t zs_cpu_handle_interrupt(zs_cpu_t* cpu, zs_u32 interrupt_level);
zs_error_t zs_cpu_return_from_exception(zs_cpu_t* cpu);

// CPU control
zs_error_t zs_cpu_start(zs_cpu_t* cpu);
zs_error_t zs_cpu_stop(zs_cpu_t* cpu);
zs_error_t zs_cpu_halt(zs_cpu_t* cpu);
zs_error_t zs_cpu_resume(zs_cpu_t* cpu);

// Status queries
zs_bool zs_cpu_is_initialized(const zs_cpu_t* cpu);
zs_bool zs_cpu_is_running(const zs_cpu_t* cpu);
zs_bool zs_cpu_is_halted(const zs_cpu_t* cpu);
zs_u32 zs_cpu_get_pc(const zs_cpu_t* cpu);
zs_u32 zs_cpu_get_cycles(const zs_cpu_t* cpu);
zs_u64 zs_cpu_get_instruction_count(const zs_cpu_t* cpu);

// Debugging
zs_error_t zs_cpu_set_breakpoint(zs_cpu_t* cpu, zs_u32 address);
zs_error_t zs_cpu_clear_breakpoint(zs_cpu_t* cpu, zs_u32 address);
zs_error_t zs_cpu_get_breakpoints(zs_cpu_t* cpu, zs_u32* addresses, zs_size_t* count);
zs_error_t zs_cpu_disassemble_instruction(zs_cpu_t* cpu, zs_u32 address, char* buffer, zs_size_t buffer_size);

// Performance counters
zs_u64 zs_cpu_get_instructions_executed(const zs_cpu_t* cpu);
zs_u64 zs_cpu_get_branches_taken(const zs_cpu_t* cpu);
zs_u64 zs_cpu_get_cache_hits(const zs_cpu_t* cpu);
zs_u64 zs_cpu_get_cache_misses(const zs_cpu_t* cpu);
zs_error_t zs_cpu_reset_performance_counters(zs_cpu_t* cpu);

// ============================================================================
// INSTRUCTION FORMAT STRUCTURES (migrated from PCSX-ReARMed)
// ============================================================================

/**
 * MIPS R-Format Instruction Structure
 * Used for register-to-register instructions
 */
typedef struct {
    zs_u32 opcode : 6;                                // Opcode (bits 31-26)
    zs_u32 rs : 5;                                    // Source register (bits 25-21)
    zs_u32 rt : 5;                                    // Target register (bits 20-16)
    zs_u32 rd : 5;                                    // Destination register (bits 15-11)
    zs_u32 shamt : 5;                                 // Shift amount (bits 10-6)
    zs_u32 funct : 6;                                 // Function code (bits 5-0)
} zs_mips_r_format_t;

/**
 * MIPS I-Format Instruction Structure
 * Used for immediate instructions
 */
typedef struct {
    zs_u32 opcode : 6;                                // Opcode (bits 31-26)
    zs_u32 rs : 5;                                    // Source register (bits 25-21)
    zs_u32 rt : 5;                                    // Target register (bits 20-16)
    zs_u32 immediate : 16;                            // Immediate value (bits 15-0)
} zs_mips_i_format_t;

/**
 * MIPS J-Format Instruction Structure
 * Used for jump instructions
 */
typedef struct {
    zs_u32 opcode : 6;                                // Opcode (bits 31-26)
    zs_u32 address : 26;                              // Jump address (bits 25-0)
} zs_mips_j_format_t;

// MIPS instruction opcodes
#define ZS_MIPS_OPCODE_SPECIAL    0x00
#define ZS_MIPS_OPCODE_REGIMM     0x01
#define ZS_MIPS_OPCODE_J          0x02
#define ZS_MIPS_OPCODE_JAL        0x03
#define ZS_MIPS_OPCODE_BEQ        0x04
#define ZS_MIPS_OPCODE_BNE        0x05
#define ZS_MIPS_OPCODE_BLEZ       0x06
#define ZS_MIPS_OPCODE_BGTZ       0x07
#define ZS_MIPS_OPCODE_ADDI       0x08
#define ZS_MIPS_OPCODE_ADDIU      0x09
#define ZS_MIPS_OPCODE_SLTI       0x0A
#define ZS_MIPS_OPCODE_SLTIU      0x0B
#define ZS_MIPS_OPCODE_ANDI       0x0C
#define ZS_MIPS_OPCODE_ORI        0x0D
#define ZS_MIPS_OPCODE_XORI       0x0E
#define ZS_MIPS_OPCODE_LUI        0x0F
#define ZS_MIPS_OPCODE_COP0       0x10
#define ZS_MIPS_OPCODE_COP1       0x11
#define ZS_MIPS_OPCODE_COP2       0x12
#define ZS_MIPS_OPCODE_COP3       0x13
#define ZS_MIPS_OPCODE_LB         0x20
#define ZS_MIPS_OPCODE_LH         0x21
#define ZS_MIPS_OPCODE_LWL        0x22
#define ZS_MIPS_OPCODE_LW         0x23
#define ZS_MIPS_OPCODE_LBU        0x24
#define ZS_MIPS_OPCODE_LHU        0x25
#define ZS_MIPS_OPCODE_LWR        0x26
#define ZS_MIPS_OPCODE_SB         0x28
#define ZS_MIPS_OPCODE_SH         0x29
#define ZS_MIPS_OPCODE_SWL        0x2A
#define ZS_MIPS_OPCODE_SW         0x2B
#define ZS_MIPS_OPCODE_SWR        0x2E
#define ZS_MIPS_OPCODE_LWC0       0x30
#define ZS_MIPS_OPCODE_LWC1       0x31
#define ZS_MIPS_OPCODE_LWC2       0x32
#define ZS_MIPS_OPCODE_LWC3       0x33
#define ZS_MIPS_OPCODE_SWC0       0x38
#define ZS_MIPS_OPCODE_SWC1       0x39
#define ZS_MIPS_OPCODE_SWC2       0x3A
#define ZS_MIPS_OPCODE_SWC3       0x3B

// MIPS function codes (for SPECIAL opcode)
#define ZS_MIPS_FUNC_SLL         0x00
#define ZS_MIPS_FUNC_SRL         0x02
#define ZS_MIPS_FUNC_SRA         0x03
#define ZS_MIPS_FUNC_SLLV        0x04
#define ZS_MIPS_FUNC_SRLV        0x06
#define ZS_MIPS_FUNC_SRAV        0x07
#define ZS_MIPS_FUNC_JR          0x08
#define ZS_MIPS_FUNC_JALR        0x09
#define ZS_MIPS_FUNC_SYSCALL     0x0C
#define ZS_MIPS_FUNC_BREAK       0x0D
#define ZS_MIPS_FUNC_MFHI        0x10
#define ZS_MIPS_FUNC_MTHI        0x11
#define ZS_MIPS_FUNC_MFLO        0x12
#define ZS_MIPS_FUNC_MTLO        0x13
#define ZS_MIPS_FUNC_MULT        0x18
#define ZS_MIPS_FUNC_MULTU       0x19
#define ZS_MIPS_FUNC_DIV         0x1A
#define ZS_MIPS_FUNC_DIVU        0x1B
#define ZS_MIPS_FUNC_ADD         0x20
#define ZS_MIPS_FUNC_ADDU        0x21
#define ZS_MIPS_FUNC_SUB         0x22
#define ZS_MIPS_FUNC_SUBU        0x23
#define ZS_MIPS_FUNC_AND         0x24
#define ZS_MIPS_FUNC_OR          0x25
#define ZS_MIPS_FUNC_XOR         0x26
#define ZS_MIPS_FUNC_NOR         0x27
#define ZS_MIPS_FUNC_SLT         0x2A
#define ZS_MIPS_FUNC_SLTU        0x2B

#ifdef __cplusplus
}
#endif

#endif // ZONISTATION_CPU_H 