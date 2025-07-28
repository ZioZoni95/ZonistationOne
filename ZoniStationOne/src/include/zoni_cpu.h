/**
 * @file zoni_cpu.h
 * @brief MIPS R3000A CPU emulation for ZoniStationOne
 * 
 * This file defines the CPU structure and emulation interface
 * for the PlayStation's MIPS R3000A processor.
 */

#ifndef ZONI_CPU_H
#define ZONI_CPU_H

#include "zoni_common.h"
#include "zoni_memory.h"

// MIPS R3000A CPU registers (following PCSX-ReARMed structure)
typedef union {
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    struct { u8 h3, h2, h, l; } b;
    struct { s8 h3, h2, h, l; } sb;
    struct { u16 h, l; } w;
    struct { s16 h, l; } sw;
#else
    struct { u8 l, h, h2, h3; } b;
    struct { s8 l, h, h2, h3; } sb;
    struct { u16 l, h; } w;
    struct { s16 l, h; } sw;
#endif
} zoni_pair_t;

typedef union {
    struct {
        u32   r0, at, v0, v1, a0, a1, a2, a3,
              t0, t1, t2, t3, t4, t5, t6, t7,
              s0, s1, s2, s3, s4, s5, s6, s7,
              t8, t9, k0, k1, gp, sp, fp, ra, lo, hi;
    } n;
    u32 r[34]; /* Lo, Hi in r[32] and r[33] */
    zoni_pair_t p[34];
} zoni_gpr_regs_t;

typedef union {
    struct {
        u32 Reserved0, Reserved1, Reserved2,  BPC,
            Reserved4, Reserved5, Reserved6,  DCIC,
            BadVAddr,  BDAM,      Reserved10, BPCM,
            SR,        Cause,     EPC,        PRid,
            Reserved16[16];
    } n;
    u32 r[32];
    zoni_pair_t p[32];
} zoni_cp0_regs_t;

// CPU exception types
typedef enum {
    ZONI_EXCEPTION_INT = 0,      // Interrupt
    ZONI_EXCEPTION_ADEL = 4,     // Address error (on load/I-fetch)
    ZONI_EXCEPTION_ADES = 5,     // Address error (on store)
    ZONI_EXCEPTION_IBE = 6,      // Bus error (instruction fetch)
    ZONI_EXCEPTION_DBE = 7,      // Bus error (data load/store)
    ZONI_EXCEPTION_SYSCALL = 8,  // syscall instruction
    ZONI_EXCEPTION_BP = 9,       // Breakpoint - a break instruction
    ZONI_EXCEPTION_RI = 10,      // reserved instruction
    ZONI_EXCEPTION_CPU = 11,     // Co-Processor unusable
    ZONI_EXCEPTION_OV = 12       // arithmetic overflow
} zoni_exception_t;

// Branch delay types
typedef enum {
    ZONI_BRANCH_TAKEN = 3,
    ZONI_BRANCH_NOT_TAKEN = 2,
    ZONI_BRANCH_NONE_OR_EXCEPTION = 0,
} zoni_branch_delay_t;

// Main CPU registers structure
typedef struct {
    zoni_gpr_regs_t gpr;        // General Purpose Registers
    zoni_cp0_regs_t cp0;        // Coprocessor0 Registers
    
    u32 pc;                     // Program counter
    u32 code;                   // Current instruction
    u32 cycle;                  // Cycle counter
    u32 interrupt;              // Interrupt status
    
    // Timing and events
    struct { u32 sCycle, cycle; } intCycle[20];
    u32 event_cycles[20];
    u32 next_counter;
    u32 next_interrupt;
    
    // CPU state
    u32 gte_busy_cycle;
    u32 muldiv_busy_cycle;
    u32 sub_cycle;
    u32 sub_cycle_step;
    u32 biu_reg;
    
    u8  stop;
    u8  branch_seen;
    u8  branching;
    u8  dload_sel;              // Delay load state
    
    // Load delay slots
    u8  dload_reg[2];
    u32 dload_val[2];
    
    // Performance counters
    u32 instruction_count;
    u32 cycle_count;
    
    // Exception handling
    u32 exception_vector;
    u32 exception_cause;
    u32 exception_epc;
} zoni_cpu_regs_t;

// CPU execution modes
typedef enum {
    ZONI_CPU_MODE_INTERPRETER = 0,
    ZONI_CPU_MODE_DYNAREC = 1
} zoni_cpu_mode_t;

// CPU configuration
typedef struct {
    zoni_cpu_mode_t mode;
    bool enable_icache;
    bool enable_dcache;
    bool precise_exceptions;
    u32 cycle_multiplier;  // 100 = 1.0x speed
} zoni_cpu_config_t;

// CPU functions
zoni_error_t zoni_cpu_init(zoni_cpu_regs_t* cpu, const zoni_cpu_config_t* config);
void zoni_cpu_reset(zoni_cpu_regs_t* cpu);
void zoni_cpu_shutdown(zoni_cpu_regs_t* cpu);

// CPU execution
zoni_error_t zoni_cpu_execute(zoni_cpu_regs_t* cpu, u32 cycles);
zoni_error_t zoni_cpu_step(zoni_cpu_regs_t* cpu);

// Memory access functions
zoni_error_t zoni_cpu_read8(zoni_cpu_regs_t* cpu, u32 address, u8* value);
zoni_error_t zoni_cpu_read16(zoni_cpu_regs_t* cpu, u32 address, u16* value);
zoni_error_t zoni_cpu_read32(zoni_cpu_regs_t* cpu, u32 address, u32* value);

zoni_error_t zoni_cpu_write8(zoni_cpu_regs_t* cpu, u32 address, u8 value);
zoni_error_t zoni_cpu_write16(zoni_cpu_regs_t* cpu, u32 address, u16 value);
zoni_error_t zoni_cpu_write32(zoni_cpu_regs_t* cpu, u32 address, u32 value);

// Register access
u32 zoni_cpu_get_register(zoni_cpu_regs_t* cpu, u8 reg);
void zoni_cpu_set_register(zoni_cpu_regs_t* cpu, u8 reg, u32 value);

// Exception handling
void zoni_cpu_trigger_exception(zoni_cpu_regs_t* cpu, zoni_exception_t cause, u32 epc);
void zoni_cpu_handle_interrupt(zoni_cpu_regs_t* cpu, u32 interrupt);

// Debug functions
void zoni_cpu_dump_registers(zoni_cpu_regs_t* cpu);
void zoni_cpu_disassemble_instruction(zoni_cpu_regs_t* cpu, u32 address, char* buffer, size_t buffer_size);

// Load delay functions
void zoni_cpu_do_load(zoni_cpu_regs_t* cpu, u32 r, u32 val);
void zoni_cpu_dload_rt(zoni_cpu_regs_t* cpu, u32 r, u32 val);
void zoni_cpu_dload_step(zoni_cpu_regs_t* cpu);
void zoni_cpu_dload_flush(zoni_cpu_regs_t* cpu);
void zoni_cpu_dload_clear(zoni_cpu_regs_t* cpu);

// MIPS instruction formats (R-type, I-type, J-type)
typedef struct {
    u32 opcode : 6;    // Opcode (bits 26-31)
    u32 rs : 5;        // Source register (bits 21-25)
    u32 rt : 5;        // Target register (bits 16-20)
    u32 rd : 5;        // Destination register (bits 11-15)
    u32 shamt : 5;     // Shift amount (bits 6-10)
    u32 funct : 6;     // Function code (bits 0-5)
} zoni_mips_r_type_t;

typedef struct {
    u32 opcode : 6;    // Opcode (bits 26-31)
    u32 rs : 5;        // Source register (bits 21-25)
    u32 rt : 5;        // Target register (bits 16-20)
    u32 immediate : 16; // Immediate value (bits 0-15)
} zoni_mips_i_type_t;

typedef struct {
    u32 opcode : 6;    // Opcode (bits 26-31)
    u32 address : 26;  // Jump address (bits 0-25)
} zoni_mips_j_type_t;

// Union for easy instruction access
typedef union {
    u32 raw;                    // Raw 32-bit instruction
    zoni_mips_r_type_t r;       // R-type format
    zoni_mips_i_type_t i;       // I-type format
    zoni_mips_j_type_t j;       // J-type format
} zoni_instruction_t;

// Helper function to convert little-endian instruction to big-endian for decoding
static inline u32 zoni_instruction_to_big_endian(u32 instruction) {
    return ((instruction & 0xFF) << 24) | ((instruction & 0xFF00) << 8) |
           ((instruction & 0xFF0000) >> 8) | ((instruction & 0xFF000000) >> 24);
}

// Memory reference (called from emulator)
void zoni_cpu_set_memory(zoni_memory_t* memory);

// Instruction fetching and execution
zoni_error_t zoni_cpu_fetch_instruction(zoni_cpu_regs_t* cpu, zoni_instruction_t* instruction);
zoni_error_t zoni_cpu_decode_instruction(zoni_instruction_t* instruction, char* disasm, size_t disasm_size);

// MIPS instruction opcodes
#define MIPS_OP_SPECIAL 0x00
#define MIPS_OP_REGIMM 0x01
#define MIPS_OP_J       0x02
#define MIPS_OP_JAL     0x03
#define MIPS_OP_BEQ     0x04
#define MIPS_OP_BNE     0x05
#define MIPS_OP_BLEZ    0x06
#define MIPS_OP_BGTZ    0x07
#define MIPS_OP_ADDI    0x08
#define MIPS_OP_ADDIU   0x09
#define MIPS_OP_SLTI    0x0A
#define MIPS_OP_SLTIU   0x0B
#define MIPS_OP_ANDI    0x0C
#define MIPS_OP_ORI     0x0D
#define MIPS_OP_XORI    0x0E
#define MIPS_OP_LUI     0x0F
#define MIPS_OP_COP0    0x10
#define MIPS_OP_COP1    0x11
#define MIPS_OP_COP2    0x12
#define MIPS_OP_COP3    0x13
#define MIPS_OP_LB      0x20
#define MIPS_OP_LH      0x21
#define MIPS_OP_LWL     0x22
#define MIPS_OP_LW      0x23
#define MIPS_OP_LBU     0x24
#define MIPS_OP_LHU     0x25
#define MIPS_OP_LWR     0x26
#define MIPS_OP_SB      0x28
#define MIPS_OP_SH      0x29
#define MIPS_OP_SWL     0x2A
#define MIPS_OP_SW      0x2B
#define MIPS_OP_SWR     0x2E
#define MIPS_OP_LWC0    0x30
#define MIPS_OP_LWC1    0x31
#define MIPS_OP_LWC2    0x32
#define MIPS_OP_LWC3    0x33
#define MIPS_OP_SWC0    0x38
#define MIPS_OP_SWC1    0x39
#define MIPS_OP_SWC2    0x3A
#define MIPS_OP_SWC3    0x3B

// MIPS function codes (for SPECIAL opcode)
#define MIPS_FUNC_SLL     0x00
#define MIPS_FUNC_SRL     0x02
#define MIPS_FUNC_SRA     0x03
#define MIPS_FUNC_SLLV    0x04
#define MIPS_FUNC_SRLV    0x06
#define MIPS_FUNC_SRAV    0x07
#define MIPS_FUNC_JR      0x08
#define MIPS_FUNC_JALR    0x09
#define MIPS_FUNC_SYSCALL 0x0C
#define MIPS_FUNC_BREAK   0x0D
#define MIPS_FUNC_MFHI    0x10
#define MIPS_FUNC_MTHI    0x11
#define MIPS_FUNC_MFLO    0x12
#define MIPS_FUNC_MTLO    0x13
#define MIPS_FUNC_MULT    0x18
#define MIPS_FUNC_MULTU   0x19
#define MIPS_FUNC_DIV     0x1A
#define MIPS_FUNC_DIVU    0x1B
#define MIPS_FUNC_ADD     0x20
#define MIPS_FUNC_ADDU    0x21
#define MIPS_FUNC_SUB     0x22
#define MIPS_FUNC_SUBU    0x23
#define MIPS_FUNC_AND     0x24
#define MIPS_FUNC_OR      0x25
#define MIPS_FUNC_XOR     0x26
#define MIPS_FUNC_NOR     0x27
#define MIPS_FUNC_SLT     0x2A
#define MIPS_FUNC_SLTU    0x2B

#endif // ZONI_CPU_H 