#include "../include/psx_cpu.h"
#include "../include/psx_memory.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// PSX-SPX: MIPS R3000A CPU implementation following guide.tex structure

void cpu_init(psx_cpu_t* cpu) {
    // Guide.tex: Initialize CPU to reset state
    memset(cpu, 0, sizeof(psx_cpu_t));
    cpu_reset(cpu);
}

void cpu_reset(psx_cpu_t* cpu) {
    // PSX-SPX: CPU Reset state
    memset(cpu->gpr, 0, sizeof(cpu->gpr));
    memset(cpu->cop0_regs, 0, sizeof(cpu->cop0_regs));
    
    // PSX-SPX: Reset values
    cpu->pc = 0xBFC00000;  // BIOS entry point
    cpu->hi = 0;
    cpu->lo = 0;
    
    // Guide.tex: Reset delay slot state
    cpu->load_reg = 0;
    cpu->load_value = 0;
    cpu->load_pending = false;
    cpu->branch_delay = false;
    cpu->next_pc = cpu->pc + 4;
    
    // PSX-SPX: COP0 reset values
    cpu->cop0_regs[COP0_STATUS] = 0x10900000;  // Boot exception vectors, cache isolated
    cpu->cop0_regs[COP0_PRID] = 0x00000002;   // R3000A processor ID
    
    // Exception state
    cpu->exception_pending = false;
    cpu->exception_cause = 0;
    
    printf("[CPU] Reset complete, PC=0x%08X\n", cpu->pc);
}

void cpu_step(psx_cpu_t* cpu) {
    // Guide.tex: Handle pending load delay slot
    if (cpu->load_pending) {
        cpu_set_gpr(cpu, cpu->load_reg, cpu->load_value);
        cpu->load_pending = false;
    }
    
    // Check for pending exceptions
    if (cpu->exception_pending) {
        // Handle exception (TODO: implement exception handling)
        cpu->exception_pending = false;
        return;
    }
    
    // Guide.tex: Fetch instruction from current PC
    u32 instruction = memory_read32(cpu->pc);
    
    printf("[CPU] PC=0x%08X, INSTR=0x%08X\n", cpu->pc, instruction);
    
    // Guide.tex: Handle branch delay slot
    u32 current_pc = cpu->pc;
    if (cpu->branch_delay) {
        cpu->pc = cpu->next_pc;
        cpu->branch_delay = false;
    } else {
        cpu->pc = cpu->pc + 4;
    }
    cpu->next_pc = cpu->pc + 4;
    
    // Guide.tex: Execute the fetched instruction
    cpu_execute_instruction(cpu, instruction);
}

void cpu_execute_instruction(psx_cpu_t* cpu, u32 instruction) {
    // Guide.tex: Decode instruction
    u32 opcode = instruction_opcode(instruction);
    
    switch (opcode) {
        case 0x00: // SPECIAL - Special function instructions
            {
                u32 funct = instruction_funct(instruction);
                switch (funct) {
                    case 0x00: // SLL - Shift Left Logical (NOP when all zeros)
                        {
                            u32 rt = instruction_rt(instruction);
                            u32 rd = instruction_rd(instruction);
                            u32 shamt = instruction_shamt(instruction);
                            
                            if (instruction == 0) {
                                printf("[CPU] NOP (sll $zero, $zero, 0)\n");
                            } else {
                                u32 result = cpu_get_gpr(cpu, rt) << shamt;
                                cpu_set_gpr(cpu, rd, result);
                                printf("[CPU] SLL r%d, r%d, %d -> 0x%08X\n", rd, rt, shamt, result);
                            }
                        }
                        break;
                        
                    case 0x25: // OR - Bitwise OR
                        {
                            u32 rs = instruction_rs(instruction);
                            u32 rt = instruction_rt(instruction);
                            u32 rd = instruction_rd(instruction);
                            // PSX-SPX: OR rd,rs,rt -> GPR[rd] = GPR[rs] | GPR[rt]
                            u32 result = cpu_get_gpr(cpu, rs) | cpu_get_gpr(cpu, rt);
                            cpu_set_gpr(cpu, rd, result);
                            printf("[CPU] OR r%d, r%d, r%d -> 0x%08X\n", rd, rs, rt, result);
                        }
                        break;
                        
                    case 0x2B: // SLTU - Set on Less Than Unsigned
                        {
                            u32 rs = instruction_rs(instruction);
                            u32 rt = instruction_rt(instruction);
                            u32 rd = instruction_rd(instruction);
                            // PSX-SPX: SLTU rd,rs,rt -> GPR[rd] = (GPR[rs] < GPR[rt]) ? 1 : 0 (unsigned)
                            u32 result = (cpu_get_gpr(cpu, rs) < cpu_get_gpr(cpu, rt)) ? 1 : 0;
                            cpu_set_gpr(cpu, rd, result);
                            printf("[CPU] SLTU r%d, r%d, r%d -> 0x%08X\n", rd, rs, rt, result);
                        }
                        break;
                        
                    case 0x21: // ADDU - Add Unsigned (no overflow exception)
                        {
                            u32 rs = instruction_rs(instruction);
                            u32 rt = instruction_rt(instruction);
                            u32 rd = instruction_rd(instruction);
                            // PSX-SPX: ADDU rd,rs,rt -> GPR[rd] = GPR[rs] + GPR[rt] (no overflow)
                            u32 result = cpu_get_gpr(cpu, rs) + cpu_get_gpr(cpu, rt);
                            cpu_set_gpr(cpu, rd, result);
                            printf("[CPU] ADDU r%d, r%d, r%d -> 0x%08X\n", rd, rs, rt, result);
                        }
                        break;
                        
                    default:
                        printf("[CPU] ERROR: Unimplemented SPECIAL function 0x%02X at PC=0x%08X\n", funct, cpu->pc - 4);
                        printf("[CPU] Instruction: 0x%08X\n", instruction);
                        exit(1);
                }
            }
            break;
        case 0x0F: // LUI - Load Upper Immediate
            {
                u32 rt = instruction_rt(instruction);
                u32 imm = instruction_imm(instruction);
                // PSX-SPX: LUI rt,imm -> GPR[rt] = imm << 16
                cpu_set_gpr(cpu, rt, imm << 16);
                printf("[CPU] LUI r%d, 0x%04X -> 0x%08X\n", rt, imm, imm << 16);
            }
            break;
            
        case 0x0D: // ORI - OR Immediate
            {
                u32 rs = instruction_rs(instruction);
                u32 rt = instruction_rt(instruction);
                u32 imm = instruction_imm(instruction);
                // PSX-SPX: ORI rt,rs,imm -> GPR[rt] = GPR[rs] OR imm
                u32 result = cpu_get_gpr(cpu, rs) | imm;
                cpu_set_gpr(cpu, rt, result);
                printf("[CPU] ORI r%d, r%d, 0x%04X -> 0x%08X\n", rt, rs, imm, result);
            }
            break;
            
        case 0x0C: // ANDI - AND Immediate
            {
                u32 rs = instruction_rs(instruction);
                u32 rt = instruction_rt(instruction);
                u32 imm = instruction_imm(instruction);
                // PSX-SPX: ANDI rt,rs,imm -> GPR[rt] = GPR[rs] AND imm
                u32 result = cpu_get_gpr(cpu, rs) & imm;
                cpu_set_gpr(cpu, rt, result);
                printf("[CPU] ANDI r%d, r%d, 0x%04X -> 0x%08X\n", rt, rs, imm, result);
            }
            break;
            
        case 0x09: // ADDIU - Add Immediate Unsigned
            {
                u32 rs = instruction_rs(instruction);
                u32 rt = instruction_rt(instruction);
                s32 imm = instruction_imm_signed(instruction);
                // PSX-SPX: ADDIU rt,rs,imm -> GPR[rt] = GPR[rs] + sign_extend(imm)
                u32 result = cpu_get_gpr(cpu, rs) + imm;
                cpu_set_gpr(cpu, rt, result);
                printf("[CPU] ADDIU r%d, r%d, %d -> 0x%08X\n", rt, rs, imm, result);
            }
            break;
            
        case 0x02: // J - Jump
            {
                u32 target = instruction_target(instruction);
                // Guide.tex: J target -> PC = (PC & 0xF0000000) | (target << 2)
                // Keep upper 4 bits of current PC, shift target left by 2 (instructions are 4-byte aligned)
                u32 jump_addr = (cpu->pc & 0xF0000000) | (target << 2);
                
                // Guide.tex: Branch delay slot handling - execute next instruction before jumping
                cpu->branch_delay = true;
                cpu->next_pc = jump_addr;
                
                printf("[CPU] J 0x%08X (branch delay slot active)\n", jump_addr);
            }
            break;
            
        case 0x03: // JAL - Jump and Link
            {
                u32 target = instruction_target(instruction);
                // PSX-SPX: JAL target -> GPR[31] = PC + 8; PC = (PC & 0xF0000000) | (target << 2)
                u32 jump_addr = (cpu->pc & 0xF0000000) | (target << 2);
                // Store return address (PC + 8 = address after branch delay slot)
                cpu_set_gpr(cpu, 31, cpu->pc + 4);
                
                // Guide.tex: Branch delay slot handling - execute next instruction before jumping
                cpu->branch_delay = true;
                cpu->next_pc = jump_addr;
                
                printf("[CPU] JAL 0x%08X, r31 = 0x%08X (branch delay slot active)\n", jump_addr, cpu->pc + 4);
            }
            break;
            
        case 0x10: // COP0 - Coprocessor 0 operations
            {
                u32 cop_op = instruction_rs(instruction);
                switch (cop_op) {
                    case 0x04: // MTC0 - Move To Coprocessor 0
                        {
                            u32 rt = instruction_rt(instruction);
                            u32 rd = instruction_rd(instruction);
                            u32 value = cpu_get_gpr(cpu, rt);
                            
                            // Guide.tex: Handle COP0 registers
                            switch (rd) {
                                case 3: // BPC - Breakpoint Program Counter
                                    cpu_set_cop0(cpu, 3, value);
                                    printf("[CPU] MTC0 r%d, COP0_BPC -> 0x%08X\n", rt, value);
                                    break;
                                    
                                case 5: // BDAM - Data Access Breakpoint Mask
                                    cpu_set_cop0(cpu, 5, value);
                                    printf("[CPU] MTC0 r%d, COP0_BDAM -> 0x%08X\n", rt, value);
                                    break;
                                    
                                case 6: // BPCM - Instruction Access Breakpoint Mask
                                    cpu_set_cop0(cpu, 6, value);
                                    printf("[CPU] MTC0 r%d, COP0_BPCM -> 0x%08X\n", rt, value);
                                    break;
                                    
                                case 7: // DCIC - Debug and Cache Invalidate Control
                                    cpu_set_cop0(cpu, 7, value);
                                    printf("[CPU] MTC0 r%d, COP0_DCIC -> 0x%08X\n", rt, value);
                                    break;
                                    
                                case 9: // COUNT - Processor cycle count
                                    cpu_set_cop0(cpu, 9, value);
                                    printf("[CPU] MTC0 r%d, COP0_COUNT -> 0x%08X\n", rt, value);
                                    break;
                                    
                                case 11: // COMPARE - Timer compare value
                                    cpu_set_cop0(cpu, 11, value);
                                    printf("[CPU] MTC0 r%d, COP0_COMPARE -> 0x%08X\n", rt, value);
                                    break;
                                    
                                case 13: // CAUSE - Exception cause register
                                    cpu_set_cop0(cpu, 13, value);
                                    printf("[CPU] MTC0 r%d, COP0_CAUSE -> 0x%08X\n", rt, value);
                                    break;
                                    
                                case 12: // STATUS - Status Register
                                    cpu_set_cop0(cpu, 12, value);
                                    printf("[CPU] MTC0 r%d, COP0_STATUS -> 0x%08X", rt, value);
                                    if (value & 0x10000) {
                                        printf(" (cache isolate bit set)");
                                    }
                                    printf("\n");
                                    break;
                                    
                                default:
                                    printf("[CPU] ERROR: Unhandled COP0 register %d in MTC0\n", rd);
                                    exit(1);
                            }
                        }
                        break;
                        
                    default:
                        printf("[CPU] ERROR: Unhandled COP0 operation 0x%02X\n", cop_op);
                        exit(1);
                }
            }
            break;
            
        case 0x05: // BNE - Branch on Not Equal
            {
                u32 rs = instruction_rs(instruction);
                u32 rt = instruction_rt(instruction);
                s32 offset = instruction_imm_signed(instruction);
                
                u32 rs_val = cpu_get_gpr(cpu, rs);
                u32 rt_val = cpu_get_gpr(cpu, rt);
                
                if (rs_val != rt_val) {
                    // Guide.tex: Branch delay slot - target address is relative to delay slot PC
                    u32 branch_addr = cpu->pc + (offset << 2);
                    cpu->branch_delay = true;
                    cpu->next_pc = branch_addr;
                    printf("[CPU] BNE r%d(0x%08X), r%d(0x%08X), %d -> BRANCH to 0x%08X\n", 
                           rs, rs_val, rt, rt_val, offset, branch_addr);
                } else {
                    printf("[CPU] BNE r%d(0x%08X), r%d(0x%08X), %d -> NO BRANCH\n", 
                           rs, rs_val, rt, rt_val, offset);
                }
            }
            break;
            
        case 0x08: // ADDI - Add Immediate
            {
                u32 rs = instruction_rs(instruction);
                u32 rt = instruction_rt(instruction);
                s32 imm = instruction_imm_signed(instruction);
                // PSX-SPX: ADDI rt,rs,imm -> GPR[rt] = GPR[rs] + sign_extend(imm)
                u32 result = cpu_get_gpr(cpu, rs) + imm;
                cpu_set_gpr(cpu, rt, result);
                printf("[CPU] ADDI r%d, r%d, %d -> 0x%08X\n", rt, rs, imm, result);
            }
            break;
            
        case 0x23: // LW - Load Word
            {
                u32 rs = instruction_rs(instruction);
                u32 rt = instruction_rt(instruction);
                s32 offset = instruction_imm_signed(instruction);
                // PSX-SPX: LW rt,offset(rs) -> GPR[rt] = [GPR[rs] + sign_extend(offset)]
                u32 addr = cpu_get_gpr(cpu, rs) + offset;
                u32 value = memory_read32(addr);
                cpu_set_gpr(cpu, rt, value);
                printf("[CPU] LW r%d, %d(r%d) -> [0x%08X] = 0x%08X\n", rt, offset, rs, addr, value);
            }
            break;
            
        case 0x2B: // SW - Store Word
            {
                u32 rs = instruction_rs(instruction);
                u32 rt = instruction_rt(instruction);
                s32 offset = instruction_imm_signed(instruction);
                // PSX-SPX: SW rt,offset(rs) -> [GPR[rs]+offset] = GPR[rt]
                u32 addr = cpu_get_gpr(cpu, rs) + offset;
                u32 value = cpu_get_gpr(cpu, rt);
                memory_write32(addr, value);
                printf("[CPU] SW r%d, %d(r%d) -> [0x%08X] = 0x%08X\n", rt, offset, rs, addr, value);
            }
            break;
            
        case 0x29: // SH - Store Halfword
            {
                u32 rs = instruction_rs(instruction);
                u32 rt = instruction_rt(instruction);
                s32 offset = instruction_imm_signed(instruction);
                // PSX-SPX: SH rt,offset(rs) -> [GPR[rs]+offset] = GPR[rt] & 0xFFFF
                u32 addr = cpu_get_gpr(cpu, rs) + offset;
                u16 value = cpu_get_gpr(cpu, rt) & 0xFFFF;
                memory_write16(addr, value);
                printf("[CPU] SH r%d, %d(r%d) -> [0x%08X] = 0x%04X\n", rt, offset, rs, addr, value);
            }
            break;
            
        default:
            printf("[CPU] ERROR: Unimplemented opcode 0x%02X at PC=0x%08X\n", opcode, cpu->pc - 4);
            printf("[CPU] Instruction: 0x%08X\n", instruction);
            exit(1);
    }
}

// Register access functions with PSX-SPX compliance
u32 cpu_get_gpr(psx_cpu_t* cpu, u32 reg) {
    if (reg == 0) return 0;  // PSX-SPX: $0 is always zero
    if (reg >= 32) return 0;
    return cpu->gpr[reg];
}

void cpu_set_gpr(psx_cpu_t* cpu, u32 reg, u32 value) {
    if (reg == 0) return;  // PSX-SPX: $0 cannot be written
    if (reg >= 32) return;
    cpu->gpr[reg] = value;
}

u32 cpu_get_cop0(psx_cpu_t* cpu, u32 reg) {
    if (reg >= 64) return 0;
    return cpu->cop0_regs[reg];
}

void cpu_set_cop0(psx_cpu_t* cpu, u32 reg, u32 value) {
    if (reg >= 64) return;
    cpu->cop0_regs[reg] = value;
}

void cpu_exception(psx_cpu_t* cpu, u32 cause) {
    // TODO: Implement full exception handling
    cpu->exception_pending = true;
    cpu->exception_cause = cause;
    printf("[CPU] Exception triggered: cause=0x%08X\n", cause);
}

// Guide.tex: Instruction decoding helper functions
u32 instruction_opcode(u32 instruction) {
    return (instruction >> 26) & 0x3F;
}

u32 instruction_rs(u32 instruction) {
    return (instruction >> 21) & 0x1F;
}

u32 instruction_rt(u32 instruction) {
    return (instruction >> 16) & 0x1F;
}

u32 instruction_rd(u32 instruction) {
    return (instruction >> 11) & 0x1F;
}

u32 instruction_shamt(u32 instruction) {
    return (instruction >> 6) & 0x1F;
}

u32 instruction_funct(u32 instruction) {
    return instruction & 0x3F;
}

u32 instruction_imm(u32 instruction) {
    return instruction & 0xFFFF;
}

s32 instruction_imm_signed(u32 instruction) {
    return (s32)((s16)(instruction & 0xFFFF));
}

u32 instruction_target(u32 instruction) {
    return instruction & 0x3FFFFFF;
}