#include "cpu.h"
#include "memory.h"
#include <stdio.h>
#include <string.h>

// MIPS register names for debugging
static const char* register_names[MIPS_REG_COUNT] = {
    "zero", "at", "v0", "v1", "a0", "a1", "a2", "a3",
    "t0", "t1", "t2", "t3", "t4", "t5", "t6", "t7", 
    "s0", "s1", "s2", "s3", "s4", "s5", "s6", "s7",
    "t8", "t9", "k0", "k1", "gp", "sp", "fp", "ra"
};

void cpu_init(mips_cpu_t* cpu) {
    memset(cpu, 0, sizeof(mips_cpu_t));
    cpu_reset(cpu);
}

void cpu_reset(mips_cpu_t* cpu) {
    // Clear all registers
    memset(cpu->gpr, 0, sizeof(cpu->gpr));
    
    // Set initial PC to BIOS entry point
    cpu->pc = MIPS_PC_RESET;
    cpu->next_pc = cpu->pc + 4;
    
    // Initialize stack pointer
    cpu->gpr[REG_SP] = MIPS_SP_RESET;
    
    // Clear special registers
    cpu->hi = 0;
    cpu->lo = 0;
    
    // Initialize coprocessor 0 registers
    memset(cpu->cop0_regs, 0, sizeof(cpu->cop0_regs));
    
    // Set PlayStation 1 specific COP0 register values
    cpu->cop0_regs[COP0_PRID] = 0x00000002;      // Processor ID (R3000A)
    cpu->cop0_regs[COP0_STATUS] = 0x10400000;    // Status: CU0=1, BEV=1 (boot exception vectors)
    cpu->cop0_regs[COP0_CAUSE] = 0x00000000;     // No pending exceptions
    cpu->cop0_regs[COP0_EPC] = 0x00000000;       // Exception PC
    cpu->cop0_regs[COP0_BADVADDR] = 0x00000000;  // Bad virtual address
    
    // Set initial CPU state
    cpu->in_branch_delay_slot = false;
    cpu->exception_pending = false;
    cpu->cycle_count = 0;
    
    printf("[CPU] Reset to PC=0x%08X, SP=0x%08X\n", cpu->pc, cpu->gpr[REG_SP]);
}

psx_result_t cpu_step(mips_cpu_t* cpu, psx_memory_t* memory) {
    // Check if BIOS is loaded
    if (!memory->bios_loaded) {
        return PSX_ERROR_BIOS_NOT_LOADED;
    }
    
    // Fetch instruction from memory
    u32 instruction_word = memory_read32(memory, cpu->pc);
    
    // Decode instruction
    mips_instruction_t instr = cpu_decode_instruction(instruction_word);
    instr.opcode = instruction_word;
    
    #ifdef DEBUG
    cpu_print_instruction(cpu->pc, &instr);
    #endif
    
    // Update PC for next instruction
    u32 current_pc = cpu->pc;
    cpu->pc = cpu->next_pc;
    cpu->next_pc += 4;
    
    // Execute instruction
    psx_result_t result = cpu_execute_instruction(cpu, memory, &instr);
    
    // Handle branch delay slot
    if (cpu->in_branch_delay_slot) {
        cpu->in_branch_delay_slot = false;
    }
    
    // Increment cycle counter
    cpu->cycle_count++;
    
    return result;
}

mips_instruction_t cpu_decode_instruction(u32 opcode) {
    mips_instruction_t instr = {0};
    
    // Extract instruction fields
    instr.opcode = opcode;
    instr.rs = (opcode >> 21) & 0x1F;
    instr.rt = (opcode >> 16) & 0x1F;
    instr.rd = (opcode >> 11) & 0x1F;
    instr.immediate = opcode & 0xFFFF;
    instr.target = opcode & 0x3FFFFFF;
    instr.offset = (s16)(opcode & 0xFFFF);  // Sign-extend for branches
    
    return instr;
}

psx_result_t cpu_execute_instruction(mips_cpu_t* cpu, psx_memory_t* memory, 
                                    const mips_instruction_t* instr) {
    u32 opcode = (instr->opcode >> 26) & 0x3F;
    u32 rs_val = cpu_get_register(cpu, instr->rs);
    u32 rt_val = cpu_get_register(cpu, instr->rt);
    
    switch (opcode) {
        case OPCODE_SPECIAL: {
            // R-type instructions
            u32 function = instr->opcode & 0x3F;
            switch (function) {
                case FUNCT_SLL: // SLL (Shift Left Logical)
                    if (instr->rd != 0) {  // Don't write to $zero
                        u32 shamt = (instr->opcode >> 6) & 0x1F;
                        cpu_set_register(cpu, instr->rd, rt_val << shamt);
                    }
                    break;
                case FUNCT_SRL: // SRL (Shift Right Logical)
                    if (instr->rd != 0) {
                        u32 shamt = (instr->opcode >> 6) & 0x1F;
                        cpu_set_register(cpu, instr->rd, rt_val >> shamt);
                    }
                    break;
                case FUNCT_SRA: // SRA (Shift Right Arithmetic)
                    if (instr->rd != 0) {
                        u32 shamt = (instr->opcode >> 6) & 0x1F;
                        cpu_set_register(cpu, instr->rd, (u32)((s32)rt_val >> shamt));
                    }
                    break;
                case FUNCT_SLLV: // SLLV (Shift Left Logical Variable)
                    if (instr->rd != 0) {
                        cpu_set_register(cpu, instr->rd, rt_val << (rs_val & 0x1F));
                    }
                    break;
                case FUNCT_SRLV: // SRLV (Shift Right Logical Variable)
                    if (instr->rd != 0) {
                        cpu_set_register(cpu, instr->rd, rt_val >> (rs_val & 0x1F));
                    }
                    break;
                case FUNCT_SRAV: // SRAV (Shift Right Arithmetic Variable)
                    if (instr->rd != 0) {
                        cpu_set_register(cpu, instr->rd, (u32)((s32)rt_val >> (rs_val & 0x1F)));
                    }
                    break;
                case FUNCT_ADD: // ADD
                    if (instr->rd != 0) {
                        cpu_set_register(cpu, instr->rd, rs_val + rt_val);
                    }
                    break;
                case FUNCT_ADDU: // ADDU
                    if (instr->rd != 0) {
                        cpu_set_register(cpu, instr->rd, rs_val + rt_val);
                    }
                    break;
                case FUNCT_SUB: // SUB
                    if (instr->rd != 0) {
                        cpu_set_register(cpu, instr->rd, rs_val - rt_val);
                    }
                    break;
                case FUNCT_SUBU: // SUBU
                    if (instr->rd != 0) {
                        cpu_set_register(cpu, instr->rd, rs_val - rt_val);
                    }
                    break;
                case FUNCT_AND: // AND
                    if (instr->rd != 0) {
                        cpu_set_register(cpu, instr->rd, rs_val & rt_val);
                    }
                    break;
                case FUNCT_OR: // OR
                    if (instr->rd != 0) {
                        cpu_set_register(cpu, instr->rd, rs_val | rt_val);
                    }
                    break;
                case FUNCT_XOR: // XOR
                    if (instr->rd != 0) {
                        cpu_set_register(cpu, instr->rd, rs_val ^ rt_val);
                    }
                    break;
                case FUNCT_NOR: // NOR
                    if (instr->rd != 0) {
                        cpu_set_register(cpu, instr->rd, ~(rs_val | rt_val));
                    }
                    break;
                case FUNCT_SLT: // SLT (Set Less Than)
                    if (instr->rd != 0) {
                        cpu_set_register(cpu, instr->rd, ((s32)rs_val < (s32)rt_val) ? 1 : 0);
                    }
                    break;
                case FUNCT_SLTU: // SLTU (Set Less Than Unsigned)
                    if (instr->rd != 0) {
                        cpu_set_register(cpu, instr->rd, (rs_val < rt_val) ? 1 : 0);
                    }
                    break;
                case FUNCT_JR: // JR
                    cpu->next_pc = rs_val;
                    cpu->in_branch_delay_slot = true;
                    break;
                case FUNCT_JALR: // JALR (Jump and Link Register)
                    if (instr->rd != 0) {
                        cpu_set_register(cpu, instr->rd, cpu->next_pc);
                    }
                    cpu->next_pc = rs_val;
                    cpu->in_branch_delay_slot = true;
                    break;
                case FUNCT_MFHI: // MFHI (Move From HI)
                    if (instr->rd != 0) {
                        cpu_set_register(cpu, instr->rd, cpu->hi);
                    }
                    break;
                case FUNCT_MTHI: // MTHI (Move To HI)
                    cpu->hi = rs_val;
                    break;
                case FUNCT_MFLO: // MFLO (Move From LO)
                    if (instr->rd != 0) {
                        cpu_set_register(cpu, instr->rd, cpu->lo);
                    }
                    break;
                case FUNCT_MTLO: // MTLO (Move To LO)
                    cpu->lo = rs_val;
                    break;
                case FUNCT_MULT: // MULT (Multiply)
                    {
                        s64 result = (s64)(s32)rs_val * (s64)(s32)rt_val;
                        cpu->lo = (u32)(result & 0xFFFFFFFF);
                        cpu->hi = (u32)(result >> 32);
                    }
                    break;
                case FUNCT_MULTU: // MULTU (Multiply Unsigned)
                    {
                        u64 result = (u64)rs_val * (u64)rt_val;
                        cpu->lo = (u32)(result & 0xFFFFFFFF);
                        cpu->hi = (u32)(result >> 32);
                    }
                    break;
                case FUNCT_DIV: // DIV (Divide)
                    if (rt_val != 0) {
                        cpu->lo = (u32)((s32)rs_val / (s32)rt_val);
                        cpu->hi = (u32)((s32)rs_val % (s32)rt_val);
                    } else {
                        // Division by zero behavior (implementation defined)
                        cpu->lo = ((s32)rs_val >= 0) ? 0xFFFFFFFF : 0x00000001;
                        cpu->hi = rs_val;
                    }
                    break;
                case FUNCT_DIVU: // DIVU (Divide Unsigned)
                    if (rt_val != 0) {
                        cpu->lo = rs_val / rt_val;
                        cpu->hi = rs_val % rt_val;
                    } else {
                        // Division by zero behavior (implementation defined)
                        cpu->lo = 0xFFFFFFFF;
                        cpu->hi = rs_val;
                    }
                    break;
                default:
                    printf("[CPU] Unimplemented SPECIAL function: 0x%02X\n", function);
                    return PSX_ERROR_INVALID_INSTRUCTION;
            }
            break;
        }
        
        case OPCODE_J: // Jump
            cpu->next_pc = (cpu->pc & 0xF0000000) | (instr->target << 2);
            cpu->in_branch_delay_slot = true;
            break;
            
        case OPCODE_JAL: // Jump and link
            cpu_set_register(cpu, REG_RA, cpu->next_pc);
            cpu->next_pc = (cpu->pc & 0xF0000000) | (instr->target << 2);
            cpu->in_branch_delay_slot = true;
            break;
            
        case OPCODE_BEQ: // Branch if equal
            if (rs_val == rt_val) {
                cpu->next_pc = cpu->pc + (instr->offset << 2);
                cpu->in_branch_delay_slot = true;
            }
            break;
            
        case OPCODE_BNE: // Branch if not equal
            if (rs_val != rt_val) {
                cpu->next_pc = cpu->pc + (instr->offset << 2);
                cpu->in_branch_delay_slot = true;
            }
            break;
            
        case OPCODE_ADDI: // Add immediate
            if (instr->rt != 0) {
                cpu_set_register(cpu, instr->rt, rs_val + (s16)instr->immediate);
            }
            break;
            
        case OPCODE_ADDIU: // Add immediate unsigned
            if (instr->rt != 0) {
                cpu_set_register(cpu, instr->rt, rs_val + (s16)instr->immediate);
            }
            break;
            
        case OPCODE_SLTI: // Set Less Than Immediate
            if (instr->rt != 0) {
                cpu_set_register(cpu, instr->rt, ((s32)rs_val < (s16)instr->immediate) ? 1 : 0);
            }
            break;
            
        case OPCODE_SLTIU: // Set Less Than Immediate Unsigned
            if (instr->rt != 0) {
                cpu_set_register(cpu, instr->rt, (rs_val < (u32)(s16)instr->immediate) ? 1 : 0);
            }
            break;
            
        case OPCODE_ANDI: // AND immediate
            if (instr->rt != 0) {
                cpu_set_register(cpu, instr->rt, rs_val & instr->immediate);
            }
            break;
            
        case OPCODE_XORI: // XOR immediate
            if (instr->rt != 0) {
                cpu_set_register(cpu, instr->rt, rs_val ^ instr->immediate);
            }
            break;
            
        case OPCODE_LUI: // Load upper immediate
            if (instr->rt != 0) {
                cpu_set_register(cpu, instr->rt, (u32)instr->immediate << 16);
            }
            break;
            
        case OPCODE_LW: // Load word
            if (instr->rt != 0) {
                u32 address = rs_val + (s16)instr->immediate;
                u32 value = memory_read32(memory, address);
                cpu_set_register(cpu, instr->rt, value);
            }
            break;
            
        case OPCODE_SW: // Store word
            {
                u32 address = rs_val + (s16)instr->immediate;
                memory_write32(memory, address, rt_val);
            }
            break;
            
        case OPCODE_ORI: // OR immediate
            if (instr->rt != 0) {
                cpu_set_register(cpu, instr->rt, rs_val | instr->immediate);
            }
            break;
            
        case OPCODE_COP0: { // Coprocessor 0 operations
            u32 cop0_func = (instr->opcode >> 21) & 0x1F;
            switch (cop0_func) {
                case COP0_MFC0: // Move From COP0
                    cpu_cop0_mfc0(cpu, instr->rt, instr->rd);
                    break;
                case COP0_MTC0: // Move To COP0
                    cpu_cop0_mtc0(cpu, instr->rt, instr->rd);
                    break;
                case COP0_RFE:  // Return From Exception
                    cpu_cop0_rfe(cpu);
                    break;
                default:
                    printf("[CPU] Unimplemented COP0 function: 0x%02X\n", cop0_func);
                    return PSX_ERROR_INVALID_INSTRUCTION;
            }
            break;
        }
            
        default:
            printf("[CPU] Unimplemented instruction opcode: 0x%02X\n", opcode);
            return PSX_ERROR_INVALID_INSTRUCTION;
    }
    
    return PSX_OK;
}

u32 cpu_get_register(const mips_cpu_t* cpu, u32 reg_index) {
    if (reg_index >= MIPS_REG_COUNT) {
        return 0;
    }
    
    // $zero always returns 0
    if (reg_index == REG_ZERO) {
        return 0;
    }
    
    return cpu->gpr[reg_index];
}

void cpu_set_register(mips_cpu_t* cpu, u32 reg_index, u32 value) {
    if (reg_index >= MIPS_REG_COUNT || reg_index == REG_ZERO) {
        return;  // Can't write to $zero or invalid register
    }
    
    cpu->gpr[reg_index] = value;
}

void cpu_branch(mips_cpu_t* cpu, u32 target_address) {
    cpu->next_pc = target_address;
    cpu->in_branch_delay_slot = true;
}

void cpu_handle_branch_delay_slot(mips_cpu_t* cpu) {
    cpu->in_branch_delay_slot = false;
}

void cpu_trigger_exception(mips_cpu_t* cpu, u32 exception_code) {
    // TODO: Implement exception handling
    printf("[CPU] Exception triggered: code 0x%X\n", exception_code);
    cpu->exception_pending = true;
}

void cpu_print_registers(const mips_cpu_t* cpu) {
    printf("[CPU] Registers:\n");
    for (int i = 0; i < MIPS_REG_COUNT; i += 4) {
        printf("  %s=0x%08X  %s=0x%08X  %s=0x%08X  %s=0x%08X\n",
               register_names[i], cpu->gpr[i],
               register_names[i+1], cpu->gpr[i+1], 
               register_names[i+2], cpu->gpr[i+2],
               register_names[i+3], cpu->gpr[i+3]);
    }
    printf("  PC=0x%08X  NextPC=0x%08X  HI=0x%08X  LO=0x%08X\n",
           cpu->pc, cpu->next_pc, cpu->hi, cpu->lo);
    printf("  Cycles=%llu  BranchDelay=%s\n",
           (unsigned long long)cpu->cycle_count,
           cpu->in_branch_delay_slot ? "true" : "false");
}

void cpu_print_instruction(u32 address, const mips_instruction_t* instr) {
    u32 opcode = (instr->opcode >> 26) & 0x3F;
    printf("[CPU] 0x%08X: 0x%08X  ", address, instr->opcode);
    
    // Basic instruction disassembly
    switch (opcode) {
        case OPCODE_SPECIAL: {
            u32 function = instr->opcode & 0x3F;
            switch (function) {
                case FUNCT_SLL: printf("sll $%s, $%s, %d", register_names[instr->rd], 
                                      register_names[instr->rt], (instr->opcode >> 6) & 0x1F); break;
                case FUNCT_SRL: printf("srl $%s, $%s, %d", register_names[instr->rd], 
                                      register_names[instr->rt], (instr->opcode >> 6) & 0x1F); break;
                case FUNCT_SRA: printf("sra $%s, $%s, %d", register_names[instr->rd], 
                                      register_names[instr->rt], (instr->opcode >> 6) & 0x1F); break;
                case FUNCT_SLLV: printf("sllv $%s, $%s, $%s", register_names[instr->rd],
                                       register_names[instr->rt], register_names[instr->rs]); break;
                case FUNCT_SRLV: printf("srlv $%s, $%s, $%s", register_names[instr->rd],
                                       register_names[instr->rt], register_names[instr->rs]); break;
                case FUNCT_SRAV: printf("srav $%s, $%s, $%s", register_names[instr->rd],
                                       register_names[instr->rt], register_names[instr->rs]); break;
                case FUNCT_ADD: printf("add $%s, $%s, $%s", register_names[instr->rd], 
                                      register_names[instr->rs], register_names[instr->rt]); break;
                case FUNCT_ADDU: printf("addu $%s, $%s, $%s", register_names[instr->rd],
                                       register_names[instr->rs], register_names[instr->rt]); break;
                case FUNCT_SUB: printf("sub $%s, $%s, $%s", register_names[instr->rd],
                                      register_names[instr->rs], register_names[instr->rt]); break;
                case FUNCT_SUBU: printf("subu $%s, $%s, $%s", register_names[instr->rd],
                                       register_names[instr->rs], register_names[instr->rt]); break;
                case FUNCT_AND: printf("and $%s, $%s, $%s", register_names[instr->rd],
                                      register_names[instr->rs], register_names[instr->rt]); break;
                case FUNCT_OR: printf("or $%s, $%s, $%s", register_names[instr->rd],
                                     register_names[instr->rs], register_names[instr->rt]); break;
                case FUNCT_XOR: printf("xor $%s, $%s, $%s", register_names[instr->rd],
                                      register_names[instr->rs], register_names[instr->rt]); break;
                case FUNCT_NOR: printf("nor $%s, $%s, $%s", register_names[instr->rd],
                                      register_names[instr->rs], register_names[instr->rt]); break;
                case FUNCT_SLT: printf("slt $%s, $%s, $%s", register_names[instr->rd],
                                      register_names[instr->rs], register_names[instr->rt]); break;
                case FUNCT_SLTU: printf("sltu $%s, $%s, $%s", register_names[instr->rd],
                                       register_names[instr->rs], register_names[instr->rt]); break;
                case FUNCT_JR: printf("jr $%s", register_names[instr->rs]); break;
                case FUNCT_JALR: printf("jalr $%s, $%s", register_names[instr->rd], register_names[instr->rs]); break;
                case FUNCT_MFHI: printf("mfhi $%s", register_names[instr->rd]); break;
                case FUNCT_MTHI: printf("mthi $%s", register_names[instr->rs]); break;
                case FUNCT_MFLO: printf("mflo $%s", register_names[instr->rd]); break;
                case FUNCT_MTLO: printf("mtlo $%s", register_names[instr->rs]); break;
                case FUNCT_MULT: printf("mult $%s, $%s", register_names[instr->rs], register_names[instr->rt]); break;
                case FUNCT_MULTU: printf("multu $%s, $%s", register_names[instr->rs], register_names[instr->rt]); break;
                case FUNCT_DIV: printf("div $%s, $%s", register_names[instr->rs], register_names[instr->rt]); break;
                case FUNCT_DIVU: printf("divu $%s, $%s", register_names[instr->rs], register_names[instr->rt]); break;
                default: printf("unknown_special(0x%02X)", function); break;
            }
            break;
        }
        case OPCODE_J: printf("j 0x%08X", instr->target << 2); break;
        case OPCODE_JAL: printf("jal 0x%08X", instr->target << 2); break;
        case OPCODE_ADDI: printf("addi $%s, $%s, %d", register_names[instr->rt], 
                                register_names[instr->rs], (s16)instr->immediate); break;
        case OPCODE_ADDIU: printf("addiu $%s, $%s, %d", register_names[instr->rt], 
                                 register_names[instr->rs], (s16)instr->immediate); break;
        case OPCODE_SLTI: printf("slti $%s, $%s, %d", register_names[instr->rt], 
                                register_names[instr->rs], (s16)instr->immediate); break;
        case OPCODE_SLTIU: printf("sltiu $%s, $%s, %d", register_names[instr->rt], 
                                 register_names[instr->rs], (s16)instr->immediate); break;
        case OPCODE_ANDI: printf("andi $%s, $%s, 0x%04X", register_names[instr->rt], 
                                register_names[instr->rs], instr->immediate); break;
        case OPCODE_XORI: printf("xori $%s, $%s, 0x%04X", register_names[instr->rt], 
                                register_names[instr->rs], instr->immediate); break;
        case OPCODE_LUI: printf("lui $%s, 0x%04X", register_names[instr->rt], instr->immediate); break;
        case OPCODE_COP0: {
            u32 cop0_func = (instr->opcode >> 21) & 0x1F;
            switch (cop0_func) {
                case COP0_MFC0: printf("mfc0 $%s, $%u", register_names[instr->rt], instr->rd); break;
                case COP0_MTC0: printf("mtc0 $%s, $%u", register_names[instr->rt], instr->rd); break;
                case COP0_RFE:  printf("rfe"); break;
                default: printf("cop0_unknown(0x%02X)", cop0_func); break;
            }
            break;
        }
        case OPCODE_LW: printf("lw $%s, %d($%s)", register_names[instr->rt], 
                              (s16)instr->immediate, register_names[instr->rs]); break;
        case OPCODE_SW: printf("sw $%s, %d($%s)", register_names[instr->rt],
                              (s16)instr->immediate, register_names[instr->rs]); break;
        default: printf("unknown(0x%02X)", opcode); break;
    }
    printf("\n");
}

instruction_type_t cpu_get_instruction_type(u32 opcode) {
    u32 op = (opcode >> 26) & 0x3F;
    
    if (op == OPCODE_SPECIAL) {
        return INSTR_TYPE_R;
    } else if (op == OPCODE_J || op == OPCODE_JAL) {
        return INSTR_TYPE_J;
    } else if (op != 0) {
        return INSTR_TYPE_I;
    }
    
    return INSTR_TYPE_UNKNOWN;
}

// COP0 (Coprocessor 0) functions
void cpu_cop0_mfc0(mips_cpu_t* cpu, u32 rt, u32 rd) {
    if (rt == 0) return; // Don't write to $zero
    
    // Move from COP0 register to CPU register
    if (rd < 32) {
        cpu_set_register(cpu, rt, cpu->cop0_regs[rd]);
    } else {
        cpu_set_register(cpu, rt, 0);
    }
}

void cpu_cop0_mtc0(mips_cpu_t* cpu, u32 rt, u32 rd) {
    if (rd >= 32) return; // Invalid COP0 register
    
    u32 value = cpu_get_register(cpu, rt);
    
    // Handle special registers with read-only fields
    switch (rd) {
        case COP0_STATUS: // Status register
            cpu->cop0_regs[COP0_STATUS] = value;
            break;
            
        case COP0_CAUSE:  // Cause register (bits 10-11 writable only)
            cpu->cop0_regs[COP0_CAUSE] = (cpu->cop0_regs[COP0_CAUSE] & ~0x0300) | (value & 0x0300);
            break;
            
        case COP0_PRID:   // Processor ID - read only
        case COP0_EPC:    // Exception PC - typically read only
        case COP0_BADVADDR: // Bad virtual address - read only
        case COP0_JUMPDEST: // Jump destination - read only
            // Don't write to read-only registers
            break;
            
        default:
            cpu->cop0_regs[rd] = value;
            break;
    }
}

void cpu_cop0_rfe(mips_cpu_t* cpu) {
    // Return From Exception - restore previous interrupt/exception state
    u32 status = cpu->cop0_regs[COP0_STATUS];
    
    // Shift the interrupt enable stack right by 2 bits
    // This restores the previous interrupt enable state
    status = (status & ~0x3F) | ((status & 0x3C) >> 2);
    
    cpu->cop0_regs[COP0_STATUS] = status;
}