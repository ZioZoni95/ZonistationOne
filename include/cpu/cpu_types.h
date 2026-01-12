#ifndef CPU_TYPES_H
#define CPU_TYPES_H

#include <stdbool.h>
#include <stdint.h>

// ============================================================
// CPU Architecture Constants
// ============================================================

#define INSTRUCTION_SIZE 4

// ============================================================
// Memory Segments
// ============================================================

typedef enum {
    SEGMENT_KUSEG,  // User virtual memory
    SEGMENT_KSEG0,  // Kernel physical memory (cached)
    SEGMENT_KSEG1,  // Kernel physical memory (uncached)
    SEGMENT_KSEG2   // Kernel virtual memory
} MemorySegment;

// ============================================================
// General Purpose Registers
// ============================================================

typedef enum {
    REG_ZERO,
    REG_AT,
    REG_V0, REG_V1,
    REG_A0, REG_A1, REG_A2, REG_A3,
    REG_T0, REG_T1, REG_T2, REG_T3, REG_T4, REG_T5, REG_T6, REG_T7,
    REG_S0, REG_S1, REG_S2, REG_S3, REG_S4, REG_S5, REG_S6, REG_S7,
    REG_T8, REG_T9,
    REG_K0, REG_K1,
    REG_GP, REG_SP, REG_FP, REG_RA,
    REG_HI, REG_LO,
    REG_COUNT
} Register;

// ============================================================
// Instruction Opcodes
// ============================================================

typedef enum {
    OPCODE_SPECIAL = 0,
    OPCODE_B = 1,      // Branch instructions (BLTZ/BGEZ/BLTZAL/BGEZAL)
    OPCODE_J = 2,
    OPCODE_JAL = 3,
    OPCODE_BEQ = 4,
    OPCODE_BNE = 5,
    OPCODE_BLEZ = 6,
    OPCODE_BGTZ = 7,
    OPCODE_ADDI = 8,
    OPCODE_ADDIU = 9,
    OPCODE_SLTI = 10,
    OPCODE_SLTIU = 11,
    OPCODE_ANDI = 12,
    OPCODE_ORI = 13,
    OPCODE_XORI = 14,
    OPCODE_LUI = 15,
    OPCODE_COP0 = 16,
    OPCODE_COP1 = 17,
    OPCODE_COP2 = 18,
    OPCODE_COP3 = 19,
    OPCODE_LB = 32,
    OPCODE_LH = 33,
    OPCODE_LWL = 34,
    OPCODE_LW = 35,
    OPCODE_LBU = 36,
    OPCODE_LHU = 37,
    OPCODE_LWR = 38,
    OPCODE_SB = 40,
    OPCODE_SH = 41,
    OPCODE_SWL = 42,
    OPCODE_SW = 43,
    OPCODE_SWR = 46,
    OPCODE_LWC0 = 48,
    OPCODE_LWC1 = 49,
    OPCODE_LWC2 = 50,
    OPCODE_LWC3 = 51,
    OPCODE_SWC0 = 56,
    OPCODE_SWC1 = 57,
    OPCODE_SWC2 = 58,
    OPCODE_SWC3 = 59
} InstructionOpcode;

// ============================================================
// Special Instruction Functions (R-type)
// ============================================================

typedef enum {
    FUNCT_SLL = 0,
    FUNCT_SRL = 2,
    FUNCT_SRA = 3,
    FUNCT_SLLV = 4,
    FUNCT_SRLV = 6,
    FUNCT_SRAV = 7,
    FUNCT_JR = 8,
    FUNCT_JALR = 9,
    FUNCT_SYSCALL = 12,
    FUNCT_BREAK = 13,
    FUNCT_MFHI = 16,
    FUNCT_MTHI = 17,
    FUNCT_MFLO = 18,
    FUNCT_MTLO = 19,
    FUNCT_MULT = 24,
    FUNCT_MULTU = 25,
    FUNCT_DIV = 26,
    FUNCT_DIVU = 27,
    FUNCT_ADD = 32,
    FUNCT_ADDU = 33,
    FUNCT_SUB = 34,
    FUNCT_SUBU = 35,
    FUNCT_AND = 36,
    FUNCT_OR = 37,
    FUNCT_XOR = 38,
    FUNCT_NOR = 39,
    FUNCT_SLT = 42,
    FUNCT_SLTU = 43
} InstructionFunction;

// ============================================================
// Coprocessor Common Instructions
// ============================================================

typedef enum {
    COP_MFCN = 0,
    COP_CFCN = 2,
    COP_MTCN = 4,
    COP_CTCN = 6
} CoprocessorCommonInstruction;

// ============================================================
// COP0 Specific Instructions
// ============================================================

typedef enum {
    COP0_TLBR = 0x01,
    COP0_TLBWI = 0x02,
    COP0_TLBWR = 0x04,
    COP0_TLBP = 0x08,
    COP0_RFE = 0x10
} Cop0Instruction;

// ============================================================
// Instruction Union for Decoding
// ============================================================

typedef union {
    uint32_t bits;

    // Common fields
    struct {
        uint32_t : 26;  // unused
        InstructionOpcode op : 6;
    };

    // I-type instructions (immediate)
    struct {
        uint32_t rs : 5;
        uint32_t rt : 5;
        uint16_t imm;
    } i;

    // J-type instructions (jump)
    struct {
        uint32_t target : 26;
    } j;

    // R-type instructions (register)
    struct {
        uint32_t rs : 5;
        uint32_t rt : 5;
        uint32_t rd : 5;
        uint8_t shamt : 5;
        InstructionFunction funct : 6;
    } r;

    // Coprocessor instructions
    struct {
        uint8_t cop_n : 2;
        uint8_t : 6;  // padding to align
        uint16_t imm16;
        uint32_t : 24;  // imm25 placeholder (24 bits available)
    } cop;
} Instruction;

// Helper macros for instruction decoding
#define INSTR_OP(instr) ((instr).op)
#define INSTR_RS(instr) ((instr).i.rs)
#define INSTR_RT(instr) ((instr).i.rt)
#define INSTR_RD(instr) ((instr).r.rd)
#define INSTR_IMM(instr) ((instr).i.imm)
#define INSTR_TARGET(instr) ((instr).j.target)
#define INSTR_SHAMT(instr) ((instr).r.shamt)
#define INSTR_FUNCT(instr) ((instr).r.funct)

// ============================================================
// Instruction Analysis Functions
// ============================================================

/**
 * @brief Check if instruction is a NOP
 */
static inline bool instr_is_nop(uint32_t instruction) {
    return instruction == 0;
}

/**
 * @brief Check if instruction is a branch
 */
static inline bool instr_is_branch(uint32_t instruction) {
    InstructionOpcode op = (InstructionOpcode)(instruction >> 26);
    return (op >= OPCODE_B && op <= OPCODE_BGTZ) || op == OPCODE_BEQ || op == OPCODE_BNE;
}

/**
 * @brief Check if instruction is unconditional branch
 */
static inline bool instr_is_unconditional_branch(uint32_t instruction) {
    InstructionOpcode op = (InstructionOpcode)(instruction >> 26);
    return op == OPCODE_J || op == OPCODE_JAL;
}

/**
 * @brief Check if instruction is direct branch
 */
static inline bool instr_is_direct_branch(uint32_t instruction) {
    InstructionOpcode op = (InstructionOpcode)(instruction >> 26);
    return op == OPCODE_J || op == OPCODE_JAL;
}

/**
 * @brief Check if instruction is a call
 */
static inline bool instr_is_call(uint32_t instruction) {
    InstructionOpcode op = (InstructionOpcode)(instruction >> 26);
    InstructionFunction funct = (InstructionFunction)(instruction & 0x3F);
    return op == OPCODE_JAL || (op == OPCODE_SPECIAL && funct == FUNCT_JALR);
}

/**
 * @brief Check if instruction is a return
 */
static inline bool instr_is_return(uint32_t instruction) {
    InstructionOpcode op = (InstructionOpcode)(instruction >> 26);
    InstructionFunction funct = (InstructionFunction)(instruction & 0x3F);
    return op == OPCODE_SPECIAL && funct == FUNCT_JR && INSTR_RS(*(Instruction*)&instruction) == REG_RA;
}

/**
 * @brief Check if instruction is memory load
 */
static inline bool instr_is_load(uint32_t instruction) {
    InstructionOpcode op = (InstructionOpcode)(instruction >> 26);
    return (op >= OPCODE_LB && op <= OPCODE_LWR) || (op >= OPCODE_LWC0 && op <= OPCODE_LWC3);
}

/**
 * @brief Check if instruction is memory store
 */
static inline bool instr_is_store(uint32_t instruction) {
    InstructionOpcode op = (InstructionOpcode)(instruction >> 26);
    return (op >= OPCODE_SB && op <= OPCODE_SWR) || (op >= OPCODE_SWC0 && op <= OPCODE_SWC3);
}

/**
 * @brief Check if instruction has load delay
 */
static inline bool instr_has_load_delay(uint32_t instruction) {
    return instr_is_load(instruction);
}

// ============================================================
// Register File Structure
// ============================================================

typedef struct {
    union {
        uint32_t r[REG_COUNT + 1];  // +1 for load delay slot

        struct {
            uint32_t zero;
            uint32_t at;
            uint32_t v0, v1;
            uint32_t a0, a1, a2, a3;
            uint32_t t0, t1, t2, t3, t4, t5, t6, t7;
            uint32_t s0, s1, s2, s3, s4, s5, s6, s7;
            uint32_t t8, t9;
            uint32_t k0, k1;
            uint32_t gp, sp, fp, ra;
            uint32_t hi, lo;
        };
    };
} Registers;

// ============================================================
// COP0 Registers
// ============================================================

typedef enum {
    COP0_BPC = 3,      // Breakpoint on execute
    COP0_BDA = 5,      // Breakpoint on data access
    COP0_JUMPDEST = 6, // Jump destination
    COP0_DCIC = 7,     // Debug and cache invalidate control
    COP0_BADVADDR = 8, // Bad virtual address
    COP0_BDAM = 9,     // Data breakpoint mask
    COP0_BPCM = 11,    // Execute breakpoint mask
    COP0_SR = 12,      // Status register
    COP0_CAUSE = 13,   // Cause register
    COP0_EPC = 14,     // Exception program counter
    COP0_PRID = 15     // Processor ID
} Cop0Register;

// ============================================================
// Exception Types
// ============================================================

typedef enum {
    EXC_INT = 0x00,    // Interrupt
    EXC_MOD = 0x01,    // TLB modification
    EXC_TLBL = 0x02,   // TLB load
    EXC_TLBS = 0x03,   // TLB store
    EXC_ADEL = 0x04,   // Address error load/fetch
    EXC_ADES = 0x05,   // Address error store
    EXC_IBE = 0x06,    // Bus error instruction fetch
    EXC_DBE = 0x07,    // Bus error data load/store
    EXC_SYSCALL = 0x08, // System call
    EXC_BREAK = 0x09,   // Break instruction
    EXC_RI = 0x0A,      // Reserved instruction
    EXC_CPU = 0x0B,     // Coprocessor unusable
    EXC_OV = 0x0C       // Arithmetic overflow
} Exception;

// ============================================================
// COP0 Register Structure
// ============================================================

typedef struct {
    uint32_t bpc;       // Breakpoint on execute
    uint32_t bda;       // Breakpoint on data access
    uint32_t tar;       // Target address register
    uint32_t badvaddr;  // Bad virtual address
    uint32_t bdam;      // Data breakpoint mask
    uint32_t bpcm;      // Execute breakpoint mask
    uint32_t epc;       // Exception program counter
    uint32_t prid;      // Processor ID

    // Status Register (SR) - bitfield access
    union {
        uint32_t bits;
        struct {
            uint32_t iec : 1;   // Current interrupt enable
            uint32_t kuc : 1;   // Current kernel/user mode
            uint32_t iep : 1;   // Previous interrupt enable
            uint32_t kup : 1;   // Previous kernel/user mode
            uint32_t ieo : 1;   // Old interrupt enable
            uint32_t kuo : 1;   // Old kernel/user mode
            uint32_t : 2;       // unused
            uint32_t im : 8;    // Interrupt mask
            uint32_t isc : 1;   // Isolate cache
            uint32_t swc : 1;   // Swap caches
            uint32_t pz : 1;    // Cache parity zero
            uint32_t cm : 1;    // Cache miss
            uint32_t pe : 1;    // Parity error
            uint32_t ts : 1;    // TLB shutdown
            uint32_t bev : 1;   // Boot exception vectors
            uint32_t : 2;       // unused
            uint32_t re : 1;    // Reverse endianness
            uint32_t : 2;       // unused
            uint32_t cu0 : 1;   // COP0 enable
            uint32_t ce1 : 1;   // COP1 enable (reserved)
            uint32_t ce2 : 1;   // COP2 enable
            uint32_t ce3 : 1;   // COP3 enable (reserved)
        };
    } sr;

    // Cause Register
    union {
        uint32_t bits;
        struct {
            uint32_t : 2;       // unused
            Exception excode : 5; // Exception code
            uint32_t : 1;       // unused
            uint32_t ip : 8;    // Interrupt pending
            uint32_t : 12;      // unused
            uint32_t ce : 2;    // Coprocessor error
            uint32_t bt : 1;    // Branch taken (in delay slot)
            uint32_t bd : 1;    // Branch delay
        };
    } cause;

    // Debug Control Register
    union {
        uint32_t bits;
        struct {
            uint32_t any_break : 1;
            uint32_t bpc_break : 1;
            uint32_t bda_break : 1;
            uint32_t bda_read_break : 1;
            uint32_t bda_write_break : 1;
            uint32_t jump_break : 1;
            uint32_t : 6;       // unused
            uint32_t jump_redirect : 2;
            uint32_t : 10;      // unused
            uint32_t super_enable1 : 1;
            uint32_t exec_break_enable : 1;
            uint32_t data_break_enable : 1;
            uint32_t data_read_break : 1;
            uint32_t data_write_break : 1;
            uint32_t any_jump_break : 1;
            uint32_t master_jump_enable : 1;
            uint32_t master_break_enable : 1;
            uint32_t super_enable2 : 1;
        };
    } dcic;
} Cop0Registers;

// ============================================================
// Legacy Compatibility (Boot Stage and Exception Cause)
// ============================================================

typedef enum {
    BOOT_STAGE_POWER_ON = 0,
    BOOT_STAGE_BIOS_INIT,
    BOOT_STAGE_LOGO_ANIMATION,
    BOOT_STAGE_PATCH_CHECK,
    BOOT_STAGE_CDROM_CHECK,
    BOOT_STAGE_WAITING_INPUT,
    BOOT_STAGE_BIOS_MENU,
    BOOT_STAGE_GAME_BOOT,
    BOOT_STAGE_GAME_RUNNING
} BootStage;

typedef enum {
    EXCEPTION_INTERRUPT = EXC_INT,
    EXCEPTION_LOAD_ADDRESS_ERROR = EXC_ADEL,
    EXCEPTION_STORE_ADDRESS_ERROR = EXC_ADES,
    EXCEPTION_SYSCALL = EXC_SYSCALL,
    EXCEPTION_BREAK = EXC_BREAK,
    EXCEPTION_ILLEGAL_INSTRUCTION = EXC_RI,
    EXCEPTION_COPROCESSOR_ERROR = EXC_CPU,
    EXCEPTION_OVERFLOW = EXC_OV
} ExceptionCause;

// ============================================================
// Instruction Decoding Helpers (Legacy)
// ============================================================

static inline uint32_t instr_function(uint32_t i) { return i >> 26; }
static inline uint32_t instr_s(uint32_t i) { return (i >> 21) & 0x1F; }
static inline uint32_t instr_t(uint32_t i) { return (i >> 16) & 0x1F; }
static inline uint32_t instr_d(uint32_t i) { return (i >> 11) & 0x1F; }
static inline uint32_t instr_imm(uint32_t i) { return i & 0xFFFF; }
static inline uint32_t instr_imm_se(uint32_t i) { 
    return (uint32_t)(int32_t)(int16_t)(i & 0xFFFF); 
}
static inline uint32_t instr_shift(uint32_t i) { return (i >> 6) & 0x1F; }
static inline uint32_t instr_subfunction(uint32_t i) { return i & 0x3F; }
static inline uint32_t instr_imm_jump(uint32_t i) { return i & 0x03FFFFFF; }
static inline uint32_t instr_cop_opcode(uint32_t i) { return (i >> 21) & 0x1F; }

// ============================================================
// Disassembly and BIOS Function Names
// ============================================================

/**
 * @brief Disassembles a MIPS instruction for debugging
 * @param instruction The 32-bit instruction word
 * @param pc Program counter address
 * @return Static string containing disassembly
 */
const char* cpu_disassemble(uint32_t instruction, uint32_t pc);

// Compatibility alias for old code
#define disassemble_mips cpu_disassemble

/**
 * @brief Get the name of a BIOS A-function for logging
 * @param func_num Function number (0-255)
 * @return Function name string or "unknown"
 */
const char* get_bios_a_function_name(uint32_t func_num);

/**
 * @brief Get the name of a BIOS B-function for logging
 * @param func_num Function number (0-255)
 * @return Function name string or "unknown"
 */
const char* get_bios_b_function_name(uint32_t func_num);

/**
 * @brief Get the name of a BIOS C-function for logging
 * @param func_num Function number (0-255)
 * @return Function name string or "unknown"
 */
const char* get_bios_c_function_name(uint32_t func_num);

#endif // CPU_TYPES_H
