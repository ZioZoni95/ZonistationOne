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