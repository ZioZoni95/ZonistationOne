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
zoni_error_t zoni_cpu_execute_r_type(zoni_cpu_regs_t* cpu, zoni_instruction_t* instruction, u32 big_endian_instruction, u8 funct) {
    switch (funct) {
        case MIPS_FUNC_ADD:
            // ADD rd, rs, rt: rd = rs + rt
            // Extract registers from big-endian instruction
            u8 rd = (big_endian_instruction >> 11) & 0x1F;
            u8 rs = (big_endian_instruction >> 21) & 0x1F;
            u8 rt = (big_endian_instruction >> 16) & 0x1F;
            
            if (rd != 0) {  // $0 is always 0
                u32 rs_val = cpu->gpr.r[rs];
                u32 rt_val = cpu->gpr.r[rt];
                u32 result = rs_val + rt_val;
                cpu->gpr.r[rd] = result;
                zoni_log(ZONI_LOG_DEBUG, "ADD $%d = $%d + $%d = 0x%08X + 0x%08X = 0x%08X", 
                         rd, rs, rt, rs_val, rt_val, result);
            }
            break;
            
        case MIPS_FUNC_ADDU:
            // ADDU rd, rs, rt: rd = rs + rt (unsigned)
            if (instruction->r.rd != 0) {
                u32 rs_val = cpu->gpr.r[instruction->r.rs];
                u32 rt_val = cpu->gpr.r[instruction->r.rt];
                u32 result = rs_val + rt_val;
                cpu->gpr.r[instruction->r.rd] = result;
                zoni_log(ZONI_LOG_DEBUG, "ADDU $%d = $%d + $%d = 0x%08X + 0x%08X = 0x%08X", 
                         instruction->r.rd, instruction->r.rs, instruction->r.rt, rs_val, rt_val, result);
            }
            break;
            
        case MIPS_FUNC_SUB:
            // SUB rd, rs, rt: rd = rs - rt
            if (instruction->r.rd != 0) {
                u32 rs_val = cpu->gpr.r[instruction->r.rs];
                u32 rt_val = cpu->gpr.r[instruction->r.rt];
                u32 result = rs_val - rt_val;
                cpu->gpr.r[instruction->r.rd] = result;
                zoni_log(ZONI_LOG_DEBUG, "SUB $%d = $%d - $%d = 0x%08X - 0x%08X = 0x%08X", 
                         instruction->r.rd, instruction->r.rs, instruction->r.rt, rs_val, rt_val, result);
            }
            break;
            
        case MIPS_FUNC_SUBU:
            // SUBU rd, rs, rt: rd = rs - rt (unsigned)
            if (instruction->r.rd != 0) {
                u32 rs_val = cpu->gpr.r[instruction->r.rs];
                u32 rt_val = cpu->gpr.r[instruction->r.rt];
                u32 result = rs_val - rt_val;
                cpu->gpr.r[instruction->r.rd] = result;
                zoni_log(ZONI_LOG_DEBUG, "SUBU $%d = $%d - $%d = 0x%08X - 0x%08X = 0x%08X", 
                         instruction->r.rd, instruction->r.rs, instruction->r.rt, rs_val, rt_val, result);
            }
            break;
            
        case MIPS_FUNC_AND:
            // AND rd, rs, rt: rd = rs & rt
            if (instruction->r.rd != 0) {
                u32 rs_val = cpu->gpr.r[instruction->r.rs];
                u32 rt_val = cpu->gpr.r[instruction->r.rt];
                u32 result = rs_val & rt_val;
                cpu->gpr.r[instruction->r.rd] = result;
                zoni_log(ZONI_LOG_DEBUG, "AND $%d = $%d & $%d = 0x%08X & 0x%08X = 0x%08X", 
                         instruction->r.rd, instruction->r.rs, instruction->r.rt, rs_val, rt_val, result);
            }
            break;
            
        case MIPS_FUNC_OR:
            // OR rd, rs, rt: rd = rs | rt
            if (instruction->r.rd != 0) {
                u32 rs_val = cpu->gpr.r[instruction->r.rs];
                u32 rt_val = cpu->gpr.r[instruction->r.rt];
                u32 result = rs_val | rt_val;
                cpu->gpr.r[instruction->r.rd] = result;
                zoni_log(ZONI_LOG_DEBUG, "OR $%d = $%d | $%d = 0x%08X | 0x%08X = 0x%08X", 
                         instruction->r.rd, instruction->r.rs, instruction->r.rt, rs_val, rt_val, result);
            }
            break;
            
        case MIPS_FUNC_XOR:
            // XOR rd, rs, rt: rd = rs ^ rt
            if (instruction->r.rd != 0) {
                u32 rs_val = cpu->gpr.r[instruction->r.rs];
                u32 rt_val = cpu->gpr.r[instruction->r.rt];
                u32 result = rs_val ^ rt_val;
                cpu->gpr.r[instruction->r.rd] = result;
                zoni_log(ZONI_LOG_DEBUG, "XOR $%d = $%d ^ $%d = 0x%08X ^ 0x%08X = 0x%08X", 
                         instruction->r.rd, instruction->r.rs, instruction->r.rt, rs_val, rt_val, result);
            }
            break;
            
        case MIPS_FUNC_NOR:
            // NOR rd, rs, rt: rd = ~(rs | rt)
            if (instruction->r.rd != 0) {
                u32 rs_val = cpu->gpr.r[instruction->r.rs];
                u32 rt_val = cpu->gpr.r[instruction->r.rt];
                u32 result = ~(rs_val | rt_val);
                cpu->gpr.r[instruction->r.rd] = result;
                zoni_log(ZONI_LOG_DEBUG, "NOR $%d = ~($%d | $%d) = ~(0x%08X | 0x%08X) = 0x%08X", 
                         instruction->r.rd, instruction->r.rs, instruction->r.rt, rs_val, rt_val, result);
            }
            break;
            
        case MIPS_FUNC_SLL:
            // SLL rd, rt, shamt: rd = rt << shamt
            if (instruction->r.rd != 0) {
                u32 rt_val = cpu->gpr.r[instruction->r.rt];
                u32 result = rt_val << instruction->r.shamt;
                cpu->gpr.r[instruction->r.rd] = result;
                zoni_log(ZONI_LOG_DEBUG, "SLL $%d = $%d << %d = 0x%08X << %d = 0x%08X", 
                         instruction->r.rd, instruction->r.rt, instruction->r.shamt, rt_val, instruction->r.shamt, result);
            }
            break;
            
        case MIPS_FUNC_SRL:
            // SRL rd, rt, shamt: rd = rt >> shamt (logical)
            if (instruction->r.rd != 0) {
                u32 rt_val = cpu->gpr.r[instruction->r.rt];
                u32 result = rt_val >> instruction->r.shamt;
                cpu->gpr.r[instruction->r.rd] = result;
                zoni_log(ZONI_LOG_DEBUG, "SRL $%d = $%d >> %d = 0x%08X >> %d = 0x%08X", 
                         instruction->r.rd, instruction->r.rt, instruction->r.shamt, rt_val, instruction->r.shamt, result);
            }
            break;
            
        case MIPS_FUNC_SRA:
            // SRA rd, rt, shamt: rd = rt >> shamt (arithmetic)
            if (instruction->r.rd != 0) {
                s32 rt_val = (s32)cpu->gpr.r[instruction->r.rt];
                s32 result = rt_val >> instruction->r.shamt;
                cpu->gpr.r[instruction->r.rd] = (u32)result;
                zoni_log(ZONI_LOG_DEBUG, "SRA $%d = $%d >> %d = 0x%08X >> %d = 0x%08X", 
                         instruction->r.rd, instruction->r.rt, instruction->r.shamt, rt_val, instruction->r.shamt, result);
            }
            break;
            
        case MIPS_FUNC_JR:
            // JR rs: PC = rs
            u32 rs_val = cpu->gpr.r[instruction->r.rs];
            cpu->pc = rs_val;
            zoni_log(ZONI_LOG_DEBUG, "JR $%d = 0x%08X", instruction->r.rs, rs_val);
            break;
            
        default:
            zoni_log(ZONI_LOG_WARNING, "Unknown R-type function: 0x%02X", instruction->r.funct);
            return ZONI_ERROR_NOT_IMPLEMENTED;
    }
    
    return ZONI_SUCCESS;
}

// ADDI rt, rs, immediate: rt = rs + immediate (signed)
zoni_error_t zoni_cpu_execute_addi(zoni_cpu_regs_t* cpu, zoni_instruction_t* instruction) {
    if (instruction->i.rt != 0) {  // $0 is always 0
        u32 rs_val = cpu->gpr.r[instruction->i.rs];
        s16 immediate = (s16)instruction->i.immediate;
        s32 result = (s32)rs_val + immediate;
        cpu->gpr.r[instruction->i.rt] = (u32)result;
        zoni_log(ZONI_LOG_DEBUG, "ADDI $%d = $%d + %d = 0x%08X + %d = 0x%08X", 
                 instruction->i.rt, instruction->i.rs, immediate, rs_val, immediate, result);
    }
    return ZONI_SUCCESS;
}

// ADDIU rt, rs, immediate: rt = rs + immediate (unsigned)
zoni_error_t zoni_cpu_execute_addiu(zoni_cpu_regs_t* cpu, zoni_instruction_t* instruction) {
    if (instruction->i.rt != 0) {
        u32 rs_val = cpu->gpr.r[instruction->i.rs];
        u16 immediate = instruction->i.immediate;
        u32 result = rs_val + immediate;
        cpu->gpr.r[instruction->i.rt] = result;
        zoni_log(ZONI_LOG_DEBUG, "ADDIU $%d = $%d + %u = 0x%08X + %u = 0x%08X", 
                 instruction->i.rt, instruction->i.rs, immediate, rs_val, immediate, result);
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
    int sel = cpu->dload_sel;
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
static void zoni_cpu_exception(zoni_cpu_regs_t* cpu, zoni_exception_t cause, u32 epc) {
    zoni_log(ZONI_LOG_DEBUG, "CPU Exception: %d at PC=0x%08X", cause, epc);
    
    cpu->cp0.n.Cause = cause;
    cpu->cp0.n.EPC = epc;
    
    // Set exception vector based on cause
    switch (cause) {
        case ZONI_EXCEPTION_INT:
            cpu->pc = 0x80000080; // Interrupt vector
            break;
        case ZONI_EXCEPTION_SYSCALL:
            cpu->pc = 0x80000040; // Syscall vector
            break;
        case ZONI_EXCEPTION_BP:
            cpu->pc = 0x80000048; // Breakpoint vector
            break;
        default:
            cpu->pc = 0x80000080; // General exception vector
            break;
    }
    
    // Set exception mode in Status Register
    cpu->cp0.n.SR |= 0x00000002; // EXL bit
}

void zoni_cpu_trigger_exception(zoni_cpu_regs_t* cpu, zoni_exception_t cause, u32 epc) {
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
    // Convert instruction to big-endian for opcode check
    u32 big_endian_instruction = zoni_instruction_to_big_endian(instruction.raw);
    zoni_instruction_t temp_instruction_check;
    temp_instruction_check.raw = big_endian_instruction;
    
    if (temp_instruction_check.r.opcode != MIPS_OP_BEQ && 
        temp_instruction_check.r.opcode != MIPS_OP_BNE && 
        temp_instruction_check.r.opcode != MIPS_OP_J && 
        temp_instruction_check.r.opcode != MIPS_OP_JAL &&
        !(temp_instruction_check.r.opcode == MIPS_OP_SPECIAL && temp_instruction_check.r.funct == MIPS_FUNC_JR)) {
        cpu->pc += 4;
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
    
    // Dump GPR registers
    for (int i = 0; i < 32; i += 4) {
        zoni_log(ZONI_LOG_INFO, "R%02d: 0x%08X  R%02d: 0x%08X  R%02d: 0x%08X  R%02d: 0x%08X",
                 i, cpu->gpr.r[i], i+1, cpu->gpr.r[i+1], i+2, cpu->gpr.r[i+2], i+3, cpu->gpr.r[i+3]);
    }
    
    // Dump special registers
    zoni_log(ZONI_LOG_INFO, "LO: 0x%08X  HI: 0x%08X", cpu->gpr.r[32], cpu->gpr.r[33]);
    
    // Dump CP0 registers
    zoni_log(ZONI_LOG_INFO, "SR: 0x%08X  Cause: 0x%08X  EPC: 0x%08X",
             cpu->cp0.n.SR, cpu->cp0.n.Cause, cpu->cp0.n.EPC);
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
    zoni_instruction_t temp_instruction;
    temp_instruction.raw = big_endian_instruction;
    
    // Basic instruction disassembly based on opcode
    switch (temp_instruction.r.opcode) {
        case MIPS_OP_SPECIAL:
            // R-type instruction, check function code
            switch (temp_instruction.r.funct) {
                case MIPS_FUNC_ADD:
                    snprintf(disasm, disasm_size, "ADD $%d, $%d, $%d", 
                            temp_instruction.r.rd, temp_instruction.r.rs, temp_instruction.r.rt);
                    break;
                case MIPS_FUNC_ADDU:
                    snprintf(disasm, disasm_size, "ADDU $%d, $%d, $%d", 
                            temp_instruction.r.rd, temp_instruction.r.rs, temp_instruction.r.rt);
                    break;
                case MIPS_FUNC_SUB:
                    snprintf(disasm, disasm_size, "SUB $%d, $%d, $%d", 
                            temp_instruction.r.rd, temp_instruction.r.rs, temp_instruction.r.rt);
                    break;
                case MIPS_FUNC_SUBU:
                    snprintf(disasm, disasm_size, "SUBU $%d, $%d, $%d", 
                            temp_instruction.r.rd, temp_instruction.r.rs, temp_instruction.r.rt);
                    break;
                case MIPS_FUNC_AND:
                    snprintf(disasm, disasm_size, "AND $%d, $%d, $%d", 
                            temp_instruction.r.rd, temp_instruction.r.rs, temp_instruction.r.rt);
                    break;
                case MIPS_FUNC_OR:
                    snprintf(disasm, disasm_size, "OR $%d, $%d, $%d", 
                            temp_instruction.r.rd, temp_instruction.r.rs, temp_instruction.r.rt);
                    break;
                case MIPS_FUNC_XOR:
                    snprintf(disasm, disasm_size, "XOR $%d, $%d, $%d", 
                            temp_instruction.r.rd, temp_instruction.r.rs, temp_instruction.r.rt);
                    break;
                case MIPS_FUNC_NOR:
                    snprintf(disasm, disasm_size, "NOR $%d, $%d, $%d", 
                            temp_instruction.r.rd, temp_instruction.r.rs, temp_instruction.r.rt);
                    break;
                case MIPS_FUNC_SLL:
                    snprintf(disasm, disasm_size, "SLL $%d, $%d, %d", 
                            temp_instruction.r.rd, temp_instruction.r.rt, temp_instruction.r.shamt);
                    break;
                case MIPS_FUNC_SRL:
                    snprintf(disasm, disasm_size, "SRL $%d, $%d, %d", 
                            temp_instruction.r.rd, temp_instruction.r.rt, temp_instruction.r.shamt);
                    break;
                case MIPS_FUNC_SRA:
                    snprintf(disasm, disasm_size, "SRA $%d, $%d, %d", 
                            temp_instruction.r.rd, temp_instruction.r.rt, temp_instruction.r.shamt);
                    break;
                case MIPS_FUNC_JR:
                    snprintf(disasm, disasm_size, "JR $%d", temp_instruction.r.rs);
                    break;
                case MIPS_FUNC_JALR:
                    snprintf(disasm, disasm_size, "JALR $%d, $%d", 
                            temp_instruction.r.rd, temp_instruction.r.rs);
                    break;
                case MIPS_FUNC_SYSCALL:
                    snprintf(disasm, disasm_size, "SYSCALL");
                    break;
                case MIPS_FUNC_BREAK:
                    snprintf(disasm, disasm_size, "BREAK");
                    break;
                case MIPS_FUNC_MFHI:
                    snprintf(disasm, disasm_size, "MFHI $%d", temp_instruction.r.rd);
                    break;
                case MIPS_FUNC_MFLO:
                    snprintf(disasm, disasm_size, "MFLO $%d", temp_instruction.r.rd);
                    break;
                case MIPS_FUNC_MULT:
                    snprintf(disasm, disasm_size, "MULT $%d, $%d", 
                            temp_instruction.r.rs, temp_instruction.r.rt);
                    break;
                case MIPS_FUNC_DIV:
                    snprintf(disasm, disasm_size, "DIV $%d, $%d", 
                            temp_instruction.r.rs, temp_instruction.r.rt);
                    break;
                default:
                    snprintf(disasm, disasm_size, "UNKNOWN_R(0x%02X)", temp_instruction.r.funct);
                    break;
            }
            break;
            
        case MIPS_OP_ADDI:
            snprintf(disasm, disasm_size, "ADDI $%d, $%d, %d", 
                    temp_instruction.i.rt, temp_instruction.i.rs, (s16)temp_instruction.i.immediate);
            break;
        case MIPS_OP_ADDIU:
            snprintf(disasm, disasm_size, "ADDIU $%d, $%d, %d", 
                    temp_instruction.i.rt, temp_instruction.i.rs, (s16)temp_instruction.i.immediate);
            break;
        case MIPS_OP_ANDI:
            snprintf(disasm, disasm_size, "ANDI $%d, $%d, 0x%04X", 
                    temp_instruction.i.rt, temp_instruction.i.rs, temp_instruction.i.immediate);
            break;
        case MIPS_OP_ORI:
            snprintf(disasm, disasm_size, "ORI $%d, $%d, 0x%04X", 
                    temp_instruction.i.rt, temp_instruction.i.rs, temp_instruction.i.immediate);
            break;
        case MIPS_OP_XORI:
            snprintf(disasm, disasm_size, "XORI $%d, $%d, 0x%04X", 
                    temp_instruction.i.rt, temp_instruction.i.rs, temp_instruction.i.immediate);
            break;
        case MIPS_OP_LUI:
            snprintf(disasm, disasm_size, "LUI $%d, 0x%04X", 
                    temp_instruction.i.rt, temp_instruction.i.immediate);
            break;
        case MIPS_OP_BEQ:
            snprintf(disasm, disasm_size, "BEQ $%d, $%d, %d", 
                    temp_instruction.i.rs, temp_instruction.i.rt, (s16)temp_instruction.i.immediate);
            break;
        case MIPS_OP_BNE:
            snprintf(disasm, disasm_size, "BNE $%d, $%d, %d", 
                    temp_instruction.i.rs, temp_instruction.i.rt, (s16)temp_instruction.i.immediate);
            break;
        case MIPS_OP_LW:
            snprintf(disasm, disasm_size, "LW $%d, %d($%d)", 
                    temp_instruction.i.rt, (s16)temp_instruction.i.immediate, temp_instruction.i.rs);
            break;
        case MIPS_OP_SW:
            snprintf(disasm, disasm_size, "SW $%d, %d($%d)", 
                    temp_instruction.i.rt, (s16)temp_instruction.i.immediate, temp_instruction.i.rs);
            break;
        case MIPS_OP_LB:
            snprintf(disasm, disasm_size, "LB $%d, %d($%d)", 
                    temp_instruction.i.rt, (s16)temp_instruction.i.immediate, temp_instruction.i.rs);
            break;
        case MIPS_OP_SB:
            snprintf(disasm, disasm_size, "SB $%d, %d($%d)", 
                    temp_instruction.i.rt, (s16)temp_instruction.i.immediate, temp_instruction.i.rs);
            break;
        case MIPS_OP_J:
            snprintf(disasm, disasm_size, "J 0x%08X", 
                    (temp_instruction.j.address << 2) & 0x0FFFFFFF);
            break;
        case MIPS_OP_JAL:
            snprintf(disasm, disasm_size, "JAL 0x%08X", 
                    (temp_instruction.j.address << 2) & 0x0FFFFFFF);
            break;
        default:
            snprintf(disasm, disasm_size, "UNKNOWN(0x%02X)", temp_instruction.r.opcode);
            break;
    }
    
    return ZONI_SUCCESS;
}

// Instruction execution engine
zoni_error_t zoni_cpu_execute_instruction(zoni_cpu_regs_t* cpu, zoni_instruction_t* instruction) {
    if (!cpu || !instruction || !g_memory) {
        return ZONI_ERROR_INVALID_PARAMETER;
    }
    
    // Convert little-endian instruction to big-endian for proper execution
    u32 big_endian_instruction = zoni_instruction_to_big_endian(instruction->raw);
    
    // Process load delay slots before executing new instruction
    zoni_cpu_dload_step(cpu);
    
    // Extract opcode and function code from big-endian instruction
    u8 opcode = (big_endian_instruction >> 26) & 0x3F;
    u8 funct = big_endian_instruction & 0x3F;
    
    // Debug: Log the instruction being executed
    zoni_log(ZONI_LOG_DEBUG, "Executing instruction: raw=0x%08X, big_endian=0x%08X, opcode=0x%02X, funct=0x%02X", 
             instruction->raw, big_endian_instruction, opcode, funct);
    
    // Execute instruction based on opcode
    switch (opcode) {
        case MIPS_OP_SPECIAL:
            // R-type instruction, check function code
            return zoni_cpu_execute_r_type(cpu, instruction, big_endian_instruction, funct);
            
        case MIPS_OP_ADDI:
            return zoni_cpu_execute_addi(cpu, instruction);
            
        case MIPS_OP_ADDIU:
            return zoni_cpu_execute_addiu(cpu, instruction);
            
        case MIPS_OP_ANDI:
            return zoni_cpu_execute_andi(cpu, instruction);
            
        case MIPS_OP_ORI:
            return zoni_cpu_execute_ori(cpu, instruction);
            
        case MIPS_OP_XORI:
            return zoni_cpu_execute_xori(cpu, instruction);
            
        case MIPS_OP_LUI:
            return zoni_cpu_execute_lui(cpu, instruction);
            
        case MIPS_OP_BEQ:
            return zoni_cpu_execute_beq(cpu, instruction);
            
        case MIPS_OP_BNE:
            return zoni_cpu_execute_bne(cpu, instruction);
            
        case MIPS_OP_LW:
            return zoni_cpu_execute_lw(cpu, instruction);
            
        case MIPS_OP_SW:
            return zoni_cpu_execute_sw(cpu, instruction);
            
        case MIPS_OP_LB:
            return zoni_cpu_execute_lb(cpu, instruction);
            
        case MIPS_OP_SB:
            return zoni_cpu_execute_sb(cpu, instruction);
            
        case MIPS_OP_J:
            return zoni_cpu_execute_j(cpu, instruction);
            
        case MIPS_OP_JAL:
            return zoni_cpu_execute_jal(cpu, instruction);
            
        default:
            zoni_log(ZONI_LOG_WARNING, "Unknown instruction opcode: 0x%02X", opcode);
            return ZONI_ERROR_NOT_IMPLEMENTED;
    }
}

// ANDI rt, rs, immediate: rt = rs & immediate
zoni_error_t zoni_cpu_execute_andi(zoni_cpu_regs_t* cpu, zoni_instruction_t* instruction) {
    if (instruction->i.rt != 0) {
        u32 rs_val = cpu->gpr.r[instruction->i.rs];
        u16 immediate = instruction->i.immediate;
        u32 result = rs_val & immediate;
        cpu->gpr.r[instruction->i.rt] = result;
        zoni_log(ZONI_LOG_DEBUG, "ANDI $%d = $%d & 0x%04X = 0x%08X & 0x%04X = 0x%08X", 
                 instruction->i.rt, instruction->i.rs, immediate, rs_val, immediate, result);
    }
    return ZONI_SUCCESS;
}

// ORI rt, rs, immediate: rt = rs | immediate
zoni_error_t zoni_cpu_execute_ori(zoni_cpu_regs_t* cpu, zoni_instruction_t* instruction) {
    if (instruction->i.rt != 0) {
        u32 rs_val = cpu->gpr.r[instruction->i.rs];
        u16 immediate = instruction->i.immediate;
        u32 result = rs_val | immediate;
        cpu->gpr.r[instruction->i.rt] = result;
        zoni_log(ZONI_LOG_DEBUG, "ORI $%d = $%d | 0x%04X = 0x%08X | 0x%04X = 0x%08X", 
                 instruction->i.rt, instruction->i.rs, immediate, rs_val, immediate, result);
    }
    return ZONI_SUCCESS;
}

// XORI rt, rs, immediate: rt = rs ^ immediate
zoni_error_t zoni_cpu_execute_xori(zoni_cpu_regs_t* cpu, zoni_instruction_t* instruction) {
    if (instruction->i.rt != 0) {
        u32 rs_val = cpu->gpr.r[instruction->i.rs];
        u16 immediate = instruction->i.immediate;
        u32 result = rs_val ^ immediate;
        cpu->gpr.r[instruction->i.rt] = result;
        zoni_log(ZONI_LOG_DEBUG, "XORI $%d = $%d ^ 0x%04X = 0x%08X ^ 0x%04X = 0x%08X", 
                 instruction->i.rt, instruction->i.rs, immediate, rs_val, immediate, result);
    }
    return ZONI_SUCCESS;
}

// LUI rt, immediate: rt = immediate << 16
zoni_error_t zoni_cpu_execute_lui(zoni_cpu_regs_t* cpu, zoni_instruction_t* instruction) {
    if (instruction->i.rt != 0) {
        u16 immediate = instruction->i.immediate;
        u32 result = immediate << 16;
        cpu->gpr.r[instruction->i.rt] = result;
        zoni_log(ZONI_LOG_DEBUG, "LUI $%d = 0x%04X << 16 = 0x%08X", 
                 instruction->i.rt, immediate, result);
    }
    return ZONI_SUCCESS;
}

// BEQ rs, rt, offset: if (rs == rt) PC += offset
zoni_error_t zoni_cpu_execute_beq(zoni_cpu_regs_t* cpu, zoni_instruction_t* instruction) {
    u32 rs_val = cpu->gpr.r[instruction->i.rs];
    u32 rt_val = cpu->gpr.r[instruction->i.rt];
    s16 offset = (s16)instruction->i.immediate;
    
    if (rs_val == rt_val) {
        u32 new_pc = cpu->pc + 4 + (offset << 2);
        cpu->pc = new_pc;
        zoni_log(ZONI_LOG_DEBUG, "BEQ $%d == $%d, PC = 0x%08X + 4 + (%d << 2) = 0x%08X", 
                 instruction->i.rs, instruction->i.rt, cpu->pc - 4 - (offset << 2), offset, new_pc);
    } else {
        zoni_log(ZONI_LOG_DEBUG, "BEQ $%d (0x%08X) != $%d (0x%08X), no branch", 
                 instruction->i.rs, rs_val, instruction->i.rt, rt_val);
    }
    return ZONI_SUCCESS;
}

// BNE rs, rt, offset: if (rs != rt) PC += offset
zoni_error_t zoni_cpu_execute_bne(zoni_cpu_regs_t* cpu, zoni_instruction_t* instruction) {
    u32 rs_val = cpu->gpr.r[instruction->i.rs];
    u32 rt_val = cpu->gpr.r[instruction->i.rt];
    s16 offset = (s16)instruction->i.immediate;
    u32 old_pc = cpu->pc;
    
    if (rs_val != rt_val) {
        u32 new_pc = cpu->pc + 4 + (offset << 2);
        cpu->pc = new_pc;
        zoni_log(ZONI_LOG_DEBUG, "BNE $%d != $%d, PC = 0x%08X + 4 + (%d << 2) = 0x%08X", 
                 instruction->i.rs, instruction->i.rt, old_pc, offset, new_pc);
    } else {
        zoni_log(ZONI_LOG_DEBUG, "BNE $%d (0x%08X) == $%d (0x%08X), no branch", 
                 instruction->i.rs, rs_val, instruction->i.rt, rt_val);
    }
    return ZONI_SUCCESS;
}

// LW rt, offset(rs): rt = Memory[rs + offset]
zoni_error_t zoni_cpu_execute_lw(zoni_cpu_regs_t* cpu, zoni_instruction_t* instruction) {
    if (instruction->i.rt != 0) {
        u32 rs_val = cpu->gpr.r[instruction->i.rs];
        s16 offset = (s16)instruction->i.immediate;
        u32 address = rs_val + offset;
        u32 value;
        
        zoni_error_t result = zoni_cpu_read32(cpu, address, &value);
        if (result == ZONI_SUCCESS) {
            // Use load delay slot for proper MIPS timing
            zoni_cpu_do_load(cpu, instruction->i.rt, value);
            zoni_log(ZONI_LOG_DEBUG, "LW $%d = Memory[0x%08X + %d] = Memory[0x%08X] = 0x%08X", 
                     instruction->i.rt, rs_val, offset, address, value);
        } else {
            zoni_log(ZONI_LOG_ERROR, "LW failed to read from address 0x%08X", address);
            return result;
        }
    }
    return ZONI_SUCCESS;
}

// SW rt, offset(rs): Memory[rs + offset] = rt
zoni_error_t zoni_cpu_execute_sw(zoni_cpu_regs_t* cpu, zoni_instruction_t* instruction) {
    u32 rs_val = cpu->gpr.r[instruction->i.rs];
    s16 offset = (s16)instruction->i.immediate;
    u32 address = rs_val + offset;
    u32 value = cpu->gpr.r[instruction->i.rt];
    
    zoni_error_t result = zoni_cpu_write32(cpu, address, value);
    if (result == ZONI_SUCCESS) {
        zoni_log(ZONI_LOG_DEBUG, "SW Memory[0x%08X + %d] = Memory[0x%08X] = $%d = 0x%08X", 
                 rs_val, offset, address, instruction->i.rt, value);
    } else {
        zoni_log(ZONI_LOG_ERROR, "SW failed to write to address 0x%08X", address);
        return result;
    }
    return ZONI_SUCCESS;
}

// LB rt, offset(rs): rt = Memory[rs + offset] (signed byte)
zoni_error_t zoni_cpu_execute_lb(zoni_cpu_regs_t* cpu, zoni_instruction_t* instruction) {
    if (instruction->i.rt != 0) {
        u32 rs_val = cpu->gpr.r[instruction->i.rs];
        s16 offset = (s16)instruction->i.immediate;
        u32 address = rs_val + offset;
        u8 value;
        
        zoni_error_t result = zoni_cpu_read8(cpu, address, &value);
        if (result == ZONI_SUCCESS) {
            // Sign extend byte to 32 bits
            s32 sign_extended = (s8)value;
            zoni_cpu_do_load(cpu, instruction->i.rt, (u32)sign_extended);
            zoni_log(ZONI_LOG_DEBUG, "LB $%d = Memory[0x%08X + %d] = Memory[0x%08X] = 0x%02X (sign-extended to 0x%08X)", 
                     instruction->i.rt, rs_val, offset, address, value, sign_extended);
        } else {
            zoni_log(ZONI_LOG_ERROR, "LB failed to read from address 0x%08X", address);
            return result;
        }
    }
    return ZONI_SUCCESS;
}

// SB rt, offset(rs): Memory[rs + offset] = rt (lowest byte)
zoni_error_t zoni_cpu_execute_sb(zoni_cpu_regs_t* cpu, zoni_instruction_t* instruction) {
    u32 rs_val = cpu->gpr.r[instruction->i.rs];
    s16 offset = (s16)instruction->i.immediate;
    u32 address = rs_val + offset;
    u8 value = (u8)cpu->gpr.r[instruction->i.rt];
    
    zoni_error_t result = zoni_cpu_write8(cpu, address, value);
    if (result == ZONI_SUCCESS) {
        zoni_log(ZONI_LOG_DEBUG, "SB Memory[0x%08X + %d] = Memory[0x%08X] = $%d (0x%08X) = 0x%02X", 
                 rs_val, offset, address, instruction->i.rt, cpu->gpr.r[instruction->i.rt], value);
    } else {
        zoni_log(ZONI_LOG_ERROR, "SB failed to write to address 0x%08X", address);
        return result;
    }
    return ZONI_SUCCESS;
}

// J address: PC = (PC & 0xF0000000) | (address << 2)
zoni_error_t zoni_cpu_execute_j(zoni_cpu_regs_t* cpu, zoni_instruction_t* instruction) {
    u32 current_pc = cpu->pc;
    u32 address = instruction->j.address;
    u32 new_pc = (current_pc & 0xF0000000) | (address << 2);
    
    cpu->pc = new_pc;
    zoni_log(ZONI_LOG_DEBUG, "J PC = (0x%08X & 0xF0000000) | (0x%06X << 2) = 0x%08X", 
             current_pc, address, new_pc);
    return ZONI_SUCCESS;
}

// JAL address: $31 = PC + 4, PC = (PC & 0xF0000000) | (address << 2)
zoni_error_t zoni_cpu_execute_jal(zoni_cpu_regs_t* cpu, zoni_instruction_t* instruction) {
    u32 current_pc = cpu->pc;
    u32 address = instruction->j.address;
    u32 new_pc = (current_pc & 0xF0000000) | (address << 2);
    
    // Save return address in $31 (ra)
    cpu->gpr.r[31] = current_pc + 4;
    cpu->pc = new_pc;
    zoni_log(ZONI_LOG_DEBUG, "JAL $31 = PC + 4 = 0x%08X + 4 = 0x%08X, PC = 0x%08X", 
             current_pc, current_pc + 4, new_pc);
    return ZONI_SUCCESS;
}