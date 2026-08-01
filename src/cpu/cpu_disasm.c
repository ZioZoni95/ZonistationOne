/* SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 ZioZoni95
 *
 * Part of ZoniStation One, a PlayStation 1 emulator.
 * See LICENSE for the full licence text and THIRD-PARTY.md for the
 * components of this project that have other authors.
 */
#include "cpu.h"
#include <stdio.h>
#include <string.h>

/**
 * @brief MIPS R3000A disassembler for debugging.
 *
 * Style matches PCSX-Redux's disr3000a.cc: lowercase mnemonics, ABI register
 * names, and the common pseudo-instructions real MIPS assemblers/disassemblers
 * recognize (nop, move, li, not, neg/negu, b/bal) instead of always showing
 * the literal encoding they're built from.
 */

static const char* const REG[32] = {
    "$0","at","v0","v1","a0","a1","a2","a3",
    "t0","t1","t2","t3","t4","t5","t6","t7",
    "s0","s1","s2","s3","s4","s5","s6","s7",
    "t8","t9","k0","k1","gp","sp","fp","ra"
};

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

    int32_t simmediate = (int32_t)(int16_t)immediate;

    switch (opcode) {
        case 0x00: // R-type instructions
            switch (funct) {
                case 0x00:
                    if (instruction == 0) { snprintf(disasm_buffer, sizeof(disasm_buffer), "nop"); break; }
                    snprintf(disasm_buffer, sizeof(disasm_buffer), "sll     %s, %s, %d", REG[rd], REG[rt], shamt); break;
                case 0x02: snprintf(disasm_buffer, sizeof(disasm_buffer), "srl     %s, %s, %d", REG[rd], REG[rt], shamt); break;
                case 0x03: snprintf(disasm_buffer, sizeof(disasm_buffer), "sra     %s, %s, %d", REG[rd], REG[rt], shamt); break;
                case 0x04: snprintf(disasm_buffer, sizeof(disasm_buffer), "sllv    %s, %s, %s", REG[rd], REG[rt], REG[rs]); break;
                case 0x06: snprintf(disasm_buffer, sizeof(disasm_buffer), "srlv    %s, %s, %s", REG[rd], REG[rt], REG[rs]); break;
                case 0x07: snprintf(disasm_buffer, sizeof(disasm_buffer), "srav    %s, %s, %s", REG[rd], REG[rt], REG[rs]); break;
                case 0x08: snprintf(disasm_buffer, sizeof(disasm_buffer), "jr      %s", REG[rs]); break;
                case 0x09:
                    if (rd == 31) snprintf(disasm_buffer, sizeof(disasm_buffer), "jalr    %s", REG[rs]);
                    else snprintf(disasm_buffer, sizeof(disasm_buffer), "jalr    %s, %s", REG[rd], REG[rs]);
                    break;
                case 0x0C: snprintf(disasm_buffer, sizeof(disasm_buffer), "syscall 0x%05x", (instruction >> 6) & 0xFFFFF); break;
                case 0x0D: snprintf(disasm_buffer, sizeof(disasm_buffer), "break   0x%05x", (instruction >> 6) & 0xFFFFF); break;
                case 0x10: snprintf(disasm_buffer, sizeof(disasm_buffer), "mfhi    %s", REG[rd]); break;
                case 0x11: snprintf(disasm_buffer, sizeof(disasm_buffer), "mthi    %s", REG[rs]); break;
                case 0x12: snprintf(disasm_buffer, sizeof(disasm_buffer), "mflo    %s", REG[rd]); break;
                case 0x13: snprintf(disasm_buffer, sizeof(disasm_buffer), "mtlo    %s", REG[rs]); break;
                case 0x18: snprintf(disasm_buffer, sizeof(disasm_buffer), "mult    %s, %s", REG[rs], REG[rt]); break;
                case 0x19: snprintf(disasm_buffer, sizeof(disasm_buffer), "multu   %s, %s", REG[rs], REG[rt]); break;
                case 0x1A: snprintf(disasm_buffer, sizeof(disasm_buffer), "div     %s, %s", REG[rs], REG[rt]); break;
                case 0x1B: snprintf(disasm_buffer, sizeof(disasm_buffer), "divu    %s, %s", REG[rs], REG[rt]); break;
                case 0x20:
                    if (rt == 0) snprintf(disasm_buffer, sizeof(disasm_buffer), "move    %s, %s", REG[rd], REG[rs]);
                    else snprintf(disasm_buffer, sizeof(disasm_buffer), "add     %s, %s, %s", REG[rd], REG[rs], REG[rt]);
                    break;
                case 0x21:
                    if (rt == 0) snprintf(disasm_buffer, sizeof(disasm_buffer), "move    %s, %s", REG[rd], REG[rs]);
                    else if (rs == 0) snprintf(disasm_buffer, sizeof(disasm_buffer), "move    %s, %s", REG[rd], REG[rt]);
                    else snprintf(disasm_buffer, sizeof(disasm_buffer), "addu    %s, %s, %s", REG[rd], REG[rs], REG[rt]);
                    break;
                case 0x22:
                    if (rs == 0) snprintf(disasm_buffer, sizeof(disasm_buffer), "neg     %s, %s", REG[rd], REG[rt]);
                    else snprintf(disasm_buffer, sizeof(disasm_buffer), "sub     %s, %s, %s", REG[rd], REG[rs], REG[rt]);
                    break;
                case 0x23:
                    if (rs == 0) snprintf(disasm_buffer, sizeof(disasm_buffer), "negu    %s, %s", REG[rd], REG[rt]);
                    else snprintf(disasm_buffer, sizeof(disasm_buffer), "subu    %s, %s, %s", REG[rd], REG[rs], REG[rt]);
                    break;
                case 0x24: snprintf(disasm_buffer, sizeof(disasm_buffer), "and     %s, %s, %s", REG[rd], REG[rs], REG[rt]); break;
                case 0x25:
                    if (rt == 0) snprintf(disasm_buffer, sizeof(disasm_buffer), "move    %s, %s", REG[rd], REG[rs]);
                    else if (rs == 0) snprintf(disasm_buffer, sizeof(disasm_buffer), "move    %s, %s", REG[rd], REG[rt]);
                    else snprintf(disasm_buffer, sizeof(disasm_buffer), "or      %s, %s, %s", REG[rd], REG[rs], REG[rt]);
                    break;
                case 0x26: snprintf(disasm_buffer, sizeof(disasm_buffer), "xor     %s, %s, %s", REG[rd], REG[rs], REG[rt]); break;
                case 0x27:
                    if (rt == 0) snprintf(disasm_buffer, sizeof(disasm_buffer), "not     %s, %s", REG[rd], REG[rs]);
                    else if (rs == 0) snprintf(disasm_buffer, sizeof(disasm_buffer), "not     %s, %s", REG[rd], REG[rt]);
                    else snprintf(disasm_buffer, sizeof(disasm_buffer), "nor     %s, %s, %s", REG[rd], REG[rs], REG[rt]);
                    break;
                case 0x2A: snprintf(disasm_buffer, sizeof(disasm_buffer), "slt     %s, %s, %s", REG[rd], REG[rs], REG[rt]); break;
                case 0x2B: snprintf(disasm_buffer, sizeof(disasm_buffer), "sltu    %s, %s, %s", REG[rd], REG[rs], REG[rt]); break;
                default: snprintf(disasm_buffer, sizeof(disasm_buffer), "?r-type op=0x%02x rs=%s rt=%s rd=%s sa=%d funct=0x%02x", opcode, REG[rs], REG[rt], REG[rd], shamt, funct); break;
            }
            break;

        case 0x01: // REGIMM branches
            {
                int subcode = (instruction >> 16) & 0x1F;
                switch (subcode) {
                    case 0x00: snprintf(disasm_buffer, sizeof(disasm_buffer), "bltz    %s, 0x%08x", REG[rs], pc + 4 + (simmediate << 2)); break;
                    case 0x01:
                        if (rs == 0) snprintf(disasm_buffer, sizeof(disasm_buffer), "b       0x%08x", pc + 4 + (simmediate << 2));
                        else snprintf(disasm_buffer, sizeof(disasm_buffer), "bgez    %s, 0x%08x", REG[rs], pc + 4 + (simmediate << 2));
                        break;
                    case 0x10: snprintf(disasm_buffer, sizeof(disasm_buffer), "bltzal  %s, 0x%08x", REG[rs], pc + 4 + (simmediate << 2)); break;
                    case 0x11:
                        if (rs == 0) snprintf(disasm_buffer, sizeof(disasm_buffer), "bal     0x%08x", pc + 4 + (simmediate << 2));
                        else snprintf(disasm_buffer, sizeof(disasm_buffer), "bgezal  %s, 0x%08x", REG[rs], pc + 4 + (simmediate << 2));
                        break;
                    default: snprintf(disasm_buffer, sizeof(disasm_buffer), "?regimm op=0x%02x rs=%s subcode=0x%02x imm=0x%04x", opcode, REG[rs], subcode, immediate); break;
                }
            }
            break;

        case 0x02: // J
            snprintf(disasm_buffer, sizeof(disasm_buffer), "j       0x%08x", (pc & 0xF0000000) | (address << 2)); break;
        case 0x03: // JAL
            snprintf(disasm_buffer, sizeof(disasm_buffer), "jal     0x%08x", (pc & 0xF0000000) | (address << 2)); break;

        case 0x04: // BEQ
            if (rs == 0 && rt == 0) snprintf(disasm_buffer, sizeof(disasm_buffer), "b       0x%08x", pc + 4 + (simmediate << 2));
            else snprintf(disasm_buffer, sizeof(disasm_buffer), "beq     %s, %s, 0x%08x", REG[rs], REG[rt], pc + 4 + (simmediate << 2));
            break;
        case 0x05: // BNE
            snprintf(disasm_buffer, sizeof(disasm_buffer), "bne     %s, %s, 0x%08x", REG[rs], REG[rt], pc + 4 + (simmediate << 2)); break;
        case 0x06: // BLEZ
            snprintf(disasm_buffer, sizeof(disasm_buffer), "blez    %s, 0x%08x", REG[rs], pc + 4 + (simmediate << 2)); break;
        case 0x07: // BGTZ
            snprintf(disasm_buffer, sizeof(disasm_buffer), "bgtz    %s, 0x%08x", REG[rs], pc + 4 + (simmediate << 2)); break;

        case 0x08: // ADDI
            snprintf(disasm_buffer, sizeof(disasm_buffer), "addi    %s, %s, %d", REG[rt], REG[rs], simmediate); break;
        case 0x09: // ADDIU
            if (rs == 0) snprintf(disasm_buffer, sizeof(disasm_buffer), "li      %s, %d", REG[rt], simmediate);
            else snprintf(disasm_buffer, sizeof(disasm_buffer), "addiu   %s, %s, %d", REG[rt], REG[rs], simmediate);
            break;
        case 0x0A: // SLTI
            snprintf(disasm_buffer, sizeof(disasm_buffer), "slti    %s, %s, %d", REG[rt], REG[rs], simmediate); break;
        case 0x0B: // SLTIU
            snprintf(disasm_buffer, sizeof(disasm_buffer), "sltiu   %s, %s, %d", REG[rt], REG[rs], simmediate); break;
        case 0x0C: // ANDI
            snprintf(disasm_buffer, sizeof(disasm_buffer), "andi    %s, %s, 0x%04x", REG[rt], REG[rs], immediate); break;
        case 0x0D: // ORI
            if (rs == 0) snprintf(disasm_buffer, sizeof(disasm_buffer), "li      %s, 0x%04x", REG[rt], immediate);
            else snprintf(disasm_buffer, sizeof(disasm_buffer), "ori     %s, %s, 0x%04x", REG[rt], REG[rs], immediate);
            break;
        case 0x0E: // XORI
            snprintf(disasm_buffer, sizeof(disasm_buffer), "xori    %s, %s, 0x%04x", REG[rt], REG[rs], immediate); break;
        case 0x0F: // LUI
            snprintf(disasm_buffer, sizeof(disasm_buffer), "lui     %s, 0x%04x", REG[rt], immediate); break;

        case 0x10: // COP0
            {
                uint32_t cop_op = (instruction >> 21) & 0x1F;
                switch (cop_op) {
                    case 0x00: snprintf(disasm_buffer, sizeof(disasm_buffer), "mfc0    %s, $%d", REG[rt], rd); break;
                    case 0x04: snprintf(disasm_buffer, sizeof(disasm_buffer), "mtc0    %s, $%d", REG[rt], rd); break;
                    case 0x10:
                        if ((instruction & 0x3F) == 0x10) {
                            snprintf(disasm_buffer, sizeof(disasm_buffer), "rfe"); break;
                        } else {
                            snprintf(disasm_buffer, sizeof(disasm_buffer), "cop0    0x%08x", instruction); break;
                        }
                    default: snprintf(disasm_buffer, sizeof(disasm_buffer), "cop0    0x%08x", instruction); break;
                }
            }
            break;

        case 0x11: // COP1
            snprintf(disasm_buffer, sizeof(disasm_buffer), "cop1    0x%08x", instruction); break;
        case 0x12: // COP2 (GTE)
            snprintf(disasm_buffer, sizeof(disasm_buffer), "cop2    0x%08x", instruction); break;
        case 0x13: // COP3
            snprintf(disasm_buffer, sizeof(disasm_buffer), "cop3    0x%08x", instruction); break;

        case 0x20: // LB
            snprintf(disasm_buffer, sizeof(disasm_buffer), "lb      %s, %d(%s)", REG[rt], simmediate, REG[rs]); break;
        case 0x21: // LH
            snprintf(disasm_buffer, sizeof(disasm_buffer), "lh      %s, %d(%s)", REG[rt], simmediate, REG[rs]); break;
        case 0x22: // LWL
            snprintf(disasm_buffer, sizeof(disasm_buffer), "lwl     %s, %d(%s)", REG[rt], simmediate, REG[rs]); break;
        case 0x23: // LW
            snprintf(disasm_buffer, sizeof(disasm_buffer), "lw      %s, %d(%s)", REG[rt], simmediate, REG[rs]); break;
        case 0x24: // LBU
            snprintf(disasm_buffer, sizeof(disasm_buffer), "lbu     %s, %d(%s)", REG[rt], simmediate, REG[rs]); break;
        case 0x25: // LHU
            snprintf(disasm_buffer, sizeof(disasm_buffer), "lhu     %s, %d(%s)", REG[rt], simmediate, REG[rs]); break;
        case 0x26: // LWR
            snprintf(disasm_buffer, sizeof(disasm_buffer), "lwr     %s, %d(%s)", REG[rt], simmediate, REG[rs]); break;

        case 0x28: // SB
            snprintf(disasm_buffer, sizeof(disasm_buffer), "sb      %s, %d(%s)", REG[rt], simmediate, REG[rs]); break;
        case 0x29: // SH
            snprintf(disasm_buffer, sizeof(disasm_buffer), "sh      %s, %d(%s)", REG[rt], simmediate, REG[rs]); break;
        case 0x2A: // SWL
            snprintf(disasm_buffer, sizeof(disasm_buffer), "swl     %s, %d(%s)", REG[rt], simmediate, REG[rs]); break;
        case 0x2B: // SW
            snprintf(disasm_buffer, sizeof(disasm_buffer), "sw      %s, %d(%s)", REG[rt], simmediate, REG[rs]); break;
        case 0x2E: // SWR
            snprintf(disasm_buffer, sizeof(disasm_buffer), "swr     %s, %d(%s)", REG[rt], simmediate, REG[rs]); break;

        case 0x30: // LWC0
            snprintf(disasm_buffer, sizeof(disasm_buffer), "lwc0    $%d, %d(%s)", rt, simmediate, REG[rs]); break;
        case 0x31: // LWC1
            snprintf(disasm_buffer, sizeof(disasm_buffer), "lwc1    $%d, %d(%s)", rt, simmediate, REG[rs]); break;
        case 0x32: // LWC2 (GTE data load)
            snprintf(disasm_buffer, sizeof(disasm_buffer), "lwc2    $%d, %d(%s)", rt, simmediate, REG[rs]); break;
        case 0x33: // LWC3
            snprintf(disasm_buffer, sizeof(disasm_buffer), "lwc3    $%d, %d(%s)", rt, simmediate, REG[rs]); break;
        case 0x38: // SWC0
            snprintf(disasm_buffer, sizeof(disasm_buffer), "swc0    $%d, %d(%s)", rt, simmediate, REG[rs]); break;
        case 0x39: // SWC1
            snprintf(disasm_buffer, sizeof(disasm_buffer), "swc1    $%d, %d(%s)", rt, simmediate, REG[rs]); break;
        case 0x3A: // SWC2 (GTE data store)
            snprintf(disasm_buffer, sizeof(disasm_buffer), "swc2    $%d, %d(%s)", rt, simmediate, REG[rs]); break;
        case 0x3B: // SWC3
            snprintf(disasm_buffer, sizeof(disasm_buffer), "swc3    $%d, %d(%s)", rt, simmediate, REG[rs]); break;

        default:
            snprintf(disasm_buffer, sizeof(disasm_buffer), "?unknown op=0x%02x rs=%s rt=%s rd=%s imm=0x%04x", opcode, REG[rs], REG[rt], REG[rd], immediate); break;
    }

    return disasm_buffer;
}
