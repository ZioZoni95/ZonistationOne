#ifndef CPU_H  
#define CPU_H

#include "psx_types.h"

// CPU subsystem function declarations

// Initialize CPU to PlayStation 1 reset state
void cpu_init(mips_cpu_t* cpu);

// Reset CPU to initial state
void cpu_reset(mips_cpu_t* cpu);

// Execute one CPU instruction cycle
psx_result_t cpu_step(mips_cpu_t* cpu, psx_memory_t* memory);

// Instruction decoding and execution
typedef struct {
    u32 opcode;      // Raw instruction
    u32 rs, rt, rd;  // Register indices  
    u16 immediate;   // Immediate value
    u32 target;      // Jump target
    s16 offset;      // Branch offset
} mips_instruction_t;

// Decode instruction from 32-bit opcode
mips_instruction_t cpu_decode_instruction(u32 opcode);

// Execute decoded instruction
psx_result_t cpu_execute_instruction(mips_cpu_t* cpu, psx_memory_t* memory, 
                                    const mips_instruction_t* instr);

// Register access (handles $zero register special case)
u32 cpu_get_register(const mips_cpu_t* cpu, u32 reg_index);
void cpu_set_register(mips_cpu_t* cpu, u32 reg_index, u32 value);

// Branch and jump handling
void cpu_branch(mips_cpu_t* cpu, u32 target_address);
void cpu_handle_branch_delay_slot(mips_cpu_t* cpu);

// Exception handling
void cpu_trigger_exception(mips_cpu_t* cpu, u32 exception_code);

// Debug functions
void cpu_print_registers(const mips_cpu_t* cpu);
void cpu_print_instruction(u32 address, const mips_instruction_t* instr);

// Instruction type identification
typedef enum {
    INSTR_TYPE_R,     // Register type (add, sub, etc.)
    INSTR_TYPE_I,     // Immediate type (addi, lw, sw, etc.)
    INSTR_TYPE_J,     // Jump type (j, jal)
    INSTR_TYPE_UNKNOWN
} instruction_type_t;

instruction_type_t cpu_get_instruction_type(u32 opcode);

// MIPS instruction opcodes (primary opcodes)
#define OPCODE_SPECIAL  0x00
#define OPCODE_REGIMM   0x01
#define OPCODE_J        0x02
#define OPCODE_JAL      0x03
#define OPCODE_BEQ      0x04
#define OPCODE_BNE      0x05
#define OPCODE_BLEZ     0x06
#define OPCODE_BGTZ     0x07
#define OPCODE_ADDI     0x08
#define OPCODE_ADDIU    0x09
#define OPCODE_SLTI     0x0A
#define OPCODE_SLTIU    0x0B
#define OPCODE_ANDI     0x0C
#define OPCODE_ORI      0x0D
#define OPCODE_XORI     0x0E
#define OPCODE_LUI      0x0F
#define OPCODE_COP0     0x10
#define OPCODE_COP1     0x11
#define OPCODE_COP2     0x12
#define OPCODE_COP3     0x13
#define OPCODE_LB       0x20
#define OPCODE_LH       0x21
#define OPCODE_LWL      0x22
#define OPCODE_LW       0x23
#define OPCODE_LBU      0x24
#define OPCODE_LHU      0x25
#define OPCODE_LWR      0x26
#define OPCODE_SB       0x28
#define OPCODE_SH       0x29
#define OPCODE_SWL      0x2A
#define OPCODE_SW       0x2B
#define OPCODE_SWR      0x2E

// SPECIAL function codes (for OPCODE_SPECIAL)
#define FUNCT_SLL       0x00
#define FUNCT_SRL       0x02
#define FUNCT_SRA       0x03
#define FUNCT_SLLV      0x04
#define FUNCT_SRLV      0x06
#define FUNCT_SRAV      0x07
#define FUNCT_JR        0x08
#define FUNCT_JALR      0x09
#define FUNCT_MFHI      0x10
#define FUNCT_MTHI      0x11
#define FUNCT_MFLO      0x12
#define FUNCT_MTLO      0x13
#define FUNCT_MULT      0x18
#define FUNCT_MULTU     0x19
#define FUNCT_DIV       0x1A
#define FUNCT_DIVU      0x1B
#define FUNCT_ADD       0x20
#define FUNCT_ADDU      0x21
#define FUNCT_SUB       0x22
#define FUNCT_SUBU      0x23
#define FUNCT_AND       0x24
#define FUNCT_OR        0x25
#define FUNCT_XOR       0x26
#define FUNCT_NOR       0x27
#define FUNCT_SLT       0x2A
#define FUNCT_SLTU      0x2B

#endif // CPU_H