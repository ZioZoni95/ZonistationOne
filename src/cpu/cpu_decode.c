#include "cpu.h"

// --- Instruction Decoding Logic ---
/**
 * @brief Decodes instruction and calls the appropriate handler.
 */
void decode_and_execute(Cpu* cpu, uint32_t instruction) {
    // PlayStation CPU opcode decoding (see cpuspecifications.md)
    // Primary opcode: bits 26..31
    uint32_t opcode = instr_function(instruction);

    switch(opcode) {
        // R-Type (SPECIAL, opcode 0x00)
        case 0x00: {
            uint32_t subfunc = instr_subfunction(instruction);
            switch(subfunc) {
                case 0x00: op_sll(cpu, instruction); break;     // SLL
                case 0x02: op_srl(cpu, instruction); break;     // SRL
                case 0x03: op_sra(cpu, instruction); break;     // SRA
                case 0x04: op_sllv(cpu, instruction); break;    // SLLV
                case 0x06: op_srlv(cpu, instruction); break;    // SRLV
                case 0x07: op_srav(cpu, instruction); break;    // SRAV
                case 0x08: op_jr(cpu, instruction); break;      // JR
                case 0x09: op_jalr(cpu, instruction); break;    // JALR
                case 0x0C: op_syscall(cpu, instruction); break; // SYSCALL
                case 0x0D: op_break(cpu, instruction); break;   // BREAK
                case 0x10: op_mfhi(cpu, instruction); break;    // MFHI
                case 0x11: op_mthi(cpu, instruction); break;    // MTHI
                case 0x12: op_mflo(cpu, instruction); break;    // MFLO
                case 0x13: op_mtlo(cpu, instruction); break;    // MTLO
                case 0x18: op_mult(cpu, instruction); break;    // MULT
                case 0x19: op_multu(cpu, instruction); break;   // MULTU
                case 0x1A: op_div(cpu, instruction); break;     // DIV
                case 0x1B: op_divu(cpu, instruction); break;    // DIVU
                case 0x20: op_add(cpu, instruction); break;     // ADD
                case 0x21: op_addu(cpu, instruction); break;    // ADDU
                case 0x22: op_sub(cpu, instruction); break;     // SUB
                case 0x23: op_subu(cpu, instruction); break;    // SUBU
                case 0x24: op_and(cpu, instruction); break;     // AND
                case 0x25: op_or(cpu, instruction); break;      // OR
                case 0x26: op_xor(cpu, instruction); break;     // XOR
                case 0x27: op_nor(cpu, instruction); break;     // NOR
                case 0x2A: op_slt(cpu, instruction); break;     // SLT
                case 0x2B: op_sltu(cpu, instruction); break;    // SLTU
                default:
                    // Reserved/illegal secondary opcode: raise Reserved Instruction Exception (excode=0x0A)
                    op_illegal(cpu, instruction);
                    break;
            }
            break;
        }

        // J-Type
        case 0x02: op_j(cpu, instruction); break;       // J
        case 0x03: op_jal(cpu, instruction); break;     // JAL

        // I-Type (Branches)
        case 0x04: op_beq(cpu, instruction); break;     // BEQ
        case 0x05: op_bne(cpu, instruction); break;     // BNE
        case 0x06: op_blez(cpu, instruction); break;    // BLEZ
        case 0x07: op_bgtz(cpu, instruction); break;    // BGTZ

        // I-Type (Immediate Arithmetic/Logical)
        case 0x08: op_addi(cpu, instruction); break;    // ADDI
        case 0x09: op_addiu(cpu, instruction); break;   // ADDIU
        case 0x0A: op_slti(cpu, instruction); break;    // SLTI
        case 0x0B: op_sltiu(cpu, instruction); break;   // SLTIU
        case 0x0C: op_andi(cpu, instruction); break;    // ANDI
        case 0x0D: op_ori(cpu, instruction); break;     // ORI
        case 0x0E: op_xori(cpu, instruction); break;    // XORI
        case 0x0F: op_lui(cpu, instruction); break;     // LUI

        // I-Type (Loads)
        case 0x20: op_lb(cpu, instruction); break;      // LB
        case 0x21: op_lh(cpu, instruction); break;      // LH
        case 0x22: op_lwl(cpu, instruction); break;     // LWL
        case 0x23: op_lw(cpu, instruction); break;      // LW
        case 0x24: op_lbu(cpu, instruction); break;     // LBU
        case 0x25: op_lhu(cpu, instruction); break;     // LHU
        case 0x26: op_lwr(cpu, instruction); break;     // LWR

        // I-Type (Stores)
        case 0x28: op_sb(cpu, instruction); break;      // SB
        case 0x29: op_sh(cpu, instruction); break;      // SH
        case 0x2A: op_swl(cpu, instruction); break;     // SWL
        case 0x2B: op_sw(cpu, instruction); break;      // SW
        case 0x2E: op_swr(cpu, instruction); break;     // SWR

        // Coprocessor Instructions
        case 0x10: op_cop0(cpu, instruction); break;    // COP0 (System Control)
        case 0x11: op_cop1(cpu, instruction); break;    // COP1 (FPU - Unused -> Exception)
        case 0x12: op_cop2(cpu, instruction); break;    // COP2 (GTE)
        case 0x13: op_cop3(cpu, instruction); break;    // COP3 (Unused -> Exception)

        // Coprocessor Load/Store
        case 0x30: op_lwc0(cpu, instruction); break;    // LWC0 (-> Exception)
        case 0x31: op_lwc1(cpu, instruction); break;    // LWC1 (-> Exception)
        case 0x32: op_lwc2(cpu, instruction); break;    // LWC2 (GTE Load)
        case 0x33: op_lwc3(cpu, instruction); break;    // LWC3 (-> Exception)
        case 0x38: op_swc0(cpu, instruction); break;    // SWC0 (-> Exception)
        case 0x39: op_swc1(cpu, instruction); break;    // SWC1 (-> Exception)
        case 0x3A: op_swc2(cpu, instruction); break;    // SWC2 (GTE Store)
        case 0x3B: op_swc3(cpu, instruction); break;    // SWC3 (-> Exception)

        // Special Branch (REGIMM: BGEZ/BLTZ etc.)
        case 0x01: op_bxx(cpu, instruction); break;     // Handles REGIMM branches

        // Default: Illegal/Unhandled Opcode
        default:
            // Reserved/illegal primary opcode: raise Reserved Instruction Exception (excode=0x0A)
            op_illegal(cpu, instruction);
            break;
    }
    // Note: Load delay for load instructions is handled in main execution cycle (see spec)
}