/**
 * @file zoni_cpu.c
 * @brief MIPS R3000A CPU emulation for ZoniStationOne
 * 
 * This file implements the CPU interpreter following the PCSX-ReARMed structure
 * but with ZoniStationOne naming conventions and modern C practices.
 */

#include "zoni_cpu.h"
#include "zoni_memory.h"
#include <string.h>

// Global CPU instance
static zoni_cpu_config_t g_cpu_config;
static zoni_memory_t* g_memory = NULL;

// Instruction execution function pointers
typedef void (*zoni_instruction_func_t)(zoni_cpu_regs_t* regs, u32 code);

// Basic instruction function arrays (will be populated later)
static zoni_instruction_func_t psx_basic[64];
static zoni_instruction_func_t psx_special[64];

// Load delay handling (following PCSX-ReARMed pattern)
void zoni_cpu_do_load(zoni_cpu_regs_t* cpu, u32 r, u32 val) {
    int sel = cpu->dload_sel ^ 1;
    ZONI_ASSERT(cpu->dload_reg[sel] == 0);
    cpu->dload_reg[sel] = r;
    cpu->dload_val[sel] = r ? val : 0;
    if (cpu->dload_reg[sel ^ 1] == r) {
        cpu->dload_val[sel ^ 1] = cpu->dload_reg[sel ^ 1] = 0;
    }
}

// R-type instruction execution
zoni_error_t zoni_cpu_execute_r_type(zoni_cpu_regs_t* cpu, zoni_instruction_t* instruction, u32 raw_instruction, u8 funct) {
    // Extract fields directly from raw instruction (little-endian)
    u8 rd = (raw_instruction >> 11) & 0x1F;
    u8 rt = (raw_instruction >> 16) & 0x1F;
    u8 rs = (raw_instruction >> 21) & 0x1F;
    u8 shamt = (raw_instruction >> 6) & 0x1F;
    
    switch (funct) {
        case MIPS_FUNC_ADD:
            // ADD rd, rs, rt: rd = rs + rt
            {
                if (rd != 0) {  // $0 is always 0
                    u32 rs_val = cpu->gpr.r[rs];
                    u32 rt_val = cpu->gpr.r[rt];
                    u32 result = rs_val + rt_val;
                    cpu->gpr.r[rd] = result;
                    zoni_log(ZONI_LOG_DEBUG, "ADD $%d = $%d + $%d = 0x%08X", rd, rs, rt, result);
                }
            }
            break;
            
        case MIPS_FUNC_ADDU:
            // ADDU rd, rs, rt: rd = rs + rt (unsigned)
            {
                if (rd != 0) {
                    u32 rs_val = cpu->gpr.r[rs];
                    u32 rt_val = cpu->gpr.r[rt];
                    u32 result = rs_val + rt_val;
                    cpu->gpr.r[rd] = result;
                    zoni_log(ZONI_LOG_DEBUG, "ADDU $%d = $%d + $%d = 0x%08X", rd, rs, rt, result);
                }
            }
            break;
            
        case MIPS_FUNC_SUB:
            // SUB rd, rs, rt: rd = rs - rt
            {
                if (rd != 0) {
                    u32 rs_val = cpu->gpr.r[rs];
                    u32 rt_val = cpu->gpr.r[rt];
                    u32 result = rs_val - rt_val;
                    cpu->gpr.r[rd] = result;
                    zoni_log(ZONI_LOG_DEBUG, "SUB $%d = $%d - $%d = 0x%08X", rd, rs, rt, result);
                }
            }
            break;
            
        case MIPS_FUNC_SUBU:
            // SUBU rd, rs, rt: rd = rs - rt (unsigned)
            {
                if (rd != 0) {
                    u32 rs_val = cpu->gpr.r[rs];
                    u32 rt_val = cpu->gpr.r[rt];
                    u32 result = rs_val - rt_val;
                    cpu->gpr.r[rd] = result;
                    zoni_log(ZONI_LOG_DEBUG, "SUBU $%d = $%d - $%d = 0x%08X", rd, rs, rt, result);
                }
            }
            break;
            
        case MIPS_FUNC_AND:
            // AND rd, rs, rt: rd = rs & rt
            {
                if (rd != 0) {
                    u32 rs_val = cpu->gpr.r[rs];
                    u32 rt_val = cpu->gpr.r[rt];
                    u32 result = rs_val & rt_val;
                    cpu->gpr.r[rd] = result;
                    zoni_log(ZONI_LOG_DEBUG, "AND $%d = $%d & $%d = 0x%08X", rd, rs, rt, result);
                }
            }
            break;
            
        case MIPS_FUNC_OR:
            // OR rd, rs, rt: rd = rs | rt
            {
                if (rd != 0) {
                    u32 rs_val = cpu->gpr.r[rs];
                    u32 rt_val = cpu->gpr.r[rt];
                    u32 result = rs_val | rt_val;
                    cpu->gpr.r[rd] = result;
                    zoni_log(ZONI_LOG_DEBUG, "OR $%d = $%d | $%d = 0x%08X", rd, rs, rt, result);
                }
            }
            break;
            
        case MIPS_FUNC_XOR:
            // XOR rd, rs, rt: rd = rs ^ rt
            {
                if (rd != 0) {
                    u32 rs_val = cpu->gpr.r[rs];
                    u32 rt_val = cpu->gpr.r[rt];
                    u32 result = rs_val ^ rt_val;
                    cpu->gpr.r[rd] = result;
                    zoni_log(ZONI_LOG_DEBUG, "XOR $%d = $%d ^ $%d = 0x%08X", rd, rs, rt, result);
                }
            }
            break;
            
        case MIPS_FUNC_NOR:
            // NOR rd, rs, rt: rd = ~(rs | rt)
            {
                if (rd != 0) {
                    u32 rs_val = cpu->gpr.r[rs];
                    u32 rt_val = cpu->gpr.r[rt];
                    u32 result = ~(rs_val | rt_val);
                    cpu->gpr.r[rd] = result;
                    zoni_log(ZONI_LOG_DEBUG, "NOR $%d = ~($%d | $%d) = 0x%08X", rd, rs, rt, result);
                }
            }
            break;
            
        case MIPS_FUNC_SLL:
            // SLL rd, rt, shamt: rd = rt << shamt
            {
                if (rd != 0) {
                    u32 rt_val = cpu->gpr.r[rt];
                    u32 result = rt_val << shamt;
                    cpu->gpr.r[rd] = result;
                    zoni_log(ZONI_LOG_DEBUG, "SLL $%d = $%d << %d = 0x%08X", rd, rt, shamt, result);
                }
            }
            break;
            
        case MIPS_FUNC_SRL:
            // SRL rd, rt, shamt: rd = rt >> shamt (logical)
            {
                if (rd != 0) {
                    u32 rt_val = cpu->gpr.r[rt];
                    u32 result = rt_val >> shamt;
                    cpu->gpr.r[rd] = result;
                    zoni_log(ZONI_LOG_DEBUG, "SRL $%d = $%d >> %d = 0x%08X", rd, rt, shamt, result);
                }
            }
            break;
            
        case MIPS_FUNC_SRA:
            // SRA rd, rt, shamt: rd = rt >> shamt (arithmetic)
            {
                if (rd != 0) {
                    s32 rt_val = (s32)cpu->gpr.r[rt];
                    s32 result = rt_val >> shamt;
                    cpu->gpr.r[rd] = (u32)result;
                    zoni_log(ZONI_LOG_DEBUG, "SRA $%d = $%d >> %d = 0x%08X", rd, rt, shamt, result);
                }
            }
            break;
            
        case MIPS_FUNC_JR:
            // JR rs: PC = rs
            {
                u32 rs_val = cpu->gpr.r[rs];
                cpu->pc = rs_val;
                zoni_log(ZONI_LOG_DEBUG, "JR $%d -> 0x%08X", rs, rs_val);
            }
            break;
            
        case MIPS_FUNC_SYSCALL:
            // SYSCALL: System call instruction
            // This triggers a system call exception for BIOS communication
            return zoni_cpu_execute_syscall(cpu, instruction);
            
        case MIPS_FUNC_MULT:
            // MULT rs, rt: hi,lo = rs * rt (signed)
            {
                s32 rs_val = (s32)cpu->gpr.r[rs];
                s32 rt_val = (s32)cpu->gpr.r[rt];
                s64 result = (s64)rs_val * (s64)rt_val;
                
                // Store high 32 bits in HI, low 32 bits in LO
                cpu->gpr.r[32] = (u32)(result >> 32);  // HI register
                cpu->gpr.r[33] = (u32)(result & 0xFFFFFFFF);  // LO register
                
                zoni_log(ZONI_LOG_DEBUG, "MULT $%d * $%d = %d * %d = %lld (HI=0x%08X, LO=0x%08X)", 
                         rs, rt, rs_val, rt_val, result, cpu->gpr.r[32], cpu->gpr.r[33]);
            }
            break;
            
        case MIPS_FUNC_MULTU:
            // MULTU rs, rt: hi,lo = rs * rt (unsigned)
            {
                u32 rs_val = cpu->gpr.r[rs];
                u32 rt_val = cpu->gpr.r[rt];
                u64 result = (u64)rs_val * (u64)rt_val;
                
                // Store high 32 bits in HI, low 32 bits in LO
                cpu->gpr.r[32] = (u32)(result >> 32);  // HI register
                cpu->gpr.r[33] = (u32)(result & 0xFFFFFFFF);  // LO register
                
                zoni_log(ZONI_LOG_DEBUG, "MULTU $%d * $%d = %u * %u = %llu (HI=0x%08X, LO=0x%08X)", 
                         rs, rt, rs_val, rt_val, result, cpu->gpr.r[32], cpu->gpr.r[33]);
            }
            break;
            
        case MIPS_FUNC_MFHI:
            // MFHI rd: rd = hi
            {
                if (rd != 0) {
                    u32 hi_val = cpu->gpr.r[32];  // HI register
                    cpu->gpr.r[rd] = hi_val;
                    zoni_log(ZONI_LOG_DEBUG, "MFHI $%d = HI = 0x%08X", rd, hi_val);
                }
            }
            break;
            
        case MIPS_FUNC_MFLO:
            // MFLO rd: rd = lo
            {
                if (rd != 0) {
                    u32 lo_val = cpu->gpr.r[33];  // LO register
                    cpu->gpr.r[rd] = lo_val;
                    zoni_log(ZONI_LOG_DEBUG, "MFLO $%d = LO = 0x%08X", rd, lo_val);
                }
            }
            break;
            
        case MIPS_FUNC_MTHI:
            // MTHI rs: hi = rs
            {
                u32 rs_val = cpu->gpr.r[rs];
                cpu->gpr.r[32] = rs_val;  // HI register
                zoni_log(ZONI_LOG_DEBUG, "MTHI HI = $%d = 0x%08X", rs, rs_val);
            }
            break;
            
        case MIPS_FUNC_MTLO:
            // MTLO rs: lo = rs
            {
                u32 rs_val = cpu->gpr.r[rs];
                cpu->gpr.r[33] = rs_val;  // LO register
                zoni_log(ZONI_LOG_DEBUG, "MTLO LO = $%d = 0x%08X", rs, rs_val);
            }
            break;
            
        case MIPS_FUNC_SLT:
            // SLT rd, rs, rt: rd = (rs < rt) ? 1 : 0 (signed)
            {
                if (rd != 0) {
                    s32 rs_val = (s32)cpu->gpr.r[rs];
                    s32 rt_val = (s32)cpu->gpr.r[rt];
                    u32 result = (rs_val < rt_val) ? 1 : 0;
                    cpu->gpr.r[rd] = result;
                    zoni_log(ZONI_LOG_DEBUG, "SLT $%d = ($%d < $%d) ? 1 : 0 = %d", 
                             rd, rs, rt, result);
                }
            }
            break;
            
        case MIPS_FUNC_SLTU:
            // SLTU rd, rs, rt: rd = (rs < rt) ? 1 : 0 (unsigned)
            {
                if (rd != 0) {
                    u32 rs_val = cpu->gpr.r[rs];
                    u32 rt_val = cpu->gpr.r[rt];
                    u32 result = (rs_val < rt_val) ? 1 : 0;
                    cpu->gpr.r[rd] = result;
                    zoni_log(ZONI_LOG_DEBUG, "SLTU $%d = ($%d < $%d) ? 1 : 0 = %d", 
                             rd, rs, rt, result);
                }
            }
            break;
            
        case MIPS_FUNC_BREAK:
            // BREAK: Breakpoint instruction
            // This triggers a breakpoint exception for debugging
            zoni_log(ZONI_LOG_DEBUG, "BREAK: Triggering breakpoint exception");
            zoni_cpu_trigger_exception(cpu, ZONI_EXCEPTION_BP, cpu->pc);
            return ZONI_SUCCESS;
            
        default:
            zoni_log(ZONI_LOG_WARNING, "Unknown R-type function: 0x%02X", funct);
            return ZONI_ERROR_NOT_IMPLEMENTED;
    }
    
    return ZONI_SUCCESS;
}

// ADDI rt, rs, immediate: rt = rs + immediate (signed)
zoni_error_t zoni_cpu_execute_addi(zoni_cpu_regs_t* cpu, zoni_instruction_t* instruction) {
    // Extract fields manually from raw instruction (little-endian)
    u8 rt = (instruction->raw >> 16) & 0x1F;
    u8 rs = (instruction->raw >> 21) & 0x1F;
    s16 immediate = (s16)(instruction->raw & 0xFFFF);
    
    if (rt != 0) {  // $0 is always 0
        u32 rs_val = cpu->gpr.r[rs];
        s32 result = (s32)rs_val + immediate;
        cpu->gpr.r[rt] = (u32)result;
        zoni_log(ZONI_LOG_DEBUG, "ADDI $%d = $%d + %d = 0x%08X", 
                 rt, rs, immediate, result);
    }
    return ZONI_SUCCESS;
}

// ADDIU rt, rs, immediate: rt = rs + immediate (unsigned)
zoni_error_t zoni_cpu_execute_addiu(zoni_cpu_regs_t* cpu, zoni_instruction_t* instruction) {
    zoni_log(ZONI_LOG_DEBUG, "ADDIU function called with instruction 0x%08X", instruction->raw);
    
    // Extract fields directly from raw instruction (little-endian)
    u8 rt = (instruction->raw >> 16) & 0x1F;
    u8 rs = (instruction->raw >> 21) & 0x1F;
    u16 immediate = instruction->raw & 0xFFFF;
    
    zoni_log(ZONI_LOG_DEBUG, "ADDIU extracted: rt=%d, rs=%d, immediate=0x%04X", rt, rs, immediate);
    
    if (rt != 0) {
        u32 rs_val = cpu->gpr.r[rs];
        u32 result = rs_val + immediate;
        cpu->gpr.r[rt] = result;
        zoni_log(ZONI_LOG_DEBUG, "ADDIU $%d = $%d (0x%08X) + %u = 0x%08X", 
                 rt, rs, rs_val, immediate, result);
    }
    return ZONI_SUCCESS;
}

void zoni_cpu_dload_rt(zoni_cpu_regs_t* cpu, u32 r, u32 val) {
    int sel = cpu->dload_sel;
    if (cpu->dload_reg[sel] == r) {
        cpu->dload_val[sel] = cpu->dload_reg[sel] = 0;
    }
    cpu->gpr.r[r] = r ? val : 0;
}

void zoni_cpu_dload_step(zoni_cpu_regs_t* cpu) {
    int sel = cpu->dload_sel ^ 1;  // Process the slot that was filled by the previous instruction
    if (cpu->dload_reg[sel] != 0) {
        cpu->gpr.r[cpu->dload_reg[sel]] = cpu->dload_val[sel];
    }
    cpu->dload_val[sel] = cpu->dload_reg[sel] = 0;
    cpu->dload_sel ^= 1;
    ZONI_ASSERT(cpu->gpr.r[0] == 0);
}

void zoni_cpu_dload_flush(zoni_cpu_regs_t* cpu) {
    cpu->gpr.r[cpu->dload_reg[0]] = cpu->dload_val[0];
    cpu->gpr.r[cpu->dload_reg[1]] = cpu->dload_val[1];
    cpu->dload_val[0] = cpu->dload_val[1] = 0;
    cpu->dload_reg[0] = cpu->dload_reg[1] = 0;
    ZONI_ASSERT(cpu->gpr.r[0] == 0);
}

void zoni_cpu_dload_clear(zoni_cpu_regs_t* cpu) {
    cpu->dload_val[0] = cpu->dload_val[1] = 0;
    cpu->dload_reg[0] = cpu->dload_reg[1] = 0;
    cpu->dload_sel = 0;
}

// Exception handling
// This function handles CPU exceptions by setting up the exception state
// and jumping to the appropriate exception vector
// Exceptions are the primary way the PlayStation BIOS communicates with the emulator
static void zoni_cpu_exception(zoni_cpu_regs_t* cpu, zoni_exception_t cause, u32 epc) {
    zoni_log(ZONI_LOG_DEBUG, "CPU Exception: %d at PC=0x%08X", cause, epc);
    
    // Store the exception cause and EPC (Exception Program Counter)
    // EPC contains the address of the instruction that caused the exception
    cpu->cp0.n.Cause = cause;
    cpu->cp0.n.EPC = epc;
    
    // Set exception vector based on cause
    // PlayStation BIOS uses specific exception vectors for different types of exceptions
    switch (cause) {
        case ZONI_EXCEPTION_INT:
            // Interrupt exception - hardware interrupts
            cpu->pc = 0x80000080; // Interrupt vector
            break;
        case ZONI_EXCEPTION_SYSCALL:
            // System call exception - BIOS system calls
            // This is the most important exception for BIOS communication
            cpu->pc = 0x80000040; // Syscall vector
            break;
        case ZONI_EXCEPTION_BP:
            // Breakpoint exception - debugging
            cpu->pc = 0x80000048; // Breakpoint vector
            break;
        case ZONI_EXCEPTION_ADEL:
            // Address error on load - memory access violation
            cpu->pc = 0x80000080; // General exception vector
            break;
        case ZONI_EXCEPTION_ADES:
            // Address error on store - memory access violation
            cpu->pc = 0x80000080; // General exception vector
            break;
        case ZONI_EXCEPTION_RI:
            // Reserved instruction - unknown instruction
            cpu->pc = 0x80000080; // General exception vector
            break;
        default:
            // General exception - catch-all for other exceptions
            cpu->pc = 0x80000080; // General exception vector
            break;
    }
    
    // Set exception mode in Status Register
    // EXL (Exception Level) bit indicates we're in exception mode
    // This prevents nested exceptions and changes CPU behavior
    cpu->cp0.n.SR |= 0x00000002; // EXL bit
}

// Trigger a CPU exception
// This function is called by instructions that need to generate exceptions
// Examples: SYSCALL, BREAK, memory access violations, etc.
// The BIOS will handle these exceptions and perform the appropriate actions
void zoni_cpu_trigger_exception(zoni_cpu_regs_t* cpu, zoni_exception_t cause, u32 epc) {
    // Log the exception for debugging
    zoni_log(ZONI_LOG_DEBUG, "Triggering exception: %d at PC=0x%08X", cause, epc);
    
    // Call the exception handler to set up the exception state
    // This will set the EPC, Cause register, and jump to the exception vector
    zoni_cpu_exception(cpu, cause, epc);
}

void zoni_cpu_handle_interrupt(zoni_cpu_regs_t* cpu, u32 interrupt) {
    zoni_log(ZONI_LOG_DEBUG, "CPU Interrupt: 0x%08X", interrupt);
    cpu->interrupt |= interrupt;
}

// Memory access functions (delegate to memory system)
zoni_error_t zoni_cpu_read8(zoni_cpu_regs_t* cpu, u32 address, u8* value) {
    ZONI_UNUSED(cpu);
    if (!g_memory) return ZONI_ERROR_INITIALIZATION_FAILED;
    return zoni_memory_read8(g_memory, address, value);
}

zoni_error_t zoni_cpu_read16(zoni_cpu_regs_t* cpu, u32 address, u16* value) {
    ZONI_UNUSED(cpu);
    if (!g_memory) return ZONI_ERROR_INITIALIZATION_FAILED;
    return zoni_memory_read16(g_memory, address, value);
}

zoni_error_t zoni_cpu_read32(zoni_cpu_regs_t* cpu, u32 address, u32* value) {
    ZONI_UNUSED(cpu);
    if (!g_memory) return ZONI_ERROR_INITIALIZATION_FAILED;
    return zoni_memory_read32(g_memory, address, value);
}

zoni_error_t zoni_cpu_write8(zoni_cpu_regs_t* cpu, u32 address, u8 value) {
    ZONI_UNUSED(cpu);
    if (!g_memory) return ZONI_ERROR_INITIALIZATION_FAILED;
    return zoni_memory_write8(g_memory, address, value);
}

zoni_error_t zoni_cpu_write16(zoni_cpu_regs_t* cpu, u32 address, u16 value) {
    ZONI_UNUSED(cpu);
    if (!g_memory) return ZONI_ERROR_INITIALIZATION_FAILED;
    return zoni_memory_write16(g_memory, address, value);
}

zoni_error_t zoni_cpu_write32(zoni_cpu_regs_t* cpu, u32 address, u32 value) {
    ZONI_UNUSED(cpu);
    if (!g_memory) return ZONI_ERROR_INITIALIZATION_FAILED;
    return zoni_memory_write32(g_memory, address, value);
}

// Register access functions
u32 zoni_cpu_get_register(zoni_cpu_regs_t* cpu, u8 reg) {
    if (reg >= 34) return 0;
    return cpu->gpr.r[reg];
}

void zoni_cpu_set_register(zoni_cpu_regs_t* cpu, u8 reg, u32 value) {
    if (reg >= 34) return;
    if (reg == 0) return; // r0 is always zero
    cpu->gpr.r[reg] = value;
}

// CPU initialization
zoni_error_t zoni_cpu_init(zoni_cpu_regs_t* cpu, const zoni_cpu_config_t* config) {
    if (!cpu || !config) {
        return ZONI_ERROR_INVALID_PARAMETER;
    }
    
    // Store global references
    g_cpu_config = *config;
    
    // Initialize CPU registers
    memset(cpu, 0, sizeof(zoni_cpu_regs_t));
    
    // Set initial PC to BIOS entry point
    cpu->pc = 0xBFC00000;
    
    // Initialize coprocessor 0 registers
    cpu->cp0.n.SR = 0x00000000;  // Status Register
    cpu->cp0.n.Cause = 0x00000000; // Cause Register
    cpu->cp0.n.EPC = 0x00000000;   // Exception Program Counter
    cpu->cp0.n.PRid = 0x00000002;  // Processor ID (R3000A)
    
    // Initialize load delay slots
    zoni_cpu_dload_clear(cpu);
    
    // Initialize instruction function arrays (will be populated later)
    memset(psx_basic, 0, sizeof(psx_basic));
    memset(psx_special, 0, sizeof(psx_special));
    
    zoni_log(ZONI_LOG_INFO, "CPU initialized successfully");
    return ZONI_SUCCESS;
}

void zoni_cpu_reset(zoni_cpu_regs_t* cpu) {
    if (!cpu) return;
    
    // Reset CPU state
    cpu->pc = 0xBFC00000;
    cpu->cycle = 0;
    cpu->interrupt = 0;
    cpu->stop = 0;
    cpu->branch_seen = 0;
    cpu->branching = 0;
    
    // Reset coprocessor 0 registers
    cpu->cp0.n.SR = 0x00000000;
    cpu->cp0.n.Cause = 0x00000000;
    cpu->cp0.n.EPC = 0x00000000;
    
    // Clear load delay slots
    zoni_cpu_dload_clear(cpu);
    
    // Reset performance counters
    cpu->instruction_count = 0;
    cpu->cycle_count = 0;
    
    zoni_log(ZONI_LOG_INFO, "CPU reset");
}

void zoni_cpu_shutdown(zoni_cpu_regs_t* cpu) {
    ZONI_UNUSED(cpu);
    zoni_log(ZONI_LOG_INFO, "CPU shutdown");
}

// CPU execution (placeholder for now)
zoni_error_t zoni_cpu_execute(zoni_cpu_regs_t* cpu, u32 cycles) {
    ZONI_UNUSED(cpu);
    ZONI_UNUSED(cycles);
    
    // TODO: Implement instruction execution
    zoni_log(ZONI_LOG_DEBUG, "CPU execute: %u cycles", cycles);
    return ZONI_ERROR_NOT_IMPLEMENTED;
}

zoni_error_t zoni_cpu_step(zoni_cpu_regs_t* cpu) {
    if (!cpu || !g_memory) {
        return ZONI_ERROR_INVALID_PARAMETER;
    }
    
    // Fetch instruction at current PC
    zoni_instruction_t instruction;
    zoni_error_t result = zoni_cpu_fetch_instruction(cpu, &instruction);
    if (result != ZONI_SUCCESS) {
        zoni_log(ZONI_LOG_ERROR, "Failed to fetch instruction at PC=0x%08X", cpu->pc);
        return result;
    }
    
    // Execute the instruction
    result = zoni_cpu_execute_instruction(cpu, &instruction);
    if (result != ZONI_SUCCESS) {
        zoni_log(ZONI_LOG_ERROR, "Failed to execute instruction at PC=0x%08X", cpu->pc);
        return result;
    }
    
    // Increment PC (unless instruction was a branch/jump)
    // Note: Branch and jump instructions update PC themselves
    // Extract opcode and function code directly from raw instruction
    // This matches the execution engine approach for consistency
    u8 opcode = (instruction.raw >> 26) & 0x3F;
    u8 funct = instruction.raw & 0x3F;
    
    // Check if this is a branch/jump instruction that updates PC
    bool is_branch_or_jump = false;
    
    if (opcode == MIPS_OP_BEQ || opcode == MIPS_OP_BNE || 
        opcode == MIPS_OP_J || opcode == MIPS_OP_JAL) {
        is_branch_or_jump = true;
    } else if (opcode == MIPS_OP_SPECIAL && funct == MIPS_FUNC_JR) {
        is_branch_or_jump = true;
    }
    
    // Only increment PC if it's not a branch/jump instruction
    if (!is_branch_or_jump) {
        cpu->pc += 4;
        zoni_log(ZONI_LOG_DEBUG, "PC incremented to 0x%08X", cpu->pc);
    } else {
        zoni_log(ZONI_LOG_DEBUG, "PC not incremented (branch/jump instruction)");
    }
    
    // Increment cycle count
    cpu->cycle++;
    
    return ZONI_SUCCESS;
}

// Debug functions
void zoni_cpu_dump_registers(zoni_cpu_regs_t* cpu) {
    if (!cpu) return;
    
    zoni_log(ZONI_LOG_INFO, "CPU Registers:");
    zoni_log(ZONI_LOG_INFO, "PC: 0x%08X", cpu->pc);
    zoni_log(ZONI_LOG_INFO, "Code: 0x%08X", cpu->code);
    zoni_log(ZONI_LOG_INFO, "Cycle: %u", cpu->cycle);
    
    // Dump GPR registers (accounting for load delay slots)
    for (int i = 0; i < 32; i += 4) {
        u32 r0 = cpu->gpr.r[i];
        u32 r1 = cpu->gpr.r[i+1];
        u32 r2 = cpu->gpr.r[i+2];
        u32 r3 = cpu->gpr.r[i+3];
        
        // Check if any of these registers have pending loads
        if (cpu->dload_reg[0] == i) r0 = cpu->dload_val[0];
        if (cpu->dload_reg[0] == i+1) r1 = cpu->dload_val[0];
        if (cpu->dload_reg[0] == i+2) r2 = cpu->dload_val[0];
        if (cpu->dload_reg[0] == i+3) r3 = cpu->dload_val[0];
        if (cpu->dload_reg[1] == i) r0 = cpu->dload_val[1];
        if (cpu->dload_reg[1] == i+1) r1 = cpu->dload_val[1];
        if (cpu->dload_reg[1] == i+2) r2 = cpu->dload_val[1];
        if (cpu->dload_reg[1] == i+3) r3 = cpu->dload_val[1];
        
        zoni_log(ZONI_LOG_INFO, "R%02d: 0x%08X  R%02d: 0x%08X  R%02d: 0x%08X  R%02d: 0x%08X",
                 i, r0, i+1, r1, i+2, r2, i+3, r3);
    }
    
    // Dump special registers
    zoni_log(ZONI_LOG_INFO, "LO: 0x%08X  HI: 0x%08X", cpu->gpr.r[32], cpu->gpr.r[33]);
    
    // Dump CP0 registers
    zoni_log(ZONI_LOG_INFO, "SR: 0x%08X  Cause: 0x%08X  EPC: 0x%08X",
             cpu->cp0.n.SR, cpu->cp0.n.Cause, cpu->cp0.n.EPC);
    
    // Dump load delay slot info for debugging
    if (cpu->dload_reg[0] != 0 || cpu->dload_reg[1] != 0) {
        zoni_log(ZONI_LOG_INFO, "Load Delay: Slot0=$%d=0x%08X, Slot1=$%d=0x%08X",
                 cpu->dload_reg[0], cpu->dload_val[0], cpu->dload_reg[1], cpu->dload_val[1]);
    }
}

void zoni_cpu_disassemble_instruction(zoni_cpu_regs_t* cpu, u32 address, char* buffer, size_t buffer_size) {
    ZONI_UNUSED(cpu);
    ZONI_UNUSED(address);
    
    // TODO: Implement instruction disassembly
    snprintf(buffer, buffer_size, "NOP");
}

// Set memory reference (called from emulator)
void zoni_cpu_set_memory(zoni_memory_t* memory) {
    g_memory = memory;
}

// Instruction fetching and execution
zoni_error_t zoni_cpu_fetch_instruction(zoni_cpu_regs_t* cpu, zoni_instruction_t* instruction) {
    if (!cpu || !instruction || !g_memory) {
        return ZONI_ERROR_INVALID_PARAMETER;
    }
    
    // Fetch 32-bit instruction from memory at current PC
    u32 instruction_word;
    zoni_error_t result = zoni_memory_read32(g_memory, cpu->pc, &instruction_word);
    if (result != ZONI_SUCCESS) {
        zoni_log(ZONI_LOG_ERROR, "Failed to fetch instruction at PC=0x%08X", cpu->pc);
        return result;
    }
    
    // Store the raw instruction in the union for easy access
    instruction->raw = instruction_word;
    
    // Update CPU state
    cpu->code = instruction_word;  // Store current instruction
    cpu->instruction_count++;      // Increment instruction counter
    
    zoni_log(ZONI_LOG_DEBUG, "Fetched instruction 0x%08X at PC=0x%08X", instruction_word, cpu->pc);
    
    return ZONI_SUCCESS;
}

zoni_error_t zoni_cpu_decode_instruction(zoni_instruction_t* instruction, char* disasm, size_t disasm_size) {
    if (!instruction || !disasm) {
        return ZONI_ERROR_INVALID_PARAMETER;
    }
    
    // Convert little-endian instruction to big-endian for proper decoding
    u32 big_endian_instruction = zoni_instruction_to_big_endian(instruction->raw);
    
    // Extract opcode and function code directly from little-endian instruction
    // MIPS instructions are stored in big-endian format in memory, so we need to convert
    u8 opcode = (instruction->raw >> 26) & 0x3F;
    u8 funct = instruction->raw & 0x3F;
    
        // Debug: Log the bit extraction for troubleshooting
    zoni_log(ZONI_LOG_DEBUG, "Decode: raw=0x%08X, opcode=0x%02X", instruction->raw, opcode);
    
    // Basic instruction disassembly based on opcode
    switch (opcode) {
        case MIPS_OP_SPECIAL:
            // R-type instruction, check function code
            switch (funct) {
                case MIPS_FUNC_ADD:
                    {
                        u8 rd = (instruction->raw >> 11) & 0x1F;
                        u8 rs = (instruction->raw >> 21) & 0x1F;
                        u8 rt = (instruction->raw >> 16) & 0x1F;
                        snprintf(disasm, disasm_size, "ADD $%d, $%d, $%d", rd, rs, rt);
                    }
                    break;
                case MIPS_FUNC_ADDU:
                    {
                        u8 rd = (instruction->raw >> 11) & 0x1F;
                        u8 rs = (instruction->raw >> 21) & 0x1F;
                        u8 rt = (instruction->raw >> 16) & 0x1F;
                        snprintf(disasm, disasm_size, "ADDU $%d, $%d, $%d", rd, rs, rt);
                    }
                    break;
                case MIPS_FUNC_SUB:
                    {
                        u8 rd = (instruction->raw >> 11) & 0x1F;
                        u8 rs = (instruction->raw >> 21) & 0x1F;
                        u8 rt = (instruction->raw >> 16) & 0x1F;
                        snprintf(disasm, disasm_size, "SUB $%d, $%d, $%d", rd, rs, rt);
                    }
                    break;
                case MIPS_FUNC_SUBU:
                    {
                        u8 rd = (instruction->raw >> 11) & 0x1F;
                        u8 rs = (instruction->raw >> 21) & 0x1F;
                        u8 rt = (instruction->raw >> 16) & 0x1F;
                        snprintf(disasm, disasm_size, "SUBU $%d, $%d, $%d", rd, rs, rt);
                    }
                    break;
                case MIPS_FUNC_AND:
                    {
                        u8 rd = (instruction->raw >> 11) & 0x1F;
                        u8 rs = (instruction->raw >> 21) & 0x1F;
                        u8 rt = (instruction->raw >> 16) & 0x1F;
                        snprintf(disasm, disasm_size, "AND $%d, $%d, $%d", rd, rs, rt);
                    }
                    break;
                case MIPS_FUNC_OR:
                    {
                        u8 rd = (instruction->raw >> 11) & 0x1F;
                        u8 rs = (instruction->raw >> 21) & 0x1F;
                        u8 rt = (instruction->raw >> 16) & 0x1F;
                        snprintf(disasm, disasm_size, "OR $%d, $%d, $%d", rd, rs, rt);
                    }
                    break;
                case MIPS_FUNC_XOR:
                    {
                        u8 rd = (instruction->raw >> 11) & 0x1F;
                        u8 rs = (instruction->raw >> 21) & 0x1F;
                        u8 rt = (instruction->raw >> 16) & 0x1F;
                        snprintf(disasm, disasm_size, "XOR $%d, $%d, $%d", rd, rs, rt);
                    }
                    break;
                case MIPS_FUNC_NOR:
                    {
                        u8 rd = (instruction->raw >> 11) & 0x1F;
                        u8 rs = (instruction->raw >> 21) & 0x1F;
                        u8 rt = (instruction->raw >> 16) & 0x1F;
                        snprintf(disasm, disasm_size, "NOR $%d, $%d, $%d", rd, rs, rt);
                    }
                    break;
                case MIPS_FUNC_SLL:
                    {
                        u8 rd = (instruction->raw >> 11) & 0x1F;
                        u8 rt = (instruction->raw >> 16) & 0x1F;
                        u8 shamt = (instruction->raw >> 6) & 0x1F;
                        snprintf(disasm, disasm_size, "SLL $%d, $%d, %d", rd, rt, shamt);
                    }
                    break;
                case MIPS_FUNC_SRL:
                    {
                        u8 rd = (instruction->raw >> 11) & 0x1F;
                        u8 rt = (instruction->raw >> 16) & 0x1F;
                        u8 shamt = (instruction->raw >> 6) & 0x1F;
                        snprintf(disasm, disasm_size, "SRL $%d, $%d, %d", rd, rt, shamt);
                    }
                    break;
                case MIPS_FUNC_SRA:
                    {
                        u8 rd = (instruction->raw >> 11) & 0x1F;
                        u8 rt = (instruction->raw >> 16) & 0x1F;
                        u8 shamt = (instruction->raw >> 6) & 0x1F;
                        snprintf(disasm, disasm_size, "SRA $%d, $%d, %d", rd, rt, shamt);
                    }
                    break;
                case MIPS_FUNC_JR:
                    {
                        u8 rs = (instruction->raw >> 21) & 0x1F;
                        snprintf(disasm, disasm_size, "JR $%d", rs);
                    }
                    break;
                case MIPS_FUNC_JALR:
                    {
                        u8 rd = (instruction->raw >> 11) & 0x1F;
                        u8 rs = (instruction->raw >> 21) & 0x1F;
                        snprintf(disasm, disasm_size, "JALR $%d, $%d", rd, rs);
                    }
                    break;
                case MIPS_FUNC_SYSCALL:
                    snprintf(disasm, disasm_size, "SYSCALL");
                    break;
                case MIPS_FUNC_BREAK:
                    snprintf(disasm, disasm_size, "BREAK");
                    break;
                case MIPS_FUNC_MFHI:
                    {
                        u8 rd = (big_endian_instruction >> 11) & 0x1F;
                        snprintf(disasm, disasm_size, "MFHI $%d", rd);
                    }
                    break;
                case MIPS_FUNC_MFLO:
                    {
                        u8 rd = (big_endian_instruction >> 11) & 0x1F;
                        snprintf(disasm, disasm_size, "MFLO $%d", rd);
                    }
                    break;
                case MIPS_FUNC_MULT:
                    {
                        u8 rs = (big_endian_instruction >> 21) & 0x1F;
                        u8 rt = (big_endian_instruction >> 16) & 0x1F;
                        snprintf(disasm, disasm_size, "MULT $%d, $%d", rs, rt);
                    }
                    break;
                case MIPS_FUNC_DIV:
                    {
                        u8 rs = (big_endian_instruction >> 21) & 0x1F;
                        u8 rt = (big_endian_instruction >> 16) & 0x1F;
                        snprintf(disasm, disasm_size, "DIV $%d, $%d", rs, rt);
                    }
                    break;
                default:
                    snprintf(disasm, disasm_size, "UNKNOWN_R(0x%02X)", funct);
                    break;
            }
            break;
            
        case MIPS_OP_ADDI:
            {
                u8 rt = (instruction->raw >> 16) & 0x1F;
                u8 rs = (instruction->raw >> 21) & 0x1F;
                s16 immediate = (s16)(instruction->raw & 0xFFFF);
                snprintf(disasm, disasm_size, "ADDI $%d, $%d, %d", rt, rs, immediate);
            }
            break;
        case MIPS_OP_ADDIU:
            {
                u8 rt = (instruction->raw >> 16) & 0x1F;
                u8 rs = (instruction->raw >> 21) & 0x1F;
                s16 immediate = (s16)(instruction->raw & 0xFFFF);
                snprintf(disasm, disasm_size, "ADDIU $%d, $%d, %d", rt, rs, immediate);
            }
            break;
        case MIPS_OP_ANDI:
            {
                u8 rt = (instruction->raw >> 16) & 0x1F;
                u8 rs = (instruction->raw >> 21) & 0x1F;
                u16 immediate = instruction->raw & 0xFFFF;
                snprintf(disasm, disasm_size, "ANDI $%d, $%d, 0x%04X", rt, rs, immediate);
            }
            break;
        case MIPS_OP_ORI:
            {
                u8 rt = (instruction->raw >> 16) & 0x1F;
                u8 rs = (instruction->raw >> 21) & 0x1F;
                u16 immediate = instruction->raw & 0xFFFF;
                snprintf(disasm, disasm_size, "ORI $%d, $%d, 0x%04X", rt, rs, immediate);
            }
            break;
        case MIPS_OP_XORI:
            {
                u8 rt = (instruction->raw >> 16) & 0x1F;
                u8 rs = (instruction->raw >> 21) & 0x1F;
                u16 immediate = instruction->raw & 0xFFFF;
                snprintf(disasm, disasm_size, "XORI $%d, $%d, 0x%04X", rt, rs, immediate);
            }
            break;
        case MIPS_OP_LUI:
            {
                u8 rt = (instruction->raw >> 16) & 0x1F;
                u16 immediate = instruction->raw & 0xFFFF;
                snprintf(disasm, disasm_size, "LUI $%d, 0x%04X", rt, immediate);
            }
            break;
        case MIPS_OP_BEQ:
            {
                u8 rs = (instruction->raw >> 21) & 0x1F;
                u8 rt = (instruction->raw >> 16) & 0x1F;
                s16 immediate = (s16)(instruction->raw & 0xFFFF);
                snprintf(disasm, disasm_size, "BEQ $%d, $%d, %d", rs, rt, immediate);
            }
            break;
        case MIPS_OP_BNE:
            {
                u8 rs = (instruction->raw >> 21) & 0x1F;
                u8 rt = (instruction->raw >> 16) & 0x1F;
                s16 immediate = (s16)(instruction->raw & 0xFFFF);
                snprintf(disasm, disasm_size, "BNE $%d, $%d, %d", rs, rt, immediate);
            }
            break;
        case MIPS_OP_LW:
            {
                u8 rt = (instruction->raw >> 16) & 0x1F;
                u8 rs = (instruction->raw >> 21) & 0x1F;
                s16 immediate = (s16)(instruction->raw & 0xFFFF);
                snprintf(disasm, disasm_size, "LW $%d, %d($%d)", rt, immediate, rs);
            }
            break;
        case MIPS_OP_SW:
            {
                u8 rt = (instruction->raw >> 16) & 0x1F;
                u8 rs = (instruction->raw >> 21) & 0x1F;
                s16 immediate = (s16)(instruction->raw & 0xFFFF);
                snprintf(disasm, disasm_size, "SW $%d, %d($%d)", rt, immediate, rs);
            }
            break;
        case MIPS_OP_LB:
            {
                u8 rt = (instruction->raw >> 16) & 0x1F;
                u8 rs = (instruction->raw >> 21) & 0x1F;
                s16 immediate = (s16)(instruction->raw & 0xFFFF);
                snprintf(disasm, disasm_size, "LB $%d, %d($%d)", rt, immediate, rs);
            }
            break;
        case MIPS_OP_SB:
            {
                u8 rt = (instruction->raw >> 16) & 0x1F;
                u8 rs = (instruction->raw >> 21) & 0x1F;
                s16 immediate = (s16)(instruction->raw & 0xFFFF);
                snprintf(disasm, disasm_size, "SB $%d, %d($%d)", rt, immediate, rs);
            }
            break;
        case MIPS_OP_J:
            {
                u32 address = instruction->raw & 0x3FFFFFF;
                snprintf(disasm, disasm_size, "J 0x%08X", (address << 2) & 0x0FFFFFFF);
            }
            break;
        case MIPS_OP_JAL:
            {
                u32 address = instruction->raw & 0x3FFFFFF;
                snprintf(disasm, disasm_size, "JAL 0x%08X", (address << 2) & 0x0FFFFFFF);
            }
            break;
        default:
            snprintf(disasm, disasm_size, "UNKNOWN(0x%02X)", opcode);
            break;
    }
    
    return ZONI_SUCCESS;
}

// Instruction execution engine
zoni_error_t zoni_cpu_execute_instruction(zoni_cpu_regs_t* cpu, zoni_instruction_t* instruction) {
    if (!cpu || !instruction || !g_memory) {
        return ZONI_ERROR_INVALID_PARAMETER;
    }
    
    // Extract opcode directly from raw instruction (little-endian)
    // The bit fields in instruction structure are already in little-endian order
    u8 opcode = (instruction->raw >> 26) & 0x3F;
    u8 funct = instruction->raw & 0x3F;
    
    // Log instruction execution for development (every 1000th instruction)
    static int instruction_count = 0;
    instruction_count++;
    if (instruction_count % 1000 == 0) {
        char disasm[128];
        zoni_cpu_decode_instruction(instruction, disasm, sizeof(disasm));
        zoni_log(ZONI_LOG_DEBUG, "Instruction %d: PC=0x%08X, %s", instruction_count, cpu->pc, disasm);
    }
    
    // Execute instruction based on opcode
    zoni_error_t result;
    switch (opcode) {
        case MIPS_OP_SPECIAL:
            // R-type instruction, check function code
            result = zoni_cpu_execute_r_type(cpu, instruction, instruction->raw, funct);
            break;
            
        case MIPS_OP_ADDI:
            result = zoni_cpu_execute_addi(cpu, instruction);
            break;
            
        case MIPS_OP_ADDIU:
            result = zoni_cpu_execute_addiu(cpu, instruction);
            break;
            
        case MIPS_OP_ANDI:
            result = zoni_cpu_execute_andi(cpu, instruction);
            break;
            
        case MIPS_OP_ORI:
            result = zoni_cpu_execute_ori(cpu, instruction);
            break;
            
        case MIPS_OP_XORI:
            result = zoni_cpu_execute_xori(cpu, instruction);
            break;
            
        case MIPS_OP_LUI:
            result = zoni_cpu_execute_lui(cpu, instruction);
            break;
            
        case MIPS_OP_BEQ:
            result = zoni_cpu_execute_beq(cpu, instruction);
            break;
            
        case MIPS_OP_BNE:
            result = zoni_cpu_execute_bne(cpu, instruction);
            break;
            
        case MIPS_OP_LW:
            result = zoni_cpu_execute_lw(cpu, instruction);
            break;
            
        case MIPS_OP_SW:
            result = zoni_cpu_execute_sw(cpu, instruction);
            break;
            
        case MIPS_OP_LB:
            result = zoni_cpu_execute_lb(cpu, instruction);
            break;
            
        case MIPS_OP_SB:
            result = zoni_cpu_execute_sb(cpu, instruction);
            break;
            
        case MIPS_OP_J:
            result = zoni_cpu_execute_j(cpu, instruction);
            break;
            
        case MIPS_OP_JAL:
            result = zoni_cpu_execute_jal(cpu, instruction);
            break;
            
        case MIPS_OP_COP0:
            result = zoni_cpu_execute_cop0(cpu, instruction);
            break;
            
        default:
            zoni_log(ZONI_LOG_WARNING, "Unknown instruction opcode: 0x%02X", opcode);
            result = ZONI_ERROR_NOT_IMPLEMENTED;
            break;
    }
    
    // Process load delay slots after executing instruction
    zoni_cpu_dload_step(cpu);
    
    return result;
}

// ANDI rt, rs, immediate: rt = rs & immediate
zoni_error_t zoni_cpu_execute_andi(zoni_cpu_regs_t* cpu, zoni_instruction_t* instruction) {
    // Extract fields manually from raw instruction (little-endian)
    u8 rt = (instruction->raw >> 16) & 0x1F;
    u8 rs = (instruction->raw >> 21) & 0x1F;
    u16 immediate = instruction->raw & 0xFFFF;
    
    if (rt != 0) {
        u32 rs_val = cpu->gpr.r[rs];
        u32 result = rs_val & immediate;
        cpu->gpr.r[rt] = result;
        zoni_log(ZONI_LOG_DEBUG, "ANDI $%d = $%d & 0x%04X = 0x%08X", 
                 rt, rs, immediate, result);
    }
    return ZONI_SUCCESS;
}

// ORI rt, rs, immediate: rt = rs | immediate
zoni_error_t zoni_cpu_execute_ori(zoni_cpu_regs_t* cpu, zoni_instruction_t* instruction) {
    // Extract fields manually from raw instruction (little-endian)
    u8 rt = (instruction->raw >> 16) & 0x1F;
    u8 rs = (instruction->raw >> 21) & 0x1F;
    u16 immediate = instruction->raw & 0xFFFF;
    
    if (rt != 0) {
        u32 rs_val = cpu->gpr.r[rs];
        u32 result = rs_val | immediate;
        cpu->gpr.r[rt] = result;
        zoni_log(ZONI_LOG_DEBUG, "ORI $%d = $%d | 0x%04X = 0x%08X", 
                 rt, rs, immediate, result);
    }
    return ZONI_SUCCESS;
}

// XORI rt, rs, immediate: rt = rs ^ immediate
zoni_error_t zoni_cpu_execute_xori(zoni_cpu_regs_t* cpu, zoni_instruction_t* instruction) {
    // Extract fields manually from raw instruction (little-endian)
    u8 rt = (instruction->raw >> 16) & 0x1F;
    u8 rs = (instruction->raw >> 21) & 0x1F;
    u16 immediate = instruction->raw & 0xFFFF;
    
    if (rt != 0) {
        u32 rs_val = cpu->gpr.r[rs];
        u32 result = rs_val ^ immediate;
        cpu->gpr.r[rt] = result;
        zoni_log(ZONI_LOG_DEBUG, "XORI $%d = $%d ^ 0x%04X = 0x%08X", 
                 rt, rs, immediate, result);
    }
    return ZONI_SUCCESS;
}

// LUI rt, immediate: rt = immediate << 16
zoni_error_t zoni_cpu_execute_lui(zoni_cpu_regs_t* cpu, zoni_instruction_t* instruction) {
    // Extract fields manually from raw instruction (little-endian)
    u8 rt = (instruction->raw >> 16) & 0x1F;
    u16 immediate = instruction->raw & 0xFFFF;
    
    if (rt != 0) {
        u32 result = (u32)immediate << 16;
        cpu->gpr.r[rt] = result;
        zoni_log(ZONI_LOG_DEBUG, "LUI $%d = 0x%04X << 16 = 0x%08X", 
                 rt, immediate, result);
    }
    return ZONI_SUCCESS;
}

// BEQ rs, rt, offset: if (rs == rt) PC += offset
zoni_error_t zoni_cpu_execute_beq(zoni_cpu_regs_t* cpu, zoni_instruction_t* instruction) {
    // Extract fields manually from raw instruction (little-endian)
    u8 rs = (instruction->raw >> 21) & 0x1F;
    u8 rt = (instruction->raw >> 16) & 0x1F;
    s16 offset = (s16)(instruction->raw & 0xFFFF);
    
    u32 rs_val = cpu->gpr.r[rs];
    u32 rt_val = cpu->gpr.r[rt];
    
    if (rs_val == rt_val) {
        u32 new_pc = cpu->pc + 4 + (offset << 2);
        cpu->pc = new_pc;
        zoni_log(ZONI_LOG_DEBUG, "BEQ $%d == $%d, PC = 0x%08X + 4 + (%d << 2) = 0x%08X", 
                 rs, rt, cpu->pc - 4 - (offset << 2), offset, new_pc);
    } else {
        zoni_log(ZONI_LOG_DEBUG, "BEQ $%d (0x%08X) != $%d (0x%08X), no branch", 
                 rs, rs_val, rt, rt_val);
    }
    return ZONI_SUCCESS;
}

// BNE rs, rt, offset: if (rs != rt) PC += offset
zoni_error_t zoni_cpu_execute_bne(zoni_cpu_regs_t* cpu, zoni_instruction_t* instruction) {
    // Extract fields manually from raw instruction (little-endian)
    u8 rs = (instruction->raw >> 21) & 0x1F;
    u8 rt = (instruction->raw >> 16) & 0x1F;
    s16 offset = (s16)(instruction->raw & 0xFFFF);
    
    u32 rs_val = cpu->gpr.r[rs];
    u32 rt_val = cpu->gpr.r[rt];
    u32 old_pc = cpu->pc;
    
    if (rs_val != rt_val) {
        u32 new_pc = cpu->pc + 4 + (offset << 2);
        cpu->pc = new_pc;
        zoni_log(ZONI_LOG_DEBUG, "BNE $%d != $%d, PC = 0x%08X + 4 + (%d << 2) = 0x%08X", 
                 rs, rt, old_pc, offset, new_pc);
    } else {
        zoni_log(ZONI_LOG_DEBUG, "BNE $%d (0x%08X) == $%d (0x%08X), no branch", 
                 rs, rs_val, rt, rt_val);
    }
    return ZONI_SUCCESS;
}

// LW rt, offset(rs): rt = Memory[rs + offset]
zoni_error_t zoni_cpu_execute_lw(zoni_cpu_regs_t* cpu, zoni_instruction_t* instruction) {
    // Extract fields manually from raw instruction (little-endian)
    u8 rt = (instruction->raw >> 16) & 0x1F;
    u8 rs = (instruction->raw >> 21) & 0x1F;
    s16 offset = (s16)(instruction->raw & 0xFFFF);
    
    if (rt != 0) {
        u32 rs_val = cpu->gpr.r[rs];
        u32 address = rs_val + offset;
        u32 value;
        
        zoni_error_t result = zoni_cpu_read32(cpu, address, &value);
        if (result == ZONI_SUCCESS) {
            // Use load delay slot for proper MIPS timing
            zoni_cpu_do_load(cpu, rt, value);
            zoni_log(ZONI_LOG_DEBUG, "LW $%d = Memory[0x%08X + %d] = Memory[0x%08X] = 0x%08X", 
                     rt, rs_val, offset, address, value);
        } else {
            zoni_log(ZONI_LOG_ERROR, "LW failed to read from address 0x%08X", address);
            return result;
        }
    }
    return ZONI_SUCCESS;
}

// SW rt, offset(rs): Memory[rs + offset] = rt
zoni_error_t zoni_cpu_execute_sw(zoni_cpu_regs_t* cpu, zoni_instruction_t* instruction) {
    // Extract fields manually from raw instruction (little-endian)
    u8 rt = (instruction->raw >> 16) & 0x1F;
    u8 rs = (instruction->raw >> 21) & 0x1F;
    s16 offset = (s16)(instruction->raw & 0xFFFF);
    
    u32 rs_val = cpu->gpr.r[rs];
    u32 address = rs_val + offset;
    u32 value = cpu->gpr.r[rt];
    
    zoni_error_t result = zoni_cpu_write32(cpu, address, value);
    if (result == ZONI_SUCCESS) {
        zoni_log(ZONI_LOG_DEBUG, "SW Memory[0x%08X + %d] = Memory[0x%08X] = $%d = 0x%08X", 
                 rs_val, offset, address, rt, value);
    } else {
        zoni_log(ZONI_LOG_ERROR, "SW failed to write to address 0x%08X", address);
        return result;
    }
    return ZONI_SUCCESS;
}

// LB rt, offset(rs): rt = Memory[rs + offset] (signed byte)
zoni_error_t zoni_cpu_execute_lb(zoni_cpu_regs_t* cpu, zoni_instruction_t* instruction) {
    // Extract fields manually from raw instruction (little-endian)
    u8 rt = (instruction->raw >> 16) & 0x1F;
    u8 rs = (instruction->raw >> 21) & 0x1F;
    s16 offset = (s16)(instruction->raw & 0xFFFF);
    
    if (rt != 0) {
        u32 rs_val = cpu->gpr.r[rs];
        u32 address = rs_val + offset;
        u8 value;
        
        zoni_error_t result = zoni_cpu_read8(cpu, address, &value);
        if (result == ZONI_SUCCESS) {
            // Sign extend byte to 32 bits
            s32 sign_extended = (s8)value;
            zoni_cpu_do_load(cpu, rt, (u32)sign_extended);
            zoni_log(ZONI_LOG_DEBUG, "LB $%d = Memory[0x%08X + %d] = Memory[0x%08X] = 0x%02X (sign-extended to 0x%08X)", 
                     rt, rs_val, offset, address, value, sign_extended);
        } else {
            zoni_log(ZONI_LOG_ERROR, "LB failed to read from address 0x%08X", address);
            return result;
        }
    }
    return ZONI_SUCCESS;
}

// SB rt, offset(rs): Memory[rs + offset] = rt (lowest byte)
zoni_error_t zoni_cpu_execute_sb(zoni_cpu_regs_t* cpu, zoni_instruction_t* instruction) {
    // Extract fields manually from raw instruction (little-endian)
    u8 rt = (instruction->raw >> 16) & 0x1F;
    u8 rs = (instruction->raw >> 21) & 0x1F;
    s16 offset = (s16)(instruction->raw & 0xFFFF);
    
    u32 rs_val = cpu->gpr.r[rs];
    u32 address = rs_val + offset;
    u8 value = (u8)cpu->gpr.r[rt];
    
    zoni_error_t result = zoni_cpu_write8(cpu, address, value);
    if (result == ZONI_SUCCESS) {
        zoni_log(ZONI_LOG_DEBUG, "SB Memory[0x%08X + %d] = Memory[0x%08X] = $%d (0x%08X) = 0x%02X", 
                 rs_val, offset, address, rt, cpu->gpr.r[rt], value);
    } else {
        zoni_log(ZONI_LOG_ERROR, "SB failed to write to address 0x%08X", address);
        return result;
    }
    return ZONI_SUCCESS;
}

// J address: PC = (PC & 0xF0000000) | (address << 2)
zoni_error_t zoni_cpu_execute_j(zoni_cpu_regs_t* cpu, zoni_instruction_t* instruction) {
    u32 current_pc = cpu->pc;
    // Extract address field manually from raw instruction (little-endian)
    u32 address = instruction->raw & 0x3FFFFFF;
    u32 new_pc = (current_pc & 0xF0000000) | (address << 2);
    
    cpu->pc = new_pc;
    zoni_log(ZONI_LOG_DEBUG, "J PC = (0x%08X & 0xF0000000) | (0x%06X << 2) = 0x%08X", 
             current_pc, address, new_pc);
    zoni_log(ZONI_LOG_DEBUG, "J Debug: raw=0x%08X, address=0x%06X, shifted=0x%08X", 
             instruction->raw, address, address << 2);
    return ZONI_SUCCESS;
}

// JAL address: $31 = PC + 4, PC = (PC & 0xF0000000) | (address << 2)
zoni_error_t zoni_cpu_execute_jal(zoni_cpu_regs_t* cpu, zoni_instruction_t* instruction) {
    u32 current_pc = cpu->pc;
    // Extract address field manually from raw instruction (little-endian)
    u32 address = instruction->raw & 0x3FFFFFF;
    u32 new_pc = (current_pc & 0xF0000000) | (address << 2);
    
    // Save return address in $31 (ra)
    cpu->gpr.r[31] = current_pc + 4;
    cpu->pc = new_pc;
    zoni_log(ZONI_LOG_DEBUG, "JAL $31 = PC + 4 = 0x%08X + 4 = 0x%08X, PC = 0x%08X", 
             current_pc, current_pc + 4, new_pc);
    return ZONI_SUCCESS;
}

// COP0 (Coprocessor 0) instruction execution
// COP0 handles system control operations like MFC0, MTC0, RFE
zoni_error_t zoni_cpu_execute_cop0(zoni_cpu_regs_t* cpu, zoni_instruction_t* instruction) {
    // Extract fields from instruction
    u8 rs = (instruction->raw >> 21) & 0x1F;
    u8 rt = (instruction->raw >> 16) & 0x1F;
    u8 rd = (instruction->raw >> 11) & 0x1F;
    u8 funct = instruction->raw & 0x3F;
    
    // Check if this is a COP0 function (rs & 0x10)
    if (rs & 0x10) {
        // COP0 function instruction
        switch (funct) {
            case 0x10: // RFE (Return From Exception)
                // RFE: Restore Status Register from exception
                cpu->cp0.n.SR = (cpu->cp0.n.SR & 0xFFFFFFF0) | ((cpu->cp0.n.SR >> 2) & 0x0F);
                zoni_log(ZONI_LOG_DEBUG, "RFE: Restored Status Register");
                return ZONI_SUCCESS;
                
            default:
                zoni_log(ZONI_LOG_WARNING, "Unknown COP0 function: 0x%02X", funct);
                return ZONI_ERROR_NOT_IMPLEMENTED;
        }
    } else {
        // COP0 move instruction
        switch (rs) {
            case 0x00: // MFC0 (Move From Coprocessor 0)
                // MFC0 rt, rd: rt = CP0[rd]
                if (rt != 0) {
                    u32 cp0_value = 0;
                    switch (rd) {
                        case 12: // Status Register
                            cp0_value = cpu->cp0.n.SR;
                            break;
                        case 13: // Cause Register
                            cp0_value = cpu->cp0.n.Cause;
                            break;
                        case 14: // EPC (Exception Program Counter)
                            cp0_value = cpu->cp0.n.EPC;
                            break;
                        case 15: // Processor ID
                            cp0_value = cpu->cp0.n.PRid;
                            break;
                        default:
                            zoni_log(ZONI_LOG_WARNING, "MFC0: Unknown CP0 register %d", rd);
                            cp0_value = 0;
                            break;
                    }
                    cpu->gpr.r[rt] = cp0_value;
                    zoni_log(ZONI_LOG_DEBUG, "MFC0 $%d = CP0[%d] = 0x%08X", rt, rd, cp0_value);
                }
                return ZONI_SUCCESS;
                
            case 0x04: // MTC0 (Move To Coprocessor 0)
                // MTC0 rt, rd: CP0[rd] = rt
                u32 rt_value = cpu->gpr.r[rt];
                switch (rd) {
                    case 12: // Status Register
                        cpu->cp0.n.SR = rt_value;
                        zoni_log(ZONI_LOG_DEBUG, "MTC0 CP0[12] = $%d = 0x%08X", rt, rt_value);
                        break;
                    case 13: // Cause Register
                        cpu->cp0.n.Cause = rt_value;
                        zoni_log(ZONI_LOG_DEBUG, "MTC0 CP0[13] = $%d = 0x%08X", rt, rt_value);
                        break;
                    case 14: // EPC (Exception Program Counter)
                        cpu->cp0.n.EPC = rt_value;
                        zoni_log(ZONI_LOG_DEBUG, "MTC0 CP0[14] = $%d = 0x%08X", rt, rt_value);
                        break;
                    default:
                        zoni_log(ZONI_LOG_WARNING, "MTC0: Unknown CP0 register %d", rd);
                        break;
                }
                return ZONI_SUCCESS;
                
            default:
                zoni_log(ZONI_LOG_WARNING, "Unknown COP0 move: rs=0x%02X", rs);
                return ZONI_ERROR_NOT_IMPLEMENTED;
        }
    }
}

// SYSCALL instruction handler
// SYSCALL is a system call instruction that triggers an exception
// It's used extensively by the PlayStation BIOS for system calls
// Format: SYSCALL (no operands)
// Opcode: 0x00 (SPECIAL), Function: 0x0C (SYSCALL)
zoni_error_t zoni_cpu_execute_syscall(zoni_cpu_regs_t* cpu, zoni_instruction_t* instruction) {
    ZONI_UNUSED(instruction);
    // SYSCALL triggers a system call exception
    // This is the primary way the PlayStation BIOS communicates with the emulator
    // The BIOS uses SYSCALL extensively for file I/O, memory management, etc.
    
    zoni_log(ZONI_LOG_DEBUG, "SYSCALL: Triggering system call exception");
    
    // Trigger a SYSCALL exception
    // This will cause the CPU to jump to the exception handler
    // The exception handler will then handle the system call based on register values
    zoni_cpu_trigger_exception(cpu, ZONI_EXCEPTION_SYSCALL, cpu->pc);
    
    // Note: We don't increment PC here because the exception handler will handle it
    // The exception handler will set the EPC (Exception Program Counter) and jump to the exception vector
    
    return ZONI_SUCCESS;
}