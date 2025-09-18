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
                case 0x00: // SLL
                    if (instr->rd != 0) {  // Don't write to $zero
                        u32 shamt = (instr->opcode >> 6) & 0x1F;
                        cpu_set_register(cpu, instr->rd, rt_val << shamt);
                    }
                    break;
                case 0x20: // ADD
                    if (instr->rd != 0) {
                        cpu_set_register(cpu, instr->rd, rs_val + rt_val);
                    }
                    break;
                case 0x21: // ADDU
                    if (instr->rd != 0) {
                        cpu_set_register(cpu, instr->rd, rs_val + rt_val);
                    }
                    break;
                case 0x08: // JR
                    cpu->next_pc = rs_val;
                    cpu->in_branch_delay_slot = true;
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
                case 0x00: printf("sll $%s, $%s, %d", register_names[instr->rd], 
                                 register_names[instr->rt], (instr->opcode >> 6) & 0x1F); break;
                case 0x20: printf("add $%s, $%s, $%s", register_names[instr->rd], 
                                 register_names[instr->rs], register_names[instr->rt]); break;
                case 0x21: printf("addu $%s, $%s, $%s", register_names[instr->rd],
                                 register_names[instr->rs], register_names[instr->rt]); break;
                case 0x08: printf("jr $%s", register_names[instr->rs]); break;
                default: printf("unknown_special(0x%02X)", function); break;
            }
            break;
        }
        case OPCODE_J: printf("j 0x%08X", instr->target << 2); break;
        case OPCODE_JAL: printf("jal 0x%08X", instr->target << 2); break;
        case OPCODE_ADDI: printf("addi $%s, $%s, %d", register_names[instr->rt], 
                                register_names[instr->rs], (s16)instr->immediate); break;
        case OPCODE_LUI: printf("lui $%s, 0x%04X", register_names[instr->rt], instr->immediate); break;
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