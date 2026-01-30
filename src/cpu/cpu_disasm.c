#include "cpu.h"
#include <stdio.h>
#include <string.h>

/**
 * @brief Simple MIPS instruction disassembler for debugging
 * @param instruction The 32-bit MIPS instruction to disassemble
 * @param pc The program counter address
 * @return Pointer to static string containing disassembly
 */
const char* disassemble_mips(uint32_t instruction, uint32_t pc) {
    static char disasm_buffer[256];
    
    uint32_t opcode = (instruction >> 26) & 0x3F;
    uint32_t rs = (instruction >> 21) & 0x1F;
    uint32_t rt = (instruction >> 16) & 0x1F;
    uint32_t rd = (instruction >> 11) & 0x1F;
    uint32_t shamt = (instruction >> 6) & 0x1F;
    uint32_t funct = instruction & 0x3F;
    uint32_t immediate = instruction & 0xFFFF;
    uint32_t address = instruction & 0x3FFFFFF;
    
    // Sign extend immediate
    int32_t simmediate = (int32_t)(int16_t)immediate;
    
    switch (opcode) {
        case 0x00: // R-type instructions
            switch (funct) {
                case 0x00: snprintf(disasm_buffer, sizeof(disasm_buffer), "SLL $%d, $%d, %d", rd, rt, shamt); break;
                case 0x02: snprintf(disasm_buffer, sizeof(disasm_buffer), "SRL $%d, $%d, %d", rd, rt, shamt); break;
                case 0x03: snprintf(disasm_buffer, sizeof(disasm_buffer), "SRA $%d, $%d, %d", rd, rt, shamt); break;
                case 0x04: snprintf(disasm_buffer, sizeof(disasm_buffer), "SLLV $%d, $%d, $%d", rd, rt, rs); break;
                case 0x06: snprintf(disasm_buffer, sizeof(disasm_buffer), "SRLV $%d, $%d, $%d", rd, rt, rs); break;
                case 0x07: snprintf(disasm_buffer, sizeof(disasm_buffer), "SRAV $%d, $%d, $%d", rd, rt, rs); break;
                case 0x08: snprintf(disasm_buffer, sizeof(disasm_buffer), "JR $%d", rs); break;
                case 0x09: snprintf(disasm_buffer, sizeof(disasm_buffer), "JALR $%d, $%d", rd, rs); break;
                case 0x0C: snprintf(disasm_buffer, sizeof(disasm_buffer), "SYSCALL 0x%05x", (instruction >> 6) & 0xFFFFF); break;
                case 0x0D: snprintf(disasm_buffer, sizeof(disasm_buffer), "BREAK 0x%05x", (instruction >> 6) & 0xFFFFF); break;
                case 0x10: snprintf(disasm_buffer, sizeof(disasm_buffer), "MFHI $%d", rd); break;
                case 0x11: snprintf(disasm_buffer, sizeof(disasm_buffer), "MTHI $%d", rs); break;
                case 0x12: snprintf(disasm_buffer, sizeof(disasm_buffer), "MFLO $%d", rd); break;
                case 0x13: snprintf(disasm_buffer, sizeof(disasm_buffer), "MTLO $%d", rs); break;
                case 0x18: snprintf(disasm_buffer, sizeof(disasm_buffer), "MULT $%d, $%d", rs, rt); break;
                case 0x19: snprintf(disasm_buffer, sizeof(disasm_buffer), "MULTU $%d, $%d", rs, rt); break;
                case 0x1A: snprintf(disasm_buffer, sizeof(disasm_buffer), "DIV $%d, $%d", rs, rt); break;
                case 0x1B: snprintf(disasm_buffer, sizeof(disasm_buffer), "DIVU $%d, $%d", rs, rt); break;
                case 0x20: snprintf(disasm_buffer, sizeof(disasm_buffer), "ADD $%d, $%d, $%d", rd, rs, rt); break;
                case 0x21: snprintf(disasm_buffer, sizeof(disasm_buffer), "ADDU $%d, $%d, $%d", rd, rs, rt); break;
                case 0x22: snprintf(disasm_buffer, sizeof(disasm_buffer), "SUB $%d, $%d, $%d", rd, rs, rt); break;
                case 0x23: snprintf(disasm_buffer, sizeof(disasm_buffer), "SUBU $%d, $%d, $%d", rd, rs, rt); break;
                case 0x24: snprintf(disasm_buffer, sizeof(disasm_buffer), "AND $%d, $%d, $%d", rd, rs, rt); break;
                case 0x25: snprintf(disasm_buffer, sizeof(disasm_buffer), "OR $%d, $%d, $%d", rd, rs, rt); break;
                case 0x26: snprintf(disasm_buffer, sizeof(disasm_buffer), "XOR $%d, $%d, $%d", rd, rs, rt); break;
                case 0x27: snprintf(disasm_buffer, sizeof(disasm_buffer), "NOR $%d, $%d, $%d", rd, rs, rt); break;
                case 0x2A: snprintf(disasm_buffer, sizeof(disasm_buffer), "SLT $%d, $%d, $%d", rd, rs, rt); break;
                case 0x2B: snprintf(disasm_buffer, sizeof(disasm_buffer), "SLTU $%d, $%d, $%d", rd, rs, rt); break;
                default: snprintf(disasm_buffer, sizeof(disasm_buffer), "R-type: op=0x%02x, rs=$%d, rt=$%d, rd=$%d, shamt=%d, funct=0x%02x", opcode, rs, rt, rd, shamt, funct); break;
            }
            break;
            
        case 0x01: // REGIMM branches
            {
                int subcode = (instruction >> 16) & 0x1F;
                switch (subcode) {
                    case 0x00: snprintf(disasm_buffer, sizeof(disasm_buffer), "BLTZ $%d, 0x%08x", rs, pc + 4 + (simmediate << 2)); break;
                    case 0x01: snprintf(disasm_buffer, sizeof(disasm_buffer), "BGEZ $%d, 0x%08x", rs, pc + 4 + (simmediate << 2)); break;
                    case 0x10: snprintf(disasm_buffer, sizeof(disasm_buffer), "BLTZAL $%d, 0x%08x", rs, pc + 4 + (simmediate << 2)); break;
                    case 0x11: snprintf(disasm_buffer, sizeof(disasm_buffer), "BGEZAL $%d, 0x%08x", rs, pc + 4 + (simmediate << 2)); break;
                    default: snprintf(disasm_buffer, sizeof(disasm_buffer), "REGIMM: op=0x%02x, rs=$%d, subcode=0x%02x, imm=0x%04x", opcode, rs, subcode, immediate); break;
                }
            }
            break;
            
        case 0x02: // J
            snprintf(disasm_buffer, sizeof(disasm_buffer), "J 0x%08x", (pc & 0xF0000000) | (address << 2)); break;
        case 0x03: // JAL
            snprintf(disasm_buffer, sizeof(disasm_buffer), "JAL 0x%08x", (pc & 0xF0000000) | (address << 2)); break;
            
        case 0x04: // BEQ
            snprintf(disasm_buffer, sizeof(disasm_buffer), "BEQ $%d, $%d, 0x%08x", rs, rt, pc + 4 + (simmediate << 2)); break;
        case 0x05: // BNE
            snprintf(disasm_buffer, sizeof(disasm_buffer), "BNE $%d, $%d, 0x%08x", rs, rt, pc + 4 + (simmediate << 2)); break;
        case 0x06: // BLEZ
            snprintf(disasm_buffer, sizeof(disasm_buffer), "BLEZ $%d, 0x%08x", rs, pc + 4 + (simmediate << 2)); break;
        case 0x07: // BGTZ
            snprintf(disasm_buffer, sizeof(disasm_buffer), "BGTZ $%d, 0x%08x", rs, pc + 4 + (simmediate << 2)); break;
            
        case 0x08: // ADDI
            snprintf(disasm_buffer, sizeof(disasm_buffer), "ADDI $%d, $%d, %d", rt, rs, simmediate); break;
        case 0x09: // ADDIU
            snprintf(disasm_buffer, sizeof(disasm_buffer), "ADDIU $%d, $%d, %d", rt, rs, simmediate); break;
        case 0x0A: // SLTI
            snprintf(disasm_buffer, sizeof(disasm_buffer), "SLTI $%d, $%d, %d", rt, rs, simmediate); break;
        case 0x0B: // SLTIU
            snprintf(disasm_buffer, sizeof(disasm_buffer), "SLTIU $%d, $%d, %d", rt, rs, simmediate); break;
        case 0x0C: // ANDI
            snprintf(disasm_buffer, sizeof(disasm_buffer), "ANDI $%d, $%d, 0x%04x", rt, rs, immediate); break;
        case 0x0D: // ORI
            snprintf(disasm_buffer, sizeof(disasm_buffer), "ORI $%d, $%d, 0x%04x", rt, rs, immediate); break;
        case 0x0E: // XORI
            snprintf(disasm_buffer, sizeof(disasm_buffer), "XORI $%d, $%d, 0x%04x", rt, rs, immediate); break;
        case 0x0F: // LUI
            snprintf(disasm_buffer, sizeof(disasm_buffer), "LUI $%d, 0x%04x", rt, immediate); break;
            
        case 0x10: // COP0
            {
                uint32_t cop_op = (instruction >> 21) & 0x1F;
                switch (cop_op) {
                    case 0x00: snprintf(disasm_buffer, sizeof(disasm_buffer), "MFC0 $%d, $%d", rt, rd); break;
                    case 0x04: snprintf(disasm_buffer, sizeof(disasm_buffer), "MTC0 $%d, $%d", rt, rd); break;
                    case 0x10: 
                        if ((instruction & 0x3F) == 0x10) {
                            snprintf(disasm_buffer, sizeof(disasm_buffer), "RFE"); break;
                        } else {
                            snprintf(disasm_buffer, sizeof(disasm_buffer), "COP0 0x%08x", instruction); break;
                        }
                    default: snprintf(disasm_buffer, sizeof(disasm_buffer), "COP0 0x%08x", instruction); break;
                }
            }
            break;
            
        case 0x11: // COP1
            snprintf(disasm_buffer, sizeof(disasm_buffer), "COP1 0x%08x", instruction); break;
        case 0x12: // COP2
            snprintf(disasm_buffer, sizeof(disasm_buffer), "COP2 0x%08x", instruction); break;
        case 0x13: // COP3
            snprintf(disasm_buffer, sizeof(disasm_buffer), "COP3 0x%08x", instruction); break;
            
        case 0x20: // LB
            snprintf(disasm_buffer, sizeof(disasm_buffer), "LB $%d, %d($%d)", rt, simmediate, rs); break;
        case 0x21: // LH
            snprintf(disasm_buffer, sizeof(disasm_buffer), "LH $%d, %d($%d)", rt, simmediate, rs); break;
        case 0x22: // LWL
            snprintf(disasm_buffer, sizeof(disasm_buffer), "LWL $%d, %d($%d)", rt, simmediate, rs); break;
        case 0x23: // LW
            snprintf(disasm_buffer, sizeof(disasm_buffer), "LW $%d, %d($%d)", rt, simmediate, rs); break;
        case 0x24: // LBU
            snprintf(disasm_buffer, sizeof(disasm_buffer), "LBU $%d, %d($%d)", rt, simmediate, rs); break;
        case 0x25: // LHU
            snprintf(disasm_buffer, sizeof(disasm_buffer), "LHU $%d, %d($%d)", rt, simmediate, rs); break;
        case 0x26: // LWR
            snprintf(disasm_buffer, sizeof(disasm_buffer), "LWR $%d, %d($%d)", rt, simmediate, rs); break;
            
        case 0x28: // SB
            snprintf(disasm_buffer, sizeof(disasm_buffer), "SB $%d, %d($%d)", rt, simmediate, rs); break;
        case 0x29: // SH
            snprintf(disasm_buffer, sizeof(disasm_buffer), "SH $%d, %d($%d)", rt, simmediate, rs); break;
        case 0x2A: // SWL
            snprintf(disasm_buffer, sizeof(disasm_buffer), "SWL $%d, %d($%d)", rt, simmediate, rs); break;
        case 0x2B: // SW
            snprintf(disasm_buffer, sizeof(disasm_buffer), "SW $%d, %d($%d)", rt, simmediate, rs); break;
        case 0x2E: // SWR
            snprintf(disasm_buffer, sizeof(disasm_buffer), "SWR $%d, %d($%d)", rt, simmediate, rs); break;
            
        case 0x30: // LWC0
            snprintf(disasm_buffer, sizeof(disasm_buffer), "LWC0 $%d, %d($%d)", rt, simmediate, rs); break;
        case 0x31: // LWC1
            snprintf(disasm_buffer, sizeof(disasm_buffer), "LWC1 $%d, %d($%d)", rt, simmediate, rs); break;
        case 0x32: // LWC2
            snprintf(disasm_buffer, sizeof(disasm_buffer), "LWC2 $%d, %d($%d)", rt, simmediate, rs); break;
        case 0x33: // LWC3
            snprintf(disasm_buffer, sizeof(disasm_buffer), "LWC3 $%d, %d($%d)", rt, simmediate, rs); break;
        case 0x38: // SWC0
            snprintf(disasm_buffer, sizeof(disasm_buffer), "SWC0 $%d, %d($%d)", rt, simmediate, rs); break;
        case 0x39: // SWC1
            snprintf(disasm_buffer, sizeof(disasm_buffer), "SWC1 $%d, %d($%d)", rt, simmediate, rs); break;
        case 0x3A: // SWC2
            snprintf(disasm_buffer, sizeof(disasm_buffer), "SWC2 $%d, %d($%d)", rt, simmediate, rs); break;
        case 0x3B: // SWC3
            snprintf(disasm_buffer, sizeof(disasm_buffer), "SWC3 $%d, %d($%d)", rt, simmediate, rs); break;
            
        default:
            snprintf(disasm_buffer, sizeof(disasm_buffer), "Unknown: op=0x%02x, rs=$%d, rt=$%d, rd=$%d, imm=0x%04x", opcode, rs, rt, rd, immediate); break;
    }
    
    return disasm_buffer;
}