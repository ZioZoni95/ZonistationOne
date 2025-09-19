/*
 * ZonistationOne - PlayStation 1 Emulator
 * MIPS R3000A Instruction Definitions
 * Based on PCSX-Redux architecture
 */

#pragma once

#include <cstdint>

namespace ZonistationOne {

// MIPS R3000A instruction format extraction macros (following PCSX-Redux pattern)
#define _Op_(code)     ((code >> 26) & 0x3F)    // Opcode (bits 31-26)
#define _Funct_(code)  ((code) & 0x3F)          // Function (bits 5-0)
#define _Rs_(code)     ((code >> 21) & 0x1F)    // Source register (bits 25-21)
#define _Rt_(code)     ((code >> 16) & 0x1F)    // Target register (bits 20-16)
#define _Rd_(code)     ((code >> 11) & 0x1F)    // Destination register (bits 15-11)
#define _Sa_(code)     ((code >> 6) & 0x1F)     // Shift amount (bits 10-6)
#define _Im_(code)     ((uint16_t)code)         // Immediate value (bits 15-0)
#define _Target_(code) (code & 0x03ffffff)      // Jump target (bits 25-0)

// Sign-extended and zero-extended immediate values
#define _Imm_(code)    ((int16_t)code)          // Sign-extended immediate
#define _ImmU_(code)   (code & 0xffff)          // Zero-extended immediate
#define _ImmLU_(code)  (code << 16)             // Load upper immediate (for LUI)

// Address calculation helpers
#define _BranchTarget_(code, pc) ((int16_t)_Im_(code) * 4 + pc)
#define _JumpTarget_(code, pc)   ((_Target_(code) * 4) + (pc & 0xf0000000))

// MIPS R3000A Opcodes (Primary opcodes - bits 31-26)
enum class Opcode : uint32_t {
    SPECIAL = 0x00,  // Special function (use funct field)
    REGIMM  = 0x01,  // Branch and trap (use rt field)
    J       = 0x02,  // Jump
    JAL     = 0x03,  // Jump and link
    BEQ     = 0x04,  // Branch if equal
    BNE     = 0x05,  // Branch if not equal
    BLEZ    = 0x06,  // Branch if less than or equal to zero
    BGTZ    = 0x07,  // Branch if greater than zero
    ADDI    = 0x08,  // Add immediate
    ADDIU   = 0x09,  // Add immediate unsigned
    SLTI    = 0x0A,  // Set less than immediate
    SLTIU   = 0x0B,  // Set less than immediate unsigned
    ANDI    = 0x0C,  // AND immediate
    ORI     = 0x0D,  // OR immediate
    XORI    = 0x0E,  // XOR immediate
    LUI     = 0x0F,  // Load upper immediate
    COP0    = 0x10,  // Coprocessor 0
    COP1    = 0x11,  // Coprocessor 1 (not used on PS1)
    COP2    = 0x12,  // Coprocessor 2 (GTE)
    COP3    = 0x13,  // Coprocessor 3 (not used on PS1)
    // 0x14-0x1F reserved
    LB      = 0x20,  // Load byte
    LH      = 0x21,  // Load halfword
    LWL     = 0x22,  // Load word left
    LW      = 0x23,  // Load word
    LBU     = 0x24,  // Load byte unsigned
    LHU     = 0x25,  // Load halfword unsigned
    LWR     = 0x26,  // Load word right
    // 0x27 reserved
    SB      = 0x28,  // Store byte
    SH      = 0x29,  // Store halfword
    SWL     = 0x2A,  // Store word left
    SW      = 0x2B,  // Store word
    // 0x2C-0x2D reserved
    SWR     = 0x2E,  // Store word right
    // 0x2F reserved
    // 0x30-0x31 reserved
    LWC2    = 0x32,  // Load word coprocessor 2
    // 0x33-0x37 reserved
    SWC2    = 0x3A,  // Store word coprocessor 2
    // 0x3B-0x3F reserved
};

// SPECIAL function codes (when opcode = 0x00, use funct field bits 5-0)
enum class SpecialFunct : uint32_t {
    SLL     = 0x00,  // Shift left logical
    // 0x01 reserved
    SRL     = 0x02,  // Shift right logical
    SRA     = 0x03,  // Shift right arithmetic
    SLLV    = 0x04,  // Shift left logical variable
    // 0x05 reserved
    SRLV    = 0x06,  // Shift right logical variable
    SRAV    = 0x07,  // Shift right arithmetic variable
    JR      = 0x08,  // Jump register
    JALR    = 0x09,  // Jump and link register
    // 0x0A-0x0B reserved
    SYSCALL = 0x0C,  // System call
    BREAK   = 0x0D,  // Break
    // 0x0E-0x0F reserved
    MFHI    = 0x10,  // Move from HI
    MTHI    = 0x11,  // Move to HI
    MFLO    = 0x12,  // Move from LO
    MTLO    = 0x13,  // Move to LO
    // 0x14-0x17 reserved
    MULT    = 0x18,  // Multiply
    MULTU   = 0x19,  // Multiply unsigned
    DIV     = 0x1A,  // Divide
    DIVU    = 0x1B,  // Divide unsigned
    // 0x1C-0x1F reserved
    ADD     = 0x20,  // Add
    ADDU    = 0x21,  // Add unsigned
    SUB     = 0x22,  // Subtract
    SUBU    = 0x23,  // Subtract unsigned
    AND     = 0x24,  // AND
    OR      = 0x25,  // OR
    XOR     = 0x26,  // XOR
    NOR     = 0x27,  // NOR
    // 0x28-0x29 reserved
    SLT     = 0x2A,  // Set less than
    SLTU    = 0x2B,  // Set less than unsigned
    // 0x2C-0x3F reserved
};

// REGIMM rt field values (when opcode = 0x01, use rt field bits 20-16)
enum class RegimmRt : uint32_t {
    BLTZ    = 0x00,  // Branch if less than zero
    BGEZ    = 0x01,  // Branch if greater than or equal to zero
    // 0x02-0x0F reserved
    BLTZAL  = 0x10,  // Branch if less than zero and link
    BGEZAL  = 0x11,  // Branch if greater than or equal to zero and link
    // 0x12-0x1F reserved
};

// Instruction format types
enum class InstructionFormat {
    R_TYPE,  // Register format: op[6] rs[5] rt[5] rd[5] shamt[5] funct[6]
    I_TYPE,  // Immediate format: op[6] rs[5] rt[5] immediate[16]
    J_TYPE,  // Jump format: op[6] address[26]
};

// Instruction information structure
struct InstructionInfo {
    uint32_t code;
    Opcode opcode;
    InstructionFormat format;
    uint32_t rs, rt, rd, sa;
    int32_t imm;
    uint32_t immU;
    uint32_t target;
    SpecialFunct specialFunct;
    RegimmRt regimmRt;
    
    InstructionInfo(uint32_t instruction) : code(instruction) {
        opcode = static_cast<Opcode>(_Op_(code));
        rs = _Rs_(code);
        rt = _Rt_(code);
        rd = _Rd_(code);
        sa = _Sa_(code);
        imm = _Imm_(code);
        immU = _ImmU_(code);
        target = _Target_(code);
        specialFunct = static_cast<SpecialFunct>(_Funct_(code));
        regimmRt = static_cast<RegimmRt>(rt);
        
        // Determine format
        switch (opcode) {
            case Opcode::SPECIAL:
                format = InstructionFormat::R_TYPE;
                break;
            case Opcode::J:
            case Opcode::JAL:
                format = InstructionFormat::J_TYPE;
                break;
            default:
                format = InstructionFormat::I_TYPE;
                break;
        }
    }
};

} // namespace ZonistationOne