#include "cpu.h"
#include <stdio.h>
#include <stdlib.h> // For exit() on fatal errors (like GTE)
#include <limits.h> // If needed for overflow checks (though __builtin is used)
#include <string.h> // For memcpy
#include <stdbool.h>// For bool type
#include "log.h"

// --- CPU Initialization ---
/**
 * @brief Initializes the CPU state to power-on defaults.
 */
void cpu_init(Cpu* cpu, Interconnect* inter) {
    LOG_CPU_INFO("CPU initialized");
    LOG_INFO("Initializing CPU...\n");

    cpu->pc = 0xbfc00000;         // Reset vector: Start of BIOS
    cpu->next_pc = cpu->pc + 4;   // Initial next PC
    cpu->current_pc = cpu->pc;    // Initial current PC (doesn't matter much before first cycle)
    cpu->inter = inter;           // Store pointer to interconnect

    // Initialize GPRs
    for (int i = 0; i < 32; ++i) {
        cpu->regs[i] = 0xdeadbeef;     // Input set garbage value
        cpu->out_regs[i] = 0xdeadbeef; // Output set garbage value
    }
    cpu->regs[REG_ZERO] = 0;      // R0 is always 0
    cpu->out_regs[REG_ZERO] = 0;  // Ensure R0 is 0 in output set too

    // Initialize Load Delay Slot state
    cpu->load_reg_idx = REG_ZERO; // Target R0 initially (no-op)
    cpu->load_value = 0;

    // Initialize HI/LO registers
    cpu->hi = 0xdeadbeef;
    cpu->lo = 0xdeadbeef;

    // Initialize Branch Delay Slot state
    cpu->branch_taken = false;    // Not initially in a branch
    cpu->in_delay_slot = false;   // Not initially in a delay slot

    // Initialize Coprocessor 0 Registers
    cpu->sr = 0x00000000;    // Status Register: Start with interrupts DISABLED (IEC=0)
    cpu->cause = 0;         // Cause Register (cleared)
    cpu->epc = 0;           // Exception PC (cleared)

    LOG_INFO("  Initializing I-Cache...\n");
    for (int i = 0; i < ICACHE_NUM_LINES; ++i) {
        cpu->icache[i].tag = 0xFFFFFFFF; // Initialize tag to an invalid pattern
        for (int j = 0; j < ICACHE_LINE_WORDS; ++j) {
            cpu->icache[i].valid[j] = false; // Mark all words in the line as invalid
            cpu->icache[i].data[j] = 0xDEADBEEF; // Optional: Initialize data to garbage
        }
    }

    // Initialize GTE
    LOG_INFO("  Initializing GTE...\n");
    gte_init(&cpu->gte);

    LOG_INFO("CPU Initialized: PC=0x%08x, NextPC=0x%08x, SR=0x%08x\n", cpu->pc, cpu->next_pc, cpu->sr);
}


// --- Register Access ---
/**
 * @brief Reads the value of a GPR from the input set (cpu->regs).
 */
uint32_t cpu_reg(Cpu* cpu, RegisterIndex index) {
    // No need to check index 0 specifically, as cpu->regs[0] is always 0.
    if (index >= 32) {
        LOG_ERROR("GPR read index out of bounds: %u\n", index);
        return 0; // Or trigger an internal error
    }
    return cpu->regs[index];
}

/**
 * @brief Writes a value to a GPR in the output set (cpu->out_regs).
 */
void cpu_set_reg(Cpu* cpu, RegisterIndex index, uint32_t value) {
    if (index >= 32) {
        LOG_ERROR("GPR write index out of bounds: %u\n", index);
        return;
    }
    // Write to output register file, *except* for R0
    if (index != REG_ZERO) {
        cpu->out_regs[index] = value;
    }
    // Ensure R0 in the output set remains 0, regardless of attempted write.
    cpu->out_regs[REG_ZERO] = 0;
}


// --- Branch/Jump Helper ---
/**
 * @brief Updates next_pc for branch instructions based on offset.
 */
void cpu_branch(Cpu* cpu, uint32_t offset_se) {
    // MIPS branch offsets are relative to the instruction *after* the delay slot (PC+4),
    // but since our 'current_pc' points to the branch itself, the effective base is current_pc+4.
    // The offset is shifted left by 2 because it's word-aligned.
    uint32_t branch_offset = offset_se << 2;
    cpu->next_pc = cpu->current_pc + 4 + branch_offset; // Target is relative to PC+4
    // The instruction handler (e.g., op_beq) MUST set cpu->branch_taken = true after calling this.
}

/**
 * @brief Handles specific BIOS A, B, and C function calls.
 * @return Returns true if the syscall was handled, false otherwise.
 */
bool handle_bios_syscall(Cpu* cpu, uint32_t syscall_num) {
    switch (syscall_num) {
        case 0x01: // EnterCriticalSection
            cpu->sr &= ~1; // Disable interrupts
            return true;   // Syscall was handled

        case 0x02: // ExitCriticalSection
            cpu->sr |= 1;  // Enable interrupts
            return true;   // Syscall was handled

        case 0x19: // B_clr_event(event)
            // Stub - does nothing, but we acknowledge it as handled.
            return true;   // Syscall was handled
        
        // Add more handled syscalls here as they appear in the logs.

        default:
            // We encountered a syscall we don't know how to handle.
            return false;
    }
}


// --- Exception Handling ---
/**
 * @brief Handles CPU exceptions (Interrupts, Syscalls, Errors, etc.).
 */
void cpu_exception(Cpu* cpu, ExceptionCause cause) {
    cpu->exception_pending = true;
    // Log all exception entries in nocash/PSX-Spex style
    LOG_CPU_IMPORTANT("@PSX-Spex EXCEPTION: cause=0x%02x EPC=0x%08x PC=0x%08x SR=0x%08x BadVaddr=0x%08x InDelaySlot=%d", cause, cpu->epc, cpu->current_pc, cpu->sr, (cpu->cause == EXCEPTION_LOAD_ADDRESS_ERROR || cpu->cause == EXCEPTION_STORE_ADDRESS_ERROR) ? cpu->epc : 0, cpu->in_delay_slot);
    LOG_INFO("[CPU] Exception raised: Cause=0x%02x, PC=0x%08x\n", cause, cpu->pc);
    if (cause == EXCEPTION_INTERRUPT) {
        LOG_INFO("[CPU] Entered interrupt handler (IRQ)\n");
    }
    // Minimal debug print for exceptions
    LOG_INFO("!!! CPU Exception: Cause=0x%02x, PC=0x%08x, InDelaySlot=%d !!!\n",
           cause, cpu->current_pc, cpu->in_delay_slot);

    // Determine exception handler address based on SR bit 22 (BEV)
    uint32_t handler_addr = (cpu->sr & (1 << 22)) ? 0xbfc00180  : 0x80000080;

    // Update Status Register (SR): Push mode bits onto the stack
    // This disables interrupts (sets IEC=0) and forces kernel mode (KUc=0) in the new state.
    uint32_t mode_stack = cpu->sr & 0x3f;    // Get bits 5:0 (KU/IE stack)
    cpu->sr &= ~0x3f;                       // Clear bits 5:0
    cpu->sr |= (mode_stack << 2) & 0x3f;    // Shift stack left, pushing 0s into KUc/IEc

    // Update Cause Register: Set ExcCode (bits 6:2)
    // Preserve Interrupt Pending bits (15:8), clear the rest for now
    uint32_t ip_bits = cpu->cause & 0xFF00; // Preserve IP bits
    cpu->cause = ip_bits | ((uint32_t)cause << 2); // Set ExcCode

    // Update EPC and Cause BD bit based on delay slot
    if (cpu->in_delay_slot) {
        cpu->epc = cpu->current_pc - 4; // EPC points to the branch instruction
        cpu->cause |= (1 << 31);        // Set the Branch Delay (BD) bit
    } else {
        cpu->epc = cpu->current_pc;     // EPC points to the faulting instruction
        cpu->cause &= ~(1 << 31);       // Ensure BD bit is clear
    }

    // --- IMPORTANT MODIFICATION: INTERRUPT Specific Dispatch ---
    if (cause == EXCEPTION_INTERRUPT) {
        // For INTERRUPTs, we need to acknowledge the interrupt by writing to I_STAT
        // This mimics what the BIOS exception handler would do
        uint16_t current_status = cpu->inter->irq_status;
        uint16_t current_mask = cpu->inter->irq_mask;
        uint16_t pending_interrupts = current_status & current_mask;
        
        LOG_INFO("[CPU] Interrupt Exception: I_STAT=0x%04x, I_MASK=0x%04x, Pending=0x%04x\n", 
                current_status, current_mask, pending_interrupts);
        
        // Acknowledge all pending interrupts by writing to I_STAT
        // According to PSX-Spex: Writing 1 to a bit clears that interrupt
        if (pending_interrupts != 0) {
            // Write to I_STAT to acknowledge (clear) the pending interrupts
            interconnect_store16(cpu->inter, IRQ_STATUS_ADDR, pending_interrupts);
            LOG_INFO("[CPU] Acknowledged interrupts: 0x%04x\n", pending_interrupts);
        }

        // --- STRICT PSX-Spex/nocash: GTE/COP2 Command Handling ---
        // If the instruction at EPC is a GTE/COP2 command (opcode 0x4Axxxxxx), increment EPC by 4
        // See: https://psx-spx.consoledev.net/cpuspecifications/#interrupts-vs-gte-commands
        uint32_t epc_instr = interconnect_load32(cpu->inter, cpu->epc);
        if ((epc_instr & 0xFE000000) == 0x4A000000) {
            LOG_CPU_IMPORTANT("@PSX-Spex GTE interrupt quirk: EPC advanced to 0x%08x", cpu->epc + 4);
            cpu->epc += 4;
        }
        // --- END STRICT GTE HANDLING ---
        
        // Return to the instruction that was interrupted
        // The `rfe` instruction in the real BIOS's exception handler would normally do this.
        cpu->pc = cpu->epc; // Return to the interrupted instruction
        cpu->next_pc = cpu->pc + 4; // Set next_pc sequentially
        
        // Restore interrupt enable state (IEC=1) to allow future interrupts
        // This mimics the RFE instruction behavior
        uint32_t mode_stack = cpu->sr & 0x3f;    // Get bits 5:0 (KU/IE stack)
        cpu->sr &= ~0x3f;                       // Clear bits 5:0
        cpu->sr |= (mode_stack >> 2) & 0x3f;    // Shift stack right, popping into KUc/IEc
        
        LOG_CPU_IMPORTANT("@PSX-Spex RFE: Returning from interrupt to EPC=0x%08x SR=0x%08x", cpu->epc, cpu->sr);
        LOG_INFO("[CPU] Exception handled, returning to PC=0x%08x\n", cpu->pc);
        return; // Exit exception handling, as interrupt is "handled" directly
    }
    // --- END INTERRUPT MODIFICATION ---

    // For all other exceptions, jump to the generic exception handler vector.
    cpu->pc = handler_addr;
    cpu->next_pc = cpu->pc + 4;
}


// --- Main Execution Cycle ---
/**
 * @brief Executes one full CPU cycle.
 */
void cpu_run_next_instruction(Cpu* cpu) {
    cpu->exception_pending = false; // Clear at start of cycle
    static uint64_t instruction_counter = 0;
    static uint32_t last_pc = 0;
    static uint64_t stuck_counter = 0;
    instruction_counter++;
    if (cpu->pc == last_pc) {
        stuck_counter++;
        if (stuck_counter % 1000000 == 0) {
            LOG_INFO("[CPU] Stuck: PC=0x%08x for %llu instructions", cpu->pc, stuck_counter);
        }
    } else {
        stuck_counter = 0;
        last_pc = cpu->pc;
    }
    // Only log progress every 100,000 instructions to avoid log spam in BIOS loops
    if (instruction_counter % 100000 == 0) {
        LOG_INFO("[CPU] Progress: Executed %llu instructions. PC=0x%08x", instruction_counter, cpu->pc);
    }

    // --- 1. Check for Interrupts ---
    // Must happen before fetching the next instruction.
    uint16_t status = cpu->inter->irq_status;
    uint16_t mask = cpu->inter->irq_mask;
    bool interrupts_globally_enabled = (cpu->sr & 1) != 0; // Check SR[0] (IEC)

    // Add detailed interrupt logging
    if ((status & mask) != 0) {
        LOG_INFO("[CPU] Interrupt Check: I_STAT=0x%04x, I_MASK=0x%04x, IEC=%d, Pending=0x%04x\n", 
                status, mask, interrupts_globally_enabled, (status & mask));
    }

    if ((status & mask) != 0 && interrupts_globally_enabled) {
        static int irq_log_count = 0;
        if (irq_log_count < 10 || irq_log_count % 1000 == 0) {
            LOG_CPU_INFO("[CPU] Triggering Interrupt Exception: I_STAT=0x%04x, I_MASK=0x%04x", status, mask);
        }
        irq_log_count++;
        cpu_exception(cpu, EXCEPTION_INTERRUPT);
        return; // Skip instruction execution, jump to handler
    }

    // --- 2. Handle Pending Load Delay ---
    // Apply the value from the *previous* cycle's load to the *current* output register set.
    cpu_set_reg(cpu, cpu->load_reg_idx, cpu->load_value);
    cpu->load_reg_idx = REG_ZERO; // Reset for the current cycle

    // --- 3. Fetch Instruction ---
    // Store PC of instruction being fetched/executed
    cpu->current_pc = cpu->pc;

    // Check PC alignment before fetch
    if (cpu->current_pc % 4 != 0) {
        LOG_ERROR("PC Alignment Error: PC=0x%08x\n", cpu->current_pc);
        cpu_exception(cpu, EXCEPTION_LOAD_ADDRESS_ERROR);
        return;
    }

    // Fetch instruction word from memory via interconnect
    uint32_t instruction = cpu_icache_fetch(cpu, cpu->current_pc); // <<< NEW LINE
    LOG_CPU_TRACE("PC=0x%08x, instruction=0x%08x", cpu->current_pc, instruction);

    // --- 4. Update Delay Slot State & Advance PC ---
    cpu->in_delay_slot = cpu->branch_taken; // Are we in a delay slot caused by the *previous* instruction?
    cpu->branch_taken = false;              // Reset branch flag for *current* instruction

    // Prepare PC for the *next* cycle (target of jump/branch or sequential)
    cpu->pc = cpu->next_pc;             // Advance PC to what was calculated last cycle
    cpu->next_pc = cpu->pc + 4;         // Assume sequential execution for now

    // --- 5. Commit Register State ---
    // Copy the output registers from the previous cycle to the input registers for this cycle.
    memcpy(cpu->regs, cpu->out_regs, sizeof(cpu->regs));
    // cpu->regs[REG_ZERO] is already 0 due to previous cpu_set_reg calls

    // --- 6. Decode and Execute ---
    // This might update cpu->next_pc and set cpu->branch_taken = true
    decode_and_execute(cpu, instruction);
    if (cpu->exception_pending) {
        return; // Exception occurred, do not update PC/next_pc again
    }

    // --- 7. Finalize State ---
    // Ensure R0 in the output set is still 0 for the next cycle.
    // (cpu_set_reg already handles this, but double-checking doesn't hurt)
    cpu->out_regs[REG_ZERO] = 0;

    // After executing each instruction (e.g., at the end of cpu_run_next_instruction):
    if ((cpu->inter->irq_status & cpu->inter->irq_mask) != 0) {
        cpu_exception(cpu, EXCEPTION_INTERRUPT);
    }
}

/**
 * @brief Fetches an instruction word from memory, checking the instruction cache first.
 * Handles cache lookup, hit/miss logic, and fetching from interconnect on miss.
 * Based on Guide Section 8.1 and 8.2 principles.
 * @param cpu Pointer to the Cpu state (containing the cache).
 * @param vaddr The virtual address of the instruction to fetch.
 * @return The 32-bit instruction word.
 */
uint32_t cpu_icache_fetch(Cpu* cpu, uint32_t vaddr) {
    // --- Cache Bypass Check ---
    // KSEG1 region (0xA0000000 - 0xBFFFFFFF) is un-cached.
    // Check the top 3 bits. If they are 101 (binary), it's KSEG1.
    if ((vaddr >> 29) == 0b101) {
        // KSEG1: Bypass cache, fetch directly from interconnect
        // printf("~ I-Cache Bypass (KSEG1 address: 0x%08x)\n", vaddr); // Optional debug
        return interconnect_load32(cpu->inter, vaddr);
    }
    // TODO: Add checks for SR[IsC] (cache isolation) and SR[SwC] (swap caches)
    //       if implementing those features later. For now, assume cache is active.


    // --- Address Calculation ---
    // The cache uses physical addresses for tags and indexing.
    uint32_t paddr = mask_region(vaddr);

    // Extract cache components from physical address (based on 4KB, 4-word lines)
    // Tag:          Bits [31:12] of paddr
    // Line Index:   Bits [11:4] of paddr (determines which of the 256 lines)
    // Word Index:   Bits [3:2]  of paddr (determines which word within the line)
    //
    uint32_t tag        = paddr >> 12;
    uint32_t line_index = (paddr >> 4) & (ICACHE_NUM_LINES - 1); // Mask for 256 lines (0xFF)
    uint32_t word_index = (paddr >> 2) & (ICACHE_LINE_WORDS - 1); // Mask for 4 words (0x3)

    // Get pointer to the relevant cache line
    ICacheLine* line = &cpu->icache[line_index];

    // --- Cache Lookup ---
    if (line->tag == tag && line->valid[word_index]) {
        // Cache Hit! Tags match and the specific word is valid.
        // printf("~ I-Cache Hit:  0x%08x (Line: %u, Word: %u)\n", vaddr, line_index, word_index); // Optional debug
        return line->data[word_index];
    }

    // --- Cache Miss ---
    // printf("~ I-Cache Miss: 0x%08x (Line: %u, Word: %u)\n", vaddr, line_index, word_index); // Optional debug

    // Fetch the required block from memory.
    // According to the guide, on a miss for word N,
    // words N through 3 of that cache line are fetched from memory.
    // Words 0 through N-1 are not fetched in this operation.

    // Calculate the physical address corresponding to the start of the cache line.
    uint32_t line_paddr_start = paddr & ~((ICACHE_LINE_WORDS * 4) - 1); // Align down to 16-byte boundary (mask low 4 bits)

    // Update the tag for the cache line (this happens even on miss)
    line->tag = tag;

    // Invalidate words in the line *before* the one we are fetching,
    // as the tag has changed, making any previous data for a different tag invalid.
    for (uint32_t j = 0; j < word_index; ++j) {
        line->valid[j] = false;
    }

    // Fetch words from memory starting at the missed word's index up to the end of the line.
    for (uint32_t j = word_index; j < ICACHE_LINE_WORDS; ++j) {
        // Calculate the physical address for this word
        uint32_t fetch_paddr = line_paddr_start + (j * 4);
        // Fetch from interconnect (bypassing cache itself - interconnect doesn't call back here)
        uint32_t instruction_data = interconnect_load32(cpu->inter, fetch_paddr);
        // Store fetched data in the cache line
        line->data[j] = instruction_data;
        // Mark this word as valid
        line->valid[j] = true;
    }

    // Return the instruction data for the originally requested word index
    return line->data[word_index];
}


// --- Instruction Decoding Logic ---
/**
 * @brief Decodes instruction and calls the appropriate handler.
 */
void decode_and_execute(Cpu* cpu, uint32_t instruction) {
    uint32_t opcode = instr_function(instruction);

    switch(opcode) {
        // --- R-Type (Opcode 0x00) --- uses subfunction field ---
        case 0b000000: {
            uint32_t subfunc = instr_subfunction(instruction);
            switch(subfunc) {
                case 0b000000: op_sll(cpu, instruction); break;     // SLL
                case 0b000010: op_srl(cpu, instruction); break;     // SRL
                case 0b000011: op_sra(cpu, instruction); break;     // SRA
                case 0b000100: op_sllv(cpu, instruction); break;    // SLLV
                case 0b000110: op_srlv(cpu, instruction); break;    // SRLV
                case 0b000111: op_srav(cpu, instruction); break;    // SRAV
                case 0b001000: op_jr(cpu, instruction); break;      // JR
                case 0b001001: op_jalr(cpu, instruction); break;    // JALR
                case 0b001100: op_syscall(cpu, instruction); break; // SYSCALL
                case 0b001101: op_break(cpu, instruction); break;   // BREAK
                case 0b010000: op_mfhi(cpu, instruction); break;    // MFHI
                case 0b010001: op_mthi(cpu, instruction); break;    // MTHI
                case 0b010010: op_mflo(cpu, instruction); break;    // MFLO
                case 0b010011: op_mtlo(cpu, instruction); break;    // MTLO
                case 0b011000: op_mult(cpu, instruction); break;    // MULT
                case 0b011001: op_multu(cpu, instruction); break;   // MULTU
                case 0b011010: op_div(cpu, instruction); break;     // DIV
                case 0b011011: op_divu(cpu, instruction); break;    // DIVU
                case 0b100000: op_add(cpu, instruction); break;     // ADD
                case 0b100001: op_addu(cpu, instruction); break;    // ADDU
                case 0b100010: op_sub(cpu, instruction); break;     // SUB
                case 0b100011: op_subu(cpu, instruction); break;    // SUBU
                case 0b100100: op_and(cpu, instruction); break;     // AND
                case 0b100101: op_or(cpu, instruction); break;      // OR
                case 0b100110: op_xor(cpu, instruction); break;     // XOR
                case 0b100111: op_nor(cpu, instruction); break;     // NOR
                case 0b101010: op_slt(cpu, instruction); break;     // SLT
                case 0b101011: op_sltu(cpu, instruction); break;    // SLTU
                default: op_illegal(cpu, instruction); break;       // Unhandled/Illegal R-Type
            }
            break; // End R-Type block
        }

        // --- J-Type ---
        case 0b000010: op_j(cpu, instruction); break;       // J
        case 0b000011: op_jal(cpu, instruction); break;     // JAL

        // --- I-Type (Branches) ---
        case 0b000100: op_beq(cpu, instruction); break;     // BEQ
        case 0b000101: op_bne(cpu, instruction); break;     // BNE
        case 0b000110: op_blez(cpu, instruction); break;    // BLEZ
        case 0b000111: op_bgtz(cpu, instruction); break;    // BGTZ

        // --- I-Type (Immediate Arithmetic/Logical) ---
        case 0b001000: op_addi(cpu, instruction); break;    // ADDI
        case 0b001001: op_addiu(cpu, instruction); break;   // ADDIU
        case 0b001010: op_slti(cpu, instruction); break;    // SLTI
        case 0b001011: op_sltiu(cpu, instruction); break;   // SLTIU
        case 0b001100: op_andi(cpu, instruction); break;    // ANDI
        case 0b001101: op_ori(cpu, instruction); break;     // ORI
        case 0b001110: op_xori(cpu, instruction); break;    // XORI
        case 0b001111: op_lui(cpu, instruction); break;     // LUI

        // --- I-Type (Loads) ---
        case 0b100000: op_lb(cpu, instruction); break;      // LB
        case 0b100001: op_lh(cpu, instruction); break;      // LH
        case 0b100010: op_lwl(cpu, instruction); break;     // LWL
        case 0b100011: op_lw(cpu, instruction); break;      // LW
        case 0b100100: op_lbu(cpu, instruction); break;     // LBU
        case 0b100101: op_lhu(cpu, instruction); break;     // LHU
        case 0b100110: op_lwr(cpu, instruction); break;     // LWR

        // --- I-Type (Stores) ---
        case 0b101000: op_sb(cpu, instruction); break;      // SB
        case 0b101001: op_sh(cpu, instruction); break;      // SH
        case 0b101010: op_swl(cpu, instruction); break;     // SWL
        case 0b101011: op_sw(cpu, instruction); break;      // SW
        case 0b101110: op_swr(cpu, instruction); break;     // SWR

        // --- Coprocessor Instructions ---
        case 0b010000: op_cop0(cpu, instruction); break;    // COP0 (System Control)
        case 0b010001: op_cop1(cpu, instruction); break;    // COP1 (FPU - Unused -> Exception)
        case 0b010010: op_cop2(cpu, instruction); break;    // COP2 (GTE)
        case 0b010011: op_cop3(cpu, instruction); break;    // COP3 (Unused -> Exception)

        // --- Coprocessor Load/Store ---
        case 0b110000: op_lwc0(cpu, instruction); break;    // LWC0 (-> Exception)
        case 0b110001: op_lwc1(cpu, instruction); break;    // LWC1 (-> Exception)
        case 0b110010: op_lwc2(cpu, instruction); break;    // LWC2 (GTE Load)
        case 0b110011: op_lwc3(cpu, instruction); break;    // LWC3 (-> Exception)
        case 0b111000: op_swc0(cpu, instruction); break;    // SWC0 (-> Exception)
        case 0b111001: op_swc1(cpu, instruction); break;    // SWC1 (-> Exception)
        case 0b111010: op_swc2(cpu, instruction); break;    // SWC2 (GTE Store)
        case 0b111011: op_swc3(cpu, instruction); break;    // SWC3 (-> Exception)

        // --- Special Branch (BGEZ/BLTZ etc.) ---
        case 0b000001: op_bxx(cpu, instruction); break;     // Handles REGIMM branches

        // --- Default: Illegal/Unhandled Opcode ---
        default: op_illegal(cpu, instruction); break;
    }
}


// --- Individual Instruction Implementations ---
// (Keep essential debug prints only: exceptions, cache isolation, GTE/COP errors)

void op_lui(Cpu* cpu, uint32_t instruction) {
    uint32_t imm = instr_imm(instruction);
    uint32_t rt = instr_t(instruction);
    cpu_set_reg(cpu, rt, imm << 16);
}

void op_ori(Cpu* cpu, uint32_t instruction) {
    uint32_t imm = instr_imm(instruction);
    uint32_t rt = instr_t(instruction);
    uint32_t rs = instr_s(instruction);
    cpu_set_reg(cpu, rt, cpu_reg(cpu, rs) | imm);
}

void op_sw(Cpu* cpu, uint32_t instruction) {
    if ((cpu->sr & 0x10000) != 0) { // Check cache isolation bit
        LOG_TRACE("~ SW Ignored (Cache Isolated, SR=0x%08x)\n", cpu->sr);
        return;
    }
    uint32_t offset = instr_imm_se(instruction);
    uint32_t rt = instr_t(instruction);
    uint32_t rs = instr_s(instruction);
    uint32_t address = cpu_reg(cpu, rs) + offset;
    uint32_t value = cpu_reg(cpu, rt); // Use input register set
    interconnect_store32(cpu->inter, address, value); // Alignment checked in interconnect
}

void op_sll(Cpu* cpu, uint32_t instruction) {
    // NOP is SLL R0, R0, 0. Check for it to avoid calculation.
    if (instruction == 0) return; // Common NOP
    uint32_t shamt = instr_shift(instruction);
    uint32_t rt = instr_t(instruction);
    uint32_t rd = instr_d(instruction);
    cpu_set_reg(cpu, rd, cpu_reg(cpu, rt) << shamt);
}

void op_addiu(Cpu* cpu, uint32_t instruction) {
    uint32_t imm_se = instr_imm_se(instruction);
    uint32_t rt = instr_t(instruction);
    uint32_t rs = instr_s(instruction);
    cpu_set_reg(cpu, rt, cpu_reg(cpu, rs) + imm_se); // Unsigned addition wraps naturally
}

void op_j(Cpu* cpu, uint32_t instruction) {
    uint32_t target_imm = instr_imm_jump(instruction);
    // Combine upper 4 bits of current PC+4 with target
    cpu->next_pc = (cpu->current_pc & 0xF0000000) | (target_imm << 2);
    cpu->branch_taken = true;
}

void op_or(Cpu* cpu, uint32_t instruction) {
    uint32_t rd = instr_d(instruction);
    uint32_t rs = instr_s(instruction);
    uint32_t rt = instr_t(instruction);
    cpu_set_reg(cpu, rd, cpu_reg(cpu, rs) | cpu_reg(cpu, rt));
}

void op_cop0(Cpu* cpu, uint32_t instruction) {
    uint32_t cop_opcode = instr_cop_opcode(instruction); // Bits 25:21 specify COP0 op
    switch (cop_opcode) {
        case 0b00000: op_mfc0(cpu, instruction); break; // MFC0
        case 0b00100: op_mtc0(cpu, instruction); break; // MTC0
        case 0b10000: // Check subfunction for RFE
            if ((instruction & 0x3f) == 0b010000) {
                op_rfe(cpu, instruction); // RFE
            } else {
                op_illegal(cpu, instruction); // Other TLB/etc. instructions
            }
            break;
        default:
             LOG_WARN("Warning: Unhandled COP0 instruction: 0x%08x (CopOp=%u) at PC=0x%08x\n", instruction, cop_opcode, cpu->current_pc);
             cpu_exception(cpu, EXCEPTION_ILLEGAL_INSTRUCTION); // Or maybe COPROCESSOR_ERROR? Illegal seems better.
            break;
    }
}

void op_mtc0(Cpu* cpu, uint32_t instruction) {
    uint32_t cpu_r = instr_t(instruction); // Source CPU register
    uint32_t cop_r = instr_d(instruction); // Destination COP0 register
    uint32_t value = cpu_reg(cpu, cpu_r);

    switch (cop_r) {
        case 3: case 5: case 6: case 7: case 9: case 11: // Breakpoint/DCIC regs
             if (value != 0) LOG_WARN("Warning: MTC0 to unhandled Breakpoint/DCIC Reg %u = 0x%08x at PC=0x%08x\n", cop_r, value, cpu->current_pc);
             // No state change for now
             break;
        case 12: // SR (Status Register)
            // printf("~ MTC0 SR = 0x%08x\n", value); // Debug
            cpu->sr = value;
            break;
        case 13: // CAUSE
             // Only bits 8 and 9 (IP0, IP1) seem writable to force software interrupts.
             // Mask other bits.
             cpu->cause = (cpu->cause & ~0x300) | (value & 0x300);
             if ((value & ~0x300) != 0) {
                 LOG_WARN("Warning: MTC0 to CAUSE attempting to write non-SW bits: 0x%08x at PC=0x%08x\n", value, cpu->current_pc);
             }
             break;
        // EPC (Reg 14) is read-only. Other registers are typically MMU-related or unused.
        default:
            LOG_WARN("Warning: MTC0 to unhandled/read-only COP0 Register %u = 0x%08x at PC=0x%08x\n", cop_r, value, cpu->current_pc);
            break;
    }
}

void op_rfe(Cpu* cpu, uint32_t instruction) {
    uint32_t old_sr = cpu->sr;
    uint32_t old_pc = cpu->pc;
    uint32_t old_epc = cpu->epc;
    uint32_t mode_stack = cpu->sr & 0x3f;
    cpu->sr &= ~0x3f;
    cpu->sr |= (mode_stack >> 2) & 0x3f; // Following guide's code
    LOG_CPU_IMPORTANT("@PSX-Spex RFE: Executed, SR before=0x%08x after=0x%08x PC=0x%08x EPC=0x%08x", old_sr, cpu->sr, old_pc, old_epc);
    // NOTE: Do NOT jump to EPC here! The BIOS handler must do the jump after RFE.
}

void op_bne(Cpu* cpu, uint32_t instruction) {
    uint32_t imm_se = instr_imm_se(instruction);
    uint32_t rs = instr_s(instruction);
    uint32_t rt = instr_t(instruction);
    if (cpu_reg(cpu, rs) != cpu_reg(cpu, rt)) {
        cpu_branch(cpu, imm_se);
        cpu->branch_taken = true;
    }
}

void op_addi(Cpu* cpu, uint32_t instruction) {
    int32_t imm_se = (int32_t)instr_imm_se(instruction);
    uint32_t rt = instr_t(instruction);
    uint32_t rs = instr_s(instruction);
    int32_t rs_value = (int32_t)cpu_reg(cpu, rs);
    int32_t result;
    // Use GCC/Clang builtin for checked signed addition
    if (__builtin_add_overflow(rs_value, imm_se, &result)) {
        LOG_ERROR("ADDI Signed Overflow: %d + %d (PC=0x%08x)\n", rs_value, imm_se, cpu->current_pc);
        cpu_exception(cpu, EXCEPTION_OVERFLOW); // Trigger overflow exception
    } else {
        cpu_set_reg(cpu, rt, (uint32_t)result);
    }
}

void op_lw(Cpu* cpu, uint32_t instruction) {
    if ((cpu->sr & 0x10000) != 0) { // Check cache isolation
        LOG_DEBUG("~ LW Ignored (Cache Isolated, SR=0x%08x)\n", cpu->sr); // Keep debug print
        return;
    }
    uint32_t offset = instr_imm_se(instruction);
    uint32_t rt = instr_t(instruction);
    uint32_t rs = instr_s(instruction);
    uint32_t address = cpu_reg(cpu, rs) + offset;

    // Perform load and schedule it for the delay slot
    uint32_t value_loaded = interconnect_load32(cpu->inter, address); // Alignment checked in interconnect
    cpu->load_reg_idx = rt;
    cpu->load_value = value_loaded;
}

void op_sltu(Cpu* cpu, uint32_t instruction) {
    uint32_t rd = instr_d(instruction);
    uint32_t rs = instr_s(instruction);
    uint32_t rt = instr_t(instruction);
    cpu_set_reg(cpu, rd, (cpu_reg(cpu, rs) < cpu_reg(cpu, rt)) ? 1 : 0);
}

void op_addu(Cpu* cpu, uint32_t instruction) {
    uint32_t rd = instr_d(instruction);
    uint32_t rs = instr_s(instruction);
    uint32_t rt = instr_t(instruction);
    cpu_set_reg(cpu, rd, cpu_reg(cpu, rs) + cpu_reg(cpu, rt));
}

void op_sh(Cpu* cpu, uint32_t instruction) {
    if ((cpu->sr & 0x10000) != 0) {
        LOG_DEBUG("~ SH Ignored (Cache Isolated, SR=0x%08x)\n", cpu->sr); // Keep debug print
        return;
    }
    uint32_t offset = instr_imm_se(instruction);
    uint32_t rt = instr_t(instruction);
    uint32_t rs = instr_s(instruction);
    uint32_t address = cpu_reg(cpu, rs) + offset;
    uint16_t value = (uint16_t)cpu_reg(cpu, rt); // Lower 16 bits of rt
    interconnect_store16(cpu->inter, address, value); // Alignment checked in interconnect
}

void op_jal(Cpu* cpu, uint32_t instruction) {
    cpu_set_reg(cpu, REG_RA, cpu->pc + 4); // Link Register $31 gets PC+8 (address after delay slot)
    uint32_t target_imm = instr_imm_jump(instruction);
    cpu->next_pc = (cpu->current_pc & 0xF0000000) | (target_imm << 2); // Same target calculation as J
    cpu->branch_taken = true;
}

void op_andi(Cpu* cpu, uint32_t instruction) {
    uint32_t imm = instr_imm(instruction); // Zero-extended immediate
    uint32_t rt = instr_t(instruction);
    uint32_t rs = instr_s(instruction);
    cpu_set_reg(cpu, rt, cpu_reg(cpu, rs) & imm);
}

void op_sb(Cpu* cpu, uint32_t instruction) {
    if ((cpu->sr & 0x10000) != 0) {
        LOG_DEBUG("~ SB Ignored (Cache Isolated, SR=0x%08x)\n", cpu->sr); // Keep debug print
        return;
    }
    uint32_t offset = instr_imm_se(instruction);
    uint32_t rt = instr_t(instruction);
    uint32_t rs = instr_s(instruction);
    uint32_t address = cpu_reg(cpu, rs) + offset;
    uint8_t value = (uint8_t)cpu_reg(cpu, rt); // Lower 8 bits of rt
    interconnect_store8(cpu->inter, address, value);
}

void op_jr(Cpu* cpu, uint32_t instruction) {
    uint32_t rs = instr_s(instruction);
    uint32_t target_address = cpu_reg(cpu, rs);
    cpu->next_pc = target_address;
    cpu->branch_taken = true;
    // Alignment check will happen on fetch in the next cycle
}

void op_lb(Cpu* cpu, uint32_t instruction) {
    if ((cpu->sr & 0x10000) != 0) {
        LOG_DEBUG("~ LB Ignored (Cache Isolated, SR=0x%08x)\n", cpu->sr); // Keep debug print
        return;
    }
    uint32_t offset = instr_imm_se(instruction);
    uint32_t rt = instr_t(instruction);
    uint32_t rs = instr_s(instruction);
    uint32_t address = cpu_reg(cpu, rs) + offset;
    uint8_t value_loaded = interconnect_load8(cpu->inter, address);
    // Sign-extend the 8-bit value to 32 bits
    uint32_t value_sign_extended = (uint32_t)(int32_t)(int8_t)value_loaded;
    // Schedule load for delay slot
    cpu->load_reg_idx = rt;
    cpu->load_value = value_sign_extended;
}

void op_beq(Cpu* cpu, uint32_t instruction) {
    uint32_t imm_se = instr_imm_se(instruction);
    uint32_t rs = instr_s(instruction);
    uint32_t rt = instr_t(instruction);
    if (cpu_reg(cpu, rs) == cpu_reg(cpu, rt)) {
        cpu_branch(cpu, imm_se);
        cpu->branch_taken = true;
    }
}

void op_mfc0(Cpu* cpu, uint32_t instruction) {
    uint32_t cpu_r_dest = instr_t(instruction); // Target CPU register
    uint32_t cop_r_src = instr_d(instruction);  // Source COP0 register
    uint32_t value_read = 0; // Default value if read fails or is unhandled

    switch (cop_r_src) {
        case 12: value_read = cpu->sr; break; // SR
        case 13: value_read = cpu->cause; break; // CAUSE
        case 14: value_read = cpu->epc; break; // EPC
        // Add reads for other COP0 registers if needed (mostly MMU/debug related)
        default:
            LOG_WARN("Warning: MFC0 read from unhandled COP0 Register %u (PC=0x%08x)\n", cop_r_src, cpu->current_pc);
            // Should it trigger an exception? Probably not, just return garbage/0.
            break;
    }
    // Schedule load for delay slot
    cpu->load_reg_idx = cpu_r_dest;
    cpu->load_value = value_read;
}

void op_and(Cpu* cpu, uint32_t instruction) {
    uint32_t rd = instr_d(instruction);
    uint32_t rs = instr_s(instruction);
    uint32_t rt = instr_t(instruction);
    cpu_set_reg(cpu, rd, cpu_reg(cpu, rs) & cpu_reg(cpu, rt));
}

void op_add(Cpu* cpu, uint32_t instruction) {
    uint32_t rd = instr_d(instruction);
    uint32_t rs = instr_s(instruction);
    uint32_t rt = instr_t(instruction);
    int32_t rs_value = (int32_t)cpu_reg(cpu, rs);
    int32_t rt_value = (int32_t)cpu_reg(cpu, rt);
    int32_t result;
    // Use GCC/Clang builtin for checked signed addition
    if (__builtin_add_overflow(rs_value, rt_value, &result)) {
        LOG_ERROR("ADD Signed Overflow: %d + %d (PC=0x%08x)\n", rs_value, rt_value, cpu->current_pc);
        cpu_exception(cpu, EXCEPTION_OVERFLOW); // Trigger overflow exception
    } else {
        cpu_set_reg(cpu, rd, (uint32_t)result);
    }
}

void op_bgtz(Cpu* cpu, uint32_t instruction) {
    uint32_t imm_se = instr_imm_se(instruction);
    uint32_t rs = instr_s(instruction);
    // Comparison is signed
    if ((int32_t)cpu_reg(cpu, rs) > 0) {
        cpu_branch(cpu, imm_se);
        cpu->branch_taken = true;
    }
}

void op_blez(Cpu* cpu, uint32_t instruction) {
    uint32_t imm_se = instr_imm_se(instruction);
    uint32_t rs = instr_s(instruction);
    // Comparison is signed
    if ((int32_t)cpu_reg(cpu, rs) <= 0) {
        cpu_branch(cpu, imm_se);
        cpu->branch_taken = true;
    }
}

void op_lbu(Cpu* cpu, uint32_t instruction) {
     if ((cpu->sr & 0x10000) != 0) {
        LOG_DEBUG("~ LBU Ignored (Cache Isolated, SR=0x%08x)\n", cpu->sr); // Keep debug print
        return;
    }
    uint32_t offset = instr_imm_se(instruction);
    uint32_t rt = instr_t(instruction);
    uint32_t rs = instr_s(instruction);
    uint32_t address = cpu_reg(cpu, rs) + offset;
    uint8_t value_loaded = interconnect_load8(cpu->inter, address);
    // Zero-extend the 8-bit value to 32 bits
    uint32_t value_zero_extended = (uint32_t)value_loaded;
    // Schedule load for delay slot
    cpu->load_reg_idx = rt;
    cpu->load_value = value_zero_extended;
}

void op_jalr(Cpu* cpu, uint32_t instruction) {
    uint32_t rs = instr_s(instruction); // Register containing target address
    uint32_t rd = instr_d(instruction); // Register to store return address (defaults to $ra=31 if rd=0?)
    uint32_t target_address = cpu_reg(cpu, rs);
    uint32_t return_addr = cpu->pc + 4; // Address of instruction after delay slot

    // Store return address in rd
    cpu_set_reg(cpu, rd, return_addr);
    // Set jump target
    cpu->next_pc = target_address;
    cpu->branch_taken = true;
    // Alignment check will happen on fetch in the next cycle
}

// Handles BGEZ, BLTZ, BGEZAL, BLTZAL based on bits 20 and 16
void op_bxx(Cpu* cpu, uint32_t instruction) {
    uint32_t imm_se = instr_imm_se(instruction);
    uint32_t rs = instr_s(instruction);
    int is_bgez = (instruction >> 16) & 1; // Bit 16: 1=BGEZ, 0=BLTZ
    int is_link = (instruction >> 20) & 1; // Bit 20: 1=Link (BGEZAL/BLTZAL)
    int32_t rs_value = (int32_t)cpu_reg(cpu, rs);

    // Determine condition met
    bool condition_met;
    if (is_bgez) { // BGEZ or BGEZAL
        condition_met = (rs_value >= 0);
    } else { // BLTZ or BLTZAL
        condition_met = (rs_value < 0);
    }

    if (condition_met) {
        // Link if necessary (store PC+8 in $ra)
        if (is_link) {
            cpu_set_reg(cpu, REG_RA, cpu->pc + 4); //
        }
        // Perform the branch
        cpu_branch(cpu, imm_se);
        cpu->branch_taken = true;
    }
}

void op_slti(Cpu* cpu, uint32_t instruction) {
    int32_t imm_se = (int32_t)instr_imm_se(instruction); // Immediate is signed
    uint32_t rt = instr_t(instruction);
    uint32_t rs = instr_s(instruction);
    // Comparison is signed
    cpu_set_reg(cpu, rt, ((int32_t)cpu_reg(cpu, rs) < imm_se) ? 1 : 0);
}

void op_subu(Cpu* cpu, uint32_t instruction) {
    uint32_t rd = instr_d(instruction);
    uint32_t rs = instr_s(instruction);
    uint32_t rt = instr_t(instruction);
    cpu_set_reg(cpu, rd, cpu_reg(cpu, rs) - cpu_reg(cpu, rt)); // Unsigned wraps
}

void op_sra(Cpu* cpu, uint32_t instruction) {
    uint32_t shamt = instr_shift(instruction);
    uint32_t rt = instr_t(instruction);
    uint32_t rd = instr_d(instruction);
    int32_t value_signed = (int32_t)cpu_reg(cpu, rt);
    // Arithmetic shift preserves sign bit
    cpu_set_reg(cpu, rd, (uint32_t)(value_signed >> shamt));
}

// Signed division
void op_div(Cpu* cpu, uint32_t instruction) {
    uint32_t rs = instr_s(instruction);
    uint32_t rt = instr_t(instruction);
    int32_t n = (int32_t)cpu_reg(cpu, rs); // Numerator
    int32_t d = (int32_t)cpu_reg(cpu, rt); // Denominator

    // Handle special cases according to MIPS spec / Guide Table 7
    if (d == 0) { // Division by zero
        cpu->hi = (uint32_t)n;
        cpu->lo = (n >= 0) ? 0xffffffff : 1;
    } else if ((uint32_t)n == 0x80000000 && d == -1) { // Overflow case: MinInt / -1
        cpu->hi = 0;
        cpu->lo = 0x80000000; // Result is MinInt
    } else { // Normal division
        cpu->lo = (uint32_t)(n / d); // Quotient
        cpu->hi = (uint32_t)(n % d); // Remainder
    }
    // Note: Division takes many cycles; result isn't available immediately.
    // We ignore timing for now. HI/LO access should stall if op not finished.
}

// Unsigned division
void op_divu(Cpu* cpu, uint32_t instruction) {
    uint32_t rs = instr_s(instruction);
    uint32_t rt = instr_t(instruction);
    uint32_t n = cpu_reg(cpu, rs);
    uint32_t d = cpu_reg(cpu, rt);

    if (d == 0) { // Division by zero
        cpu->hi = n;          // Remainder is numerator
        cpu->lo = 0xffffffff; // Quotient is -1
    } else { // Normal division
        cpu->lo = n / d; // Quotient
        cpu->hi = n % d; // Remainder
    }
    // Ignore timing stall for now.
}

// Move From LO
void op_mflo(Cpu* cpu, uint32_t instruction) {
    uint32_t rd = instr_d(instruction);
    cpu_set_reg(cpu, rd, cpu->lo); //
    // TODO: Should stall if previous DIV/MULT not finished.
}

// Shift Right Logical
void op_srl(Cpu* cpu, uint32_t instruction) {
    uint32_t shamt = instr_shift(instruction);
    uint32_t rt = instr_t(instruction);
    uint32_t rd = instr_d(instruction);
    // Logical shift fills with zeros
    cpu_set_reg(cpu, rd, cpu_reg(cpu, rt) >> shamt);
}

// Set if Less Than Immediate Unsigned
void op_sltiu(Cpu* cpu, uint32_t instruction) {
    uint32_t imm_se = instr_imm_se(instruction); // Immediate is sign-extended
    uint32_t rt = instr_t(instruction);
    uint32_t rs = instr_s(instruction);
    // Comparison is unsigned
    cpu_set_reg(cpu, rt, (cpu_reg(cpu, rs) < imm_se) ? 1 : 0);
}

// Set on Less Than (Signed)
void op_slt(Cpu* cpu, uint32_t instruction) {
    uint32_t rd = instr_d(instruction);
    uint32_t rs = instr_s(instruction);
    uint32_t rt = instr_t(instruction);
    // Comparison is signed
    cpu_set_reg(cpu, rd, ((int32_t)cpu_reg(cpu, rs) < (int32_t)cpu_reg(cpu, rt)) ? 1 : 0);
}

// Move From HI
void op_mfhi(Cpu* cpu, uint32_t instruction) {
    uint32_t rd = instr_d(instruction);
    cpu_set_reg(cpu, rd, cpu->hi); //
    // TODO: Should stall if previous DIV/MULT not finished.
}

// System Call
void op_syscall(Cpu* cpu, uint32_t instruction) {
    (void)instruction;
    // Get the syscall number from register $a0
    uint32_t syscall_num = cpu_reg(cpu, 4); 

    // Attempt to handle it directly
    bool was_handled = handle_bios_syscall(cpu, syscall_num);

    // If the handler returned false, it means we don't have this
    // syscall implemented yet. In that case, trigger a full exception
    // so we can see it in the logs and debug it.
    if (!was_handled) {
        LOG_ERROR("Unhandled BIOS Syscall: 0x%02x, triggering full exception.\n", syscall_num);
        cpu_exception(cpu, EXCEPTION_SYSCALL);
        return; // <--- Ensure we do not continue executing after exception
    }
    // If it was handled, we do nothing and simply proceed to the next instruction.
}

// Bitwise Not Or
void op_nor(Cpu* cpu, uint32_t instruction) {
    uint32_t rd = instr_d(instruction);
    uint32_t rs = instr_s(instruction);
    uint32_t rt = instr_t(instruction);
    cpu_set_reg(cpu, rd, ~(cpu_reg(cpu, rs) | cpu_reg(cpu, rt))); //
}

// Move To LO
void op_mtlo(Cpu* cpu, uint32_t instruction) {
    uint32_t rs = instr_s(instruction);
    cpu->lo = cpu_reg(cpu, rs); //
    // TODO: Writing HI/LO can interfere with ongoing DIV/MULT. Ignored for now.
}

// Move To HI
void op_mthi(Cpu* cpu, uint32_t instruction) {
    uint32_t rs = instr_s(instruction);
    cpu->hi = cpu_reg(cpu, rs); //
    // TODO: Timing/interlock implications ignored.
}

// Load Halfword Unsigned
void op_lhu(Cpu* cpu, uint32_t instruction) {
     if ((cpu->sr & 0x10000) != 0) {
        LOG_DEBUG("~ LHU Ignored (Cache Isolated, SR=0x%08x)\n", cpu->sr); // Keep debug print
        return;
    }
    uint32_t offset = instr_imm_se(instruction);
    uint32_t rt = instr_t(instruction);
    uint32_t rs = instr_s(instruction);
    uint32_t address = cpu_reg(cpu, rs) + offset;
    uint16_t value_loaded = interconnect_load16(cpu->inter, address); // Alignment checked in interconnect
    // Zero-extend the 16-bit value
    uint32_t value_zero_extended = (uint32_t)value_loaded;
    // Schedule load for delay slot
    cpu->load_reg_idx = rt;
    cpu->load_value = value_zero_extended;
}

// Load Halfword (Signed)
void op_lh(Cpu* cpu, uint32_t instruction) {
    if ((cpu->sr & 0x10000) != 0) {
        LOG_DEBUG("~ LH Ignored (Cache Isolated, SR=0x%08x)\n", cpu->sr); // Keep debug print
        return;
    }
    uint32_t offset = instr_imm_se(instruction);
    uint32_t rt = instr_t(instruction);
    uint32_t rs = instr_s(instruction);
    uint32_t address = cpu_reg(cpu, rs) + offset;
    uint16_t value_loaded = interconnect_load16(cpu->inter, address); // Alignment checked in interconnect
    // Sign-extend the 16-bit value
    uint32_t value_sign_extended = (uint32_t)(int32_t)(int16_t)value_loaded;
    // Schedule load for delay slot
    cpu->load_reg_idx = rt;
    cpu->load_value = value_sign_extended;
}

// Shift Left Logical Variable
void op_sllv(Cpu* cpu, uint32_t instruction) {
    uint32_t rd = instr_d(instruction);
    uint32_t rs = instr_s(instruction); // Register containing shift amount
    uint32_t rt = instr_t(instruction); // Register to shift
    // Shift amount uses only lower 5 bits
    uint32_t shift_amount = cpu_reg(cpu, rs) & 0x1F;
    cpu_set_reg(cpu, rd, cpu_reg(cpu, rt) << shift_amount);
}

// Shift Right Arithmetic Variable
void op_srav(Cpu* cpu, uint32_t instruction) {
    uint32_t rd = instr_d(instruction);
    uint32_t rs = instr_s(instruction); // Register containing shift amount
    uint32_t rt = instr_t(instruction); // Register to shift
    uint32_t shift_amount = cpu_reg(cpu, rs) & 0x1F; // Lower 5 bits
    int32_t value_signed = (int32_t)cpu_reg(cpu, rt);
    // Arithmetic shift
    cpu_set_reg(cpu, rd, (uint32_t)(value_signed >> shift_amount));
}

// Shift Right Logical Variable
void op_srlv(Cpu* cpu, uint32_t instruction) {
    uint32_t rd = instr_d(instruction);
    uint32_t rs = instr_s(instruction); // Register containing shift amount
    uint32_t rt = instr_t(instruction); // Register to shift
    uint32_t shift_amount = cpu_reg(cpu, rs) & 0x1F; // Lower 5 bits
    // Logical shift
    cpu_set_reg(cpu, rd, cpu_reg(cpu, rt) >> shift_amount);
}

// Multiply Unsigned
void op_multu(Cpu* cpu, uint32_t instruction) {
    uint32_t rs = instr_s(instruction);
    uint32_t rt = instr_t(instruction);
    // Perform 64-bit multiplication
    uint64_t val_rs = (uint64_t)cpu_reg(cpu, rs);
    uint64_t val_rt = (uint64_t)cpu_reg(cpu, rt);
    uint64_t result_64 = val_rs * val_rt;
    // Store result in HI/LO
    cpu->hi = (uint32_t)(result_64 >> 32);  //
    cpu->lo = (uint32_t)(result_64 & 0xFFFFFFFF); //
    // Ignore timing stall for now
}

// Bitwise Exclusive Or
void op_xor(Cpu* cpu, uint32_t instruction) {
     uint32_t rd = instr_d(instruction);
    uint32_t rs = instr_s(instruction);
    uint32_t rt = instr_t(instruction);
    cpu_set_reg(cpu, rd, cpu_reg(cpu, rs) ^ cpu_reg(cpu, rt));
}

// Breakpoint
void op_break(Cpu* cpu, uint32_t instruction) {
    (void)instruction;
    LOG_CPU_IMPORTANT("@nocash BREAK OPCODE: EPC=0x%08x PC=0x%08x");
    cpu_exception(cpu, EXCEPTION_BREAK); //
}

// Multiply (Signed)
void op_mult(Cpu* cpu, uint32_t instruction) {
    uint32_t rs = instr_s(instruction);
    uint32_t rt = instr_t(instruction);
    // Perform 64-bit signed multiplication
    int64_t val_rs_s = (int64_t)(int32_t)cpu_reg(cpu, rs);
    int64_t val_rt_s = (int64_t)(int32_t)cpu_reg(cpu, rt);
    int64_t result_s64 = val_rs_s * val_rt_s;
    // Store result in HI/LO
    cpu->hi = (uint32_t)((uint64_t)result_s64 >> 32); //
    cpu->lo = (uint32_t)((uint64_t)result_s64 & 0xFFFFFFFF); //
    // Ignore timing stall
}

// Subtract (Signed, with Overflow check)
void op_sub(Cpu* cpu, uint32_t instruction) {
    uint32_t rd = instr_d(instruction);
    uint32_t rs = instr_s(instruction);
    uint32_t rt = instr_t(instruction);
    int32_t rs_value = (int32_t)cpu_reg(cpu, rs);
    int32_t rt_value = (int32_t)cpu_reg(cpu, rt);
    int32_t result;
    // Use GCC/Clang builtin for checked signed subtraction
    if (__builtin_sub_overflow(rs_value, rt_value, &result)) {
        LOG_ERROR("SUB Signed Overflow: %d - %d (PC=0x%08x)\n", rs_value, rt_value, cpu->current_pc);
        cpu_exception(cpu, EXCEPTION_OVERFLOW); //
    } else {
        cpu_set_reg(cpu, rd, (uint32_t)result);
    }
}

// Bitwise Exclusive Or Immediate
void op_xori(Cpu* cpu, uint32_t instruction) {
    uint32_t imm = instr_imm(instruction); // Zero-extended immediate
    uint32_t rt = instr_t(instruction);
    uint32_t rs = instr_s(instruction);
    cpu_set_reg(cpu, rt, cpu_reg(cpu, rs) ^ imm);
}

// Coprocessor 1 (FPU) Opcode - Triggers exception
void op_cop1(Cpu* cpu, uint32_t instruction) {
    LOG_WARN("Warning: Unsupported COP1 (FPU) instruction: 0x%08x (PC=0x%08x)\n", instruction, cpu->current_pc);
    cpu_exception(cpu, EXCEPTION_COPROCESSOR_ERROR); //
}

// Coprocessor 2 (GTE) Opcode - Currently unimplemented
void op_cop2(Cpu* cpu, uint32_t instruction) {
    LOG_TRACE("GTE: Executing instruction 0x%08x (PC=0x%08x)\n", instruction, cpu->current_pc);
    
    // Execute the GTE instruction
    uint32_t cycles = gte_execute_instruction(&cpu->gte, instruction);
    
    // TODO: Handle GTE busy state and timing if needed
    // For now, we'll just continue execution
}

// Coprocessor 3 Opcode - Triggers exception
void op_cop3(Cpu* cpu, uint32_t instruction) {
    LOG_WARN("Warning: Unsupported COP3 instruction: 0x%08x (PC=0x%08x)\n", instruction, cpu->current_pc);
    cpu_exception(cpu, EXCEPTION_COPROCESSOR_ERROR); //
}

// Load Word Left (Handles unaligned loads)
void op_lwl(Cpu* cpu, uint32_t instruction) {
    if ((cpu->sr & 0x10000) != 0) {
        LOG_DEBUG("~ LWL Ignored (Cache Isolated, SR=0x%08x)\n", cpu->sr); return;
    }
    uint32_t offset = instr_imm_se(instruction);
    uint32_t rt = instr_t(instruction);
    uint32_t rs = instr_s(instruction);
    uint32_t addr = cpu_reg(cpu, rs) + offset;

    // Merge with pending load value if target register matches
    uint32_t current_rt_value = (cpu->load_reg_idx == rt) ? cpu->load_value : cpu->out_regs[rt];

    uint32_t aligned_addr = addr & ~3;
    uint32_t aligned_word = interconnect_load32(cpu->inter, aligned_addr);
    uint32_t merged_value;

    // Shift and mask based on address alignment (Little Endian)
    switch (addr & 3) {
        case 0: merged_value = (current_rt_value & 0x00FFFFFF) | (aligned_word << 24); break;
        case 1: merged_value = (current_rt_value & 0x0000FFFF) | (aligned_word << 16); break;
        case 2: merged_value = (current_rt_value & 0x000000FF) | (aligned_word << 8);  break;
        case 3: merged_value = (current_rt_value & 0x00000000) | (aligned_word << 0);  break;
        default: merged_value = 0; /* Should not happen */ break;
    }
    // Schedule merged value for load delay slot
    cpu->load_reg_idx = rt;
    cpu->load_value = merged_value;
}

// Load Word Right (Handles unaligned loads)
void op_lwr(Cpu* cpu, uint32_t instruction) {
     if ((cpu->sr & 0x10000) != 0) {
        LOG_DEBUG("~ LWR Ignored (Cache Isolated, SR=0x%08x)\n", cpu->sr); return;
    }
    uint32_t offset = instr_imm_se(instruction);
    uint32_t rt = instr_t(instruction);
    uint32_t rs = instr_s(instruction);
    uint32_t addr = cpu_reg(cpu, rs) + offset;

    // Merge with pending load value if target register matches
    uint32_t current_rt_value = (cpu->load_reg_idx == rt) ? cpu->load_value : cpu->out_regs[rt];

    uint32_t aligned_addr = addr & ~3;
    uint32_t aligned_word = interconnect_load32(cpu->inter, aligned_addr);
    uint32_t merged_value;

    // Shift and mask based on address alignment (Little Endian)
    switch (addr & 3) {
        case 0: merged_value = (current_rt_value & 0x00000000) | (aligned_word >> 0);  break;
        case 1: merged_value = (current_rt_value & 0xFF000000) | (aligned_word >> 8);  break;
        case 2: merged_value = (current_rt_value & 0xFFFF0000) | (aligned_word >> 16); break;
        case 3: merged_value = (current_rt_value & 0xFFFFFF00) | (aligned_word >> 24); break;
        default: merged_value = 0; /* Should not happen */ break;
    }
    // Schedule merged value for load delay slot
    cpu->load_reg_idx = rt;
    cpu->load_value = merged_value;
}

// Store Word Left (Handles unaligned stores)
void op_swl(Cpu* cpu, uint32_t instruction) {
     if ((cpu->sr & 0x10000) != 0) {
        LOG_DEBUG("~ SWL Ignored (Cache Isolated, SR=0x%08x)\n", cpu->sr); return;
    }
    uint32_t offset = instr_imm_se(instruction);
    uint32_t rt = instr_t(instruction); // Register containing data to store
    uint32_t rs = instr_s(instruction); // Register containing base address
    uint32_t addr = cpu_reg(cpu, rs) + offset;
    uint32_t value_to_store = cpu_reg(cpu, rt); // Use input register set value

    uint32_t aligned_addr = addr & ~3;
    // Read-Modify-Write the aligned word in memory
    uint32_t current_mem_word = interconnect_load32(cpu->inter, aligned_addr);
    uint32_t modified_mem_word;

    // Shift and mask based on address alignment (Little Endian)
    switch (addr & 3) {
        case 0: modified_mem_word = (current_mem_word & 0xFFFFFF00) | (value_to_store >> 24); break;
        case 1: modified_mem_word = (current_mem_word & 0xFFFF0000) | (value_to_store >> 16); break;
        case 2: modified_mem_word = (current_mem_word & 0xFF000000) | (value_to_store >> 8);  break;
        case 3: modified_mem_word = (current_mem_word & 0x00000000) | (value_to_store >> 0);  break;
        default: modified_mem_word = current_mem_word; /* Should not happen */ break;
    }
    interconnect_store32(cpu->inter, aligned_addr, modified_mem_word);
}

// Store Word Right (Handles unaligned stores)
void op_swr(Cpu* cpu, uint32_t instruction) {
    if ((cpu->sr & 0x10000) != 0) {
        LOG_DEBUG("~ SWR Ignored (Cache Isolated, SR=0x%08x)\n", cpu->sr); return;
    }
    uint32_t offset = instr_imm_se(instruction);
    uint32_t rt = instr_t(instruction); // Register containing data to store
    uint32_t rs = instr_s(instruction); // Register containing base address
    uint32_t addr = cpu_reg(cpu, rs) + offset;
    uint32_t value_to_store = cpu_reg(cpu, rt); // Use input register set value

    uint32_t aligned_addr = addr & ~3;
    // Read-Modify-Write
    uint32_t current_mem_word = interconnect_load32(cpu->inter, aligned_addr);
    uint32_t modified_mem_word;

    // Shift and mask based on address alignment (Little Endian)
    switch (addr & 3) {
        case 0: modified_mem_word = (current_mem_word & 0x00000000) | (value_to_store << 0);  break;
        case 1: modified_mem_word = (current_mem_word & 0x000000FF) | (value_to_store << 8);  break;
        case 2: modified_mem_word = (current_mem_word & 0x0000FFFF) | (value_to_store << 16); break;
        case 3: modified_mem_word = (current_mem_word & 0x00FFFFFF) | (value_to_store << 24); break;
        default: modified_mem_word = current_mem_word; /* Should not happen */ break;
    }
    interconnect_store32(cpu->inter, aligned_addr, modified_mem_word);
}

// Load Word Coprocessor 0 - Not supported
void op_lwc0(Cpu* cpu, uint32_t instruction) {
    LOG_WARN("Warning: Unsupported LWC0 instruction: 0x%08x (PC=0x%08x)\n", instruction, cpu->current_pc);
    cpu_exception(cpu, EXCEPTION_COPROCESSOR_ERROR); //
}

// Load Word Coprocessor 1 (FPU) - Not supported
void op_lwc1(Cpu* cpu, uint32_t instruction) {
    LOG_WARN("Warning: Unsupported LWC1 instruction: 0x%08x (PC=0x%08x)\n", instruction, cpu->current_pc);
    cpu_exception(cpu, EXCEPTION_COPROCESSOR_ERROR); //
}

// Load Word Coprocessor 2 (GTE) - Unimplemented
void op_lwc2(Cpu* cpu, uint32_t instruction) {
    uint32_t cpu_r_dest = instr_t(instruction); // Target CPU register
    uint32_t gte_r_src = instr_d(instruction);  // Source GTE register
    uint32_t value_read = gte_read_data_register(&cpu->gte, gte_r_src);
    
    // Schedule load for delay slot
    cpu->load_reg_idx = cpu_r_dest;
    cpu->load_value = value_read;
}

// Load Word Coprocessor 3 - Not supported
void op_lwc3(Cpu* cpu, uint32_t instruction) {
    LOG_WARN("Warning: Unsupported LWC3 instruction: 0x%08x (PC=0x%08x)\n", instruction, cpu->current_pc);
    cpu_exception(cpu, EXCEPTION_COPROCESSOR_ERROR); //
}

// Store Word Coprocessor 0 - Not supported
void op_swc0(Cpu* cpu, uint32_t instruction) {
    LOG_WARN("Warning: Unsupported SWC0 instruction: 0x%08x (PC=0x%08x)\n", instruction, cpu->current_pc);
    cpu_exception(cpu, EXCEPTION_COPROCESSOR_ERROR); //
}

// Store Word Coprocessor 1 (FPU) - Not supported
void op_swc1(Cpu* cpu, uint32_t instruction) {
    LOG_WARN("Warning: Unsupported SWC1 instruction: 0x%08x (PC=0x%08x)\n", instruction, cpu->current_pc);
    cpu_exception(cpu, EXCEPTION_COPROCESSOR_ERROR); //
}

// Store Word Coprocessor 2 (GTE) - Unimplemented
void op_swc2(Cpu* cpu, uint32_t instruction) {
    uint32_t cpu_r_src = instr_t(instruction); // Source CPU register
    uint32_t gte_r_dest = instr_d(instruction); // Target GTE register
    uint32_t value = cpu_reg(cpu, cpu_r_src);
    
    gte_write_data_register(&cpu->gte, gte_r_dest, value);
}

// Store Word Coprocessor 3 - Not supported
void op_swc3(Cpu* cpu, uint32_t instruction) {
    LOG_WARN("Warning: Unsupported SWC3 instruction: 0x%08x (PC=0x%08x)\n", instruction, cpu->current_pc);
    cpu_exception(cpu, EXCEPTION_COPROCESSOR_ERROR); //
}

// Illegal/Unhandled Instruction Handler
void op_illegal(Cpu* cpu, uint32_t instruction) {
    LOG_ERROR("Error: Illegal/Unhandled instruction 0x%08x encountered at PC=0x%08x\n", instruction, cpu->current_pc);
    cpu_exception(cpu, EXCEPTION_ILLEGAL_INSTRUCTION); //
}