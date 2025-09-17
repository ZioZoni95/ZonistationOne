#ifndef PSX_CPU_H
#define PSX_CPU_H

#include "psx_types.h"

// PSX-SPX: MIPS R3000A CPU Implementation
// Following guide.tex structure with PSX-SPX register specifications

// PSX-SPX: General Purpose Registers (32 registers, $0-$31)
#define PSX_REG_ZERO    0   // Always zero
#define PSX_REG_AT      1   // Assembler temporary
#define PSX_REG_V0      2   // Return values
#define PSX_REG_V1      3
#define PSX_REG_A0      4   // Function arguments
#define PSX_REG_A1      5
#define PSX_REG_A2      6
#define PSX_REG_A3      7
#define PSX_REG_T0      8   // Temporary registers
#define PSX_REG_T1      9
#define PSX_REG_T2      10
#define PSX_REG_T3      11
#define PSX_REG_T4      12
#define PSX_REG_T5      13
#define PSX_REG_T6      14
#define PSX_REG_T7      15
#define PSX_REG_S0      16  // Saved registers
#define PSX_REG_S1      17
#define PSX_REG_S2      18
#define PSX_REG_S3      19
#define PSX_REG_S4      20
#define PSX_REG_S5      21
#define PSX_REG_S6      22
#define PSX_REG_S7      23
#define PSX_REG_T8      24  // More temporaries
#define PSX_REG_T9      25
#define PSX_REG_K0      26  // Kernel reserved
#define PSX_REG_K1      27
#define PSX_REG_GP      28  // Global pointer
#define PSX_REG_SP      29  // Stack pointer
#define PSX_REG_FP      30  // Frame pointer
#define PSX_REG_RA      31  // Return address

// PSX-SPX: COP0 System Control Coprocessor Registers
#define COP0_STATUS     12  // Status register
#define COP0_CAUSE      13  // Cause register (interrupt/exception info)
#define COP0_EPC        14  // Exception Program Counter
#define COP0_PRID       15  // Processor ID
#define COP0_DCIC       7   // Debug and Cache Invalidate Control
#define COP0_JUMPDEST   6   // Jump destination (breakpoint)
#define COP0_BADVADDR   8   // Bad Virtual Address
#define COP0_BDAM       9   // Data Address Breakpoint Mask
#define COP0_BPCM       11  // Breakpoint Control Mask
#define COP0_BPC        3   // Breakpoint Control

// Guide.tex: CPU state structure
typedef struct {
    // PSX-SPX: MIPS R3000A general purpose registers
    u32 gpr[32];        // General Purpose Registers ($0-$31)
    u32 pc;             // Program Counter
    u32 hi, lo;         // Multiplication/Division result registers
    
    // PSX-SPX: COP0 System Control Coprocessor registers
    u32 cop0_regs[64];  // System control registers
    
    // Guide.tex: Load delay slot emulation
    u32 load_reg;       // Register number that will be loaded
    u32 load_value;     // Value to load next cycle
    bool load_pending;  // Is there a pending load?
    
    // Guide.tex: Branch delay slot handling
    bool branch_delay;  // Are we in a branch delay slot?
    u32 next_pc;        // PC after branch delay slot
    
    // Exception handling state
    bool exception_pending;
    u32 exception_cause;
    
} psx_cpu_t;

// Guide.tex: Basic CPU interface functions
void cpu_init(psx_cpu_t* cpu);
void cpu_reset(psx_cpu_t* cpu);
void cpu_step(psx_cpu_t* cpu);

// Register access functions
u32 cpu_get_gpr(psx_cpu_t* cpu, u32 reg);
void cpu_set_gpr(psx_cpu_t* cpu, u32 reg, u32 value);
u32 cpu_get_cop0(psx_cpu_t* cpu, u32 reg);
void cpu_set_cop0(psx_cpu_t* cpu, u32 reg, u32 value);

// Exception handling
void cpu_exception(psx_cpu_t* cpu, u32 cause);

// Instruction execution
void cpu_execute_instruction(psx_cpu_t* cpu, u32 instruction);

// Guide.tex: Instruction decoding helpers
u32 instruction_opcode(u32 instruction);
u32 instruction_rs(u32 instruction);
u32 instruction_rt(u32 instruction);
u32 instruction_rd(u32 instruction);
u32 instruction_shamt(u32 instruction);
u32 instruction_funct(u32 instruction);
u32 instruction_imm(u32 instruction);
s32 instruction_imm_signed(u32 instruction);
u32 instruction_target(u32 instruction);

#endif // PSX_CPU_H