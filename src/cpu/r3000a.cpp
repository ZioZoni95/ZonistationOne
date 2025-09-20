/*
 * ZonistationOne - PlayStation 1 Emulator
 * MIPS R3000A CPU Core Implementation (Stub)
 */

#include "cpu/r3000a.h"
#include "memory/memory_map.h"
#include "core/logger.h"
#include <iostream>
#include <cstring>

namespace ZonistationOne {

CPU::CPU(Memory* memory) 
    : m_memory(memory)
    , m_halted(false)
    , m_cycleCount(0)
    , m_delaySlot(false)
    , m_inInterruptServiceRoutine(false) {
    
    // Initialize registers to zero
    std::memset(m_registers, 0, sizeof(m_registers));
    std::memset(m_cop0_registers, 0, sizeof(m_cop0_registers));
    
    m_pc = RESET_VECTOR;
    m_nextPC = m_pc + 4;
    m_hi = m_lo = 0;
}

CPU::~CPU() {
    shutdown();
}

bool CPU::initialize() {
    ZONI_LOG_INFO(CPU, "Initializing MIPS R3000A core");
    
    // Reset to initial state
    reset();
    
    ZONI_LOG_INFO(CPU, "Initialization complete");
    return true;
}

void CPU::shutdown() {
    // Nothing to cleanup for now
}

void CPU::reset() {
    ZONI_LOG_INFO(CPU, "Resetting to initial state");
    
    // Clear all registers
    std::memset(m_registers, 0, sizeof(m_registers));
    std::memset(m_cop0_registers, 0, sizeof(m_cop0_registers));
    
    // Initialize COP0 registers to proper values following Redux patterns
    m_cop0_registers[12] = 0x10900000; // Status register initial value
    
    // Reset PC to BIOS entry point
    m_pc = RESET_VECTOR;
    m_nextPC = m_pc + 4;
    m_hi = m_lo = 0;
    
    m_halted = false;
    m_cycleCount = 0;
    m_delaySlot = false;
    m_inInterruptServiceRoutine = false;
}

uint32_t CPU::step() {
    if (m_halted) {
        // CPU is halted, don't execute any instructions
        ZONI_LOG_TRACE(CPU, "CPU step called while halted");
        return 0; // Don't consume cycles when halted
    }
    
    // Check for interrupts (following PCSX-Redux pattern)
    branchTest();
    
    // Fetch instruction
    uint32_t instruction = fetchInstruction();
    
    // Update PC (handle delay slot)
    uint32_t currentPC = m_pc;
    m_pc = m_nextPC;
    m_nextPC = m_pc + 4;
    
    // Execute instruction
    executeInstruction(instruction);
    
    m_cycleCount++;
    return 1; // Most instructions take 1 cycle
}

void CPU::runFor(uint32_t cycles) {
    for (uint32_t i = 0; i < cycles && !m_halted; ++i) {
        step();
    }
}

uint32_t CPU::fetchInstruction() {
    if (!m_memory) {
        ZONI_LOG_ERROR(CPU, "No memory interface available");
        m_halted = true;
        return 0;
    }
    
    return m_memory->read32(m_pc);
}

void CPU::executeInstruction(uint32_t instruction) {
    // Handle NOP early (most common instruction)
    if (instruction == 0) {
        return;
    }
    
    // Decode instruction
    InstructionInfo info(instruction);
    
    // Dispatch to appropriate handler
    uint32_t opcodeIndex = static_cast<uint32_t>(info.opcode);
    if (opcodeIndex < 64 && s_primaryHandlers[opcodeIndex] != nullptr) {
        (this->*s_primaryHandlers[opcodeIndex])(info);
    } else {
        handleUnknownInstruction(instruction, opcodeIndex);
    }
}

uint32_t CPU::getRegister(int reg) const {
    if (reg < 0 || reg >= 32) return 0;
    return m_registers[reg];
}

void CPU::setRegister(int reg, uint32_t value) {
    if (reg <= 0 || reg >= 32) return; // R0 is always zero
    m_registers[reg] = value;
}

void CPU::dumpState() const {
    ZONI_LOG_INFO(CPU, "=== CPU State ===");
    ZONI_LOG_INFO(CPU, "  PC: 0x%08x", m_pc);
    ZONI_LOG_INFO(CPU, "  Next PC: 0x%08x", m_nextPC);
    ZONI_LOG_INFO(CPU, "  Cycles: %llu", m_cycleCount);
    ZONI_LOG_INFO(CPU, "  Halted: %s", m_halted ? "Yes" : "No");
    
    // Print some key registers
    ZONI_LOG_INFO(CPU, "  Registers:");
}

// Cache control operations
void CPU::invalidateInstructionCache() {
    ZONI_LOG_INFO(CPU, "Invalidating instruction cache");
    // R3000A has 4KB instruction cache
    // For emulation, we don't need to actually implement cache hardware
    // but we should simulate the timing and behavior effects
}

void CPU::invalidateDataCache() {
    ZONI_LOG_INFO(CPU, "Invalidating data cache");
    // R3000A has 1KB data cache
    // Similar to instruction cache, simulate without full hardware implementation
}

void CPU::flushCache() {
    ZONI_LOG_INFO(CPU, "Flushing all caches");
    invalidateInstructionCache();
    invalidateDataCache();
    // Additional flush operations like write-back if needed
}

void CPU::configureCacheMode(uint32_t config) {
    ZONI_LOG_INFO(CPU, "Configuring cache mode: 0x%08x", config);
    // Handle cache configuration settings
    // This affects cache behavior but for basic emulation we just log it
    
    // Parse configuration bits (based on R3000A documentation)
    bool icacheEnabled = (config & 0x00000002) == 0;  // Bit 1 inverted
    bool dcacheEnabled = (config & 0x00000004) == 0;  // Bit 2 inverted
    
    ZONI_LOG_INFO(CPU, "Cache configuration - ICache: %s, DCache: %s", 
                  icacheEnabled ? "enabled" : "disabled",
                  dcacheEnabled ? "enabled" : "disabled");
}

// Helper method for unknown instructions
void CPU::handleUnknownInstruction(uint32_t instruction, uint32_t opcode) {
    static int unknownCount = 0;
    if (unknownCount < 10) { // Don't spam too much
        ZONI_LOG_CPU_UNKNOWN_INSTRUCTION("Unknown instruction 0x%08x (opcode 0x%02x) at PC 0x%08x", 
                                          instruction, opcode, m_pc);
        unknownCount++;
    }
    
    // For now, halt on unknown instructions to prevent infinite loops
    if (unknownCount >= 10) {
        ZONI_LOG_WARN(CPU, "Too many unknown instructions, halting");
        m_halted = true;
    }
}

// ========================================
// Exception Handling (Following PCSX-Redux patterns)
// ========================================

void CPU::exception(Exception e, bool inDelaySlot) {
    exception(static_cast<uint32_t>(e) << 2, inDelaySlot);
}

void CPU::exception(uint32_t code, bool inDelaySlot) {
    ZONI_LOG_DEBUG(CPU, "Exception 0x%08x at PC 0x%08x (delay slot: %s)", 
                   code, m_pc, inDelaySlot ? "yes" : "no");
    
    m_inInterruptServiceRoutine = true;
    
    // Set the EPC & PC
    if (inDelaySlot) {
        code |= 0x80000000;  // Set BD (Branch Delay) bit
        m_cop0_registers[14] = m_pc - 4;  // EPC points to branch instruction
    } else {
        m_cop0_registers[14] = m_pc;  // EPC points to current instruction
    }
    
    // Set exception vector based on BEV bit in Status register
    if (m_cop0_registers[12] & 0x400000) {  // BEV bit set (boot exception vectors)
        m_pc = 0xbfc00180;
    } else {
        m_pc = 0x80000080;  // Normal exception vector
    }
    
    m_nextPC = m_pc + 4;
    
    // Set the Cause register
    m_cop0_registers[13] = code;
    
    // Set the Status register - push interrupt mask stack
    // Following Redux pattern: save old status and disable interrupts
    m_cop0_registers[12] = (m_cop0_registers[12] & ~0x3f) | ((m_cop0_registers[12] & 0xf) << 2);
}

void CPU::branchTest() {
    // Simple interrupt checking - following Redux patterns
    // For now, we don't have hardware interrupt sources, so this is mostly a stub
    // In a full implementation, this would check for:
    // - Timer interrupts
    // - I/O interrupts 
    // - DMA interrupts
    // - etc.
    
    // Check if interrupts are enabled in Status register
    if ((m_cop0_registers[12] & 0x401) == 0x401) {  // IEc and master enable
        // For now, no interrupt sources to check
        // This is where we'd check hardware interrupt registers and trigger exception(0x400, false);
    }
}

// ========================================
// Primary instruction handlers
// ========================================

void CPU::handleSPECIAL(const InstructionInfo& info) {
    uint32_t functIndex = static_cast<uint32_t>(info.specialFunct);
    if (functIndex < 64 && s_specialHandlers[functIndex] != nullptr) {
        (this->*s_specialHandlers[functIndex])(info);
    } else {
        handleUnknownInstruction(info.code, static_cast<uint32_t>(info.opcode));
    }
}

void CPU::handleLUI(const InstructionInfo& info) {
    // LUI rt, immediate - Load Upper Immediate
    // Format: LUI rt, immediate
    if (info.rt == 0) return; // Can't write to register 0
    
    uint32_t value = _ImmLU_(info.code); // Shift immediate to upper 16 bits
    ZONI_LOG_CPU_INSTRUCTION("LUI R%d, 0x%04x (result: 0x%08x)", info.rt, info.immU, value);
    
    setRegister(info.rt, value);
}

void CPU::handleCOP0(const InstructionInfo& info) {
    // COP0 - Coprocessor 0 (System Control)
    // Format: COP0 rs, ...
    // Rs field determines the COP0 operation
    
    uint32_t cop0_op = info.rs;  // Rs field contains the COP0 sub-operation
    
    switch (cop0_op) {
        case 0x00: // MFC0 - Move From Coprocessor 0
        {
            if (info.rt == 0) return; // Can't write to register 0
            uint32_t cop0_reg = info.rd;
            uint32_t value = m_cop0_registers[cop0_reg];
            
            ZONI_LOG_CPU_INSTRUCTION("MFC0 R%d, COP0[%d] (read 0x%08x)", 
                                     info.rt, cop0_reg, value);
            setRegister(info.rt, value);
            break;
        }
        
        case 0x04: // MTC0 - Move To Coprocessor 0
        {
            uint32_t cop0_reg = info.rd;
            uint32_t value = getRegister(info.rt);
            
            ZONI_LOG_CPU_INSTRUCTION("MTC0 R%d, COP0[%d] (write 0x%08x)", 
                                     info.rt, cop0_reg, value);
            
            // Handle special COP0 registers
            switch (cop0_reg) {
                case 12: // Status register
                    m_cop0_registers[12] = value;
                    branchTest(); // Test for interrupts after Status change
                    break;
                case 13: // Cause register  
                    m_cop0_registers[13] = value & ~(0xfc00); // Clear reserved bits
                    branchTest(); // Test for interrupts after Cause change
                    break;
                default:
                    m_cop0_registers[cop0_reg] = value;
                    break;
            }
            break;
        }
        
        case 0x10: // RFE - Return From Exception
        {
            // RFE restores previous interrupt state by shifting Status register bits
            // Following PCSX-Redux implementation:
            // - Clear bottom 4 bits of Status register
            // - Shift bits [5:2] right by 2 positions
            // - OR the shifted bits back to restore previous state
            
            uint32_t status = m_cop0_registers[12]; // Status register is COP0[12]
            uint32_t old_status = status;
            
            // Clear bottom 4 bits and shift bits [5:2] right by 2
            status = (status & ~0xF) | ((status & 0x3C) >> 2);
            m_cop0_registers[12] = status;
            
            // Mark that we're no longer in an ISR
            m_inInterruptServiceRoutine = false;
            
            ZONI_LOG_CPU_INSTRUCTION("RFE (Status: 0x%08x -> 0x%08x)", 
                                     old_status, status);
                                     
            // Test for software interrupts after RFE (following Redux pattern)
            branchTest();
            break;
        }
        
        default:
            ZONI_LOG_WARN(CPU, "Unknown COP0 operation: rs=0x%02x, instruction=0x%08x", 
                          cop0_op, info.code);
            handleUnknownInstruction(info.code, static_cast<uint32_t>(info.opcode));
            break;
    }
}

void CPU::handleORI(const InstructionInfo& info) {
    // ORI rt, rs, immediate - OR Immediate
    // Format: ORI rt, rs, immediate
    if (info.rt == 0) return; // Can't write to register 0
    
    uint32_t rsValue = getRegister(info.rs);
    uint32_t result = rsValue | info.immU;
    
    ZONI_LOG_CPU_INSTRUCTION("ORI R%d, R%d, 0x%04x (0x%08x | 0x%04x = 0x%08x)", 
                             info.rt, info.rs, info.immU, rsValue, info.immU, result);
    
    setRegister(info.rt, result);
}

void CPU::handleADDIU(const InstructionInfo& info) {
    // ADDIU rt, rs, immediate - Add Immediate Unsigned
    // Format: ADDIU rt, rs, immediate
    if (info.rt == 0) return; // Can't write to register 0
    
    uint32_t rsValue = getRegister(info.rs);
    uint32_t result = rsValue + static_cast<uint32_t>(info.imm); // Sign-extend and add
    
    ZONI_LOG_CPU_INSTRUCTION("ADDIU R%d, R%d, %d (0x%08x + %d = 0x%08x)", 
                             info.rt, info.rs, info.imm, rsValue, info.imm, result);
    
    setRegister(info.rt, result);
}

void CPU::handleSW(const InstructionInfo& info) {
    // SW rt, offset(rs) - Store Word
    // Format: SW rt, offset(rs)
    uint32_t baseAddr = getRegister(info.rs);
    uint32_t address = baseAddr + static_cast<uint32_t>(info.imm); // Sign-extend offset
    uint32_t value = getRegister(info.rt);
    
    ZONI_LOG_CPU_INSTRUCTION("SW R%d, %d(R%d) [0x%08x] = 0x%08x", 
                             info.rt, info.imm, info.rs, address, value);
    
    // Check alignment (word access must be 4-byte aligned)
    if (address & 0x3) {
        ZONI_LOG_ERROR(CPU, "Unaligned store word access at 0x%08x", address);
        // TODO: Generate alignment exception
        m_halted = true;
        return;
    }
    
    if (m_memory) {
        m_memory->write32(address, value);
    } else {
        ZONI_LOG_ERROR(CPU, "No memory interface available for SW");
        m_halted = true;
    }
}

void CPU::handleLWL(const InstructionInfo& info) {
    // LWL - Load Word Left (unaligned load)
    // Format: LWL rt, offset(rs)
    // Operation: Load left portion of word, merge with existing rt value
    
    if (info.rt == 0) return; // Can't write to register 0
    
    uint32_t baseAddr = getRegister(info.rs);
    uint32_t address = baseAddr + static_cast<uint32_t>(info.imm);
    uint32_t alignedAddr = address & ~0x3; // Word-aligned address
    uint32_t shift = (address & 0x3) * 8;   // Byte offset * 8
    
    // Load aligned word from memory
    uint32_t memValue = m_memory->read32(alignedAddr);
    uint32_t currentValue = getRegister(info.rt);
    
    // Extract left portion and merge with right portion of current register
    uint32_t mask = (0xFFFFFFFF << (32 - shift)) & 0xFFFFFFFF;
    uint32_t result = (memValue << (32 - shift)) | (currentValue & ~mask);
    
    ZONI_LOG_CPU_INSTRUCTION("LWL R%d, %d(R%d) [0x%08x] (aligned: 0x%08x, shift: %d)", 
                             info.rt, info.imm, info.rs, address, alignedAddr, shift);
    
    setRegister(info.rt, result);
}

void CPU::handleLWR(const InstructionInfo& info) {
    // LWR - Load Word Right (unaligned load)
    // Format: LWR rt, offset(rs)
    // Operation: Load right portion of word, merge with existing rt value
    
    if (info.rt == 0) return; // Can't write to register 0
    
    uint32_t baseAddr = getRegister(info.rs);
    uint32_t address = baseAddr + static_cast<uint32_t>(info.imm);
    uint32_t alignedAddr = address & ~0x3; // Word-aligned address
    uint32_t shift = (address & 0x3) * 8;   // Byte offset * 8
    
    // Load aligned word from memory
    uint32_t memValue = m_memory->read32(alignedAddr);
    uint32_t currentValue = getRegister(info.rt);
    
    // Extract right portion and merge with left portion of current register
    uint32_t mask = 0xFFFFFFFF >> (24 - shift);
    uint32_t result = (memValue >> shift) | (currentValue & ~mask);
    
    ZONI_LOG_CPU_INSTRUCTION("LWR R%d, %d(R%d) [0x%08x] (aligned: 0x%08x, shift: %d)", 
                             info.rt, info.imm, info.rs, address, alignedAddr, shift);
    
    setRegister(info.rt, result);
}

void CPU::handleSWL(const InstructionInfo& info) {
    // SWL - Store Word Left (unaligned store)
    // Format: SWL rt, offset(rs)
    // Operation: Store left portion of rt to memory, preserving other bytes
    
    uint32_t baseAddr = getRegister(info.rs);
    uint32_t address = baseAddr + static_cast<uint32_t>(info.imm);
    uint32_t alignedAddr = address & ~0x3; // Word-aligned address
    uint32_t shift = (address & 0x3) * 8;   // Byte offset * 8
    uint32_t value = getRegister(info.rt);
    
    // Load current aligned word
    uint32_t memValue = m_memory->read32(alignedAddr);
    
    // Create mask and merge
    uint32_t mask = 0xFFFFFFFF >> (32 - shift);
    uint32_t result = (memValue & mask) | (value >> (32 - shift));
    
    ZONI_LOG_CPU_INSTRUCTION("SWL R%d, %d(R%d) [0x%08x] (aligned: 0x%08x, shift: %d)", 
                             info.rt, info.imm, info.rs, address, alignedAddr, shift);
    
    m_memory->write32(alignedAddr, result);
}

void CPU::handleLW(const InstructionInfo& info) {
    // LW rt, offset(rs) - Load Word
    // Format: LW rt, offset(rs)
    if (info.rt == 0) return; // Can't write to register 0
    
    uint32_t baseAddr = getRegister(info.rs);
    uint32_t address = baseAddr + static_cast<uint32_t>(info.imm); // Sign-extend offset
    
    ZONI_LOG_CPU_INSTRUCTION("LW R%d, %d(R%d) [0x%08x]", 
                             info.rt, info.imm, info.rs, address);
    
    // Check alignment (word access must be 4-byte aligned)
    if (address & 0x3) {
        ZONI_LOG_ERROR(CPU, "Unaligned load word access at 0x%08x", address);
        // TODO: Generate alignment exception
        m_halted = true;
        return;
    }
    
    if (m_memory) {
        uint32_t value = m_memory->read32(address);
        setRegister(info.rt, value);
        ZONI_LOG_CPU_INSTRUCTION("  -> Loaded 0x%08x into R%d", value, info.rt);
    } else {
        ZONI_LOG_ERROR(CPU, "No memory interface available for LW");
        m_halted = true;
    }
}

// ========================================
// SPECIAL function handlers  
// ========================================

void CPU::handleOR(const InstructionInfo& info) {
    // OR rd, rs, rt - Bitwise OR
    // Format: OR rd, rs, rt (SPECIAL funct=0x25)
    if (info.rd == 0) return; // Can't write to register 0
    
    uint32_t rsValue = getRegister(info.rs);
    uint32_t rtValue = getRegister(info.rt);
    uint32_t result = rsValue | rtValue;
    
    ZONI_LOG_CPU_INSTRUCTION("OR R%d, R%d, R%d (0x%08x | 0x%08x = 0x%08x)", 
                             info.rd, info.rs, info.rt, rsValue, rtValue, result);
    
    setRegister(info.rd, result);
}

void CPU::handleADDU(const InstructionInfo& info) {
    // ADDU rd, rs, rt - Add Unsigned
    // Format: ADDU rd, rs, rt (SPECIAL funct=0x21)
    if (info.rd == 0) return; // Can't write to register 0
    
    uint32_t rsValue = getRegister(info.rs);
    uint32_t rtValue = getRegister(info.rt);
    uint32_t result = rsValue + rtValue;
    
    ZONI_LOG_CPU_INSTRUCTION("ADDU R%d, R%d, R%d (0x%08x + 0x%08x = 0x%08x)", 
                             info.rd, info.rs, info.rt, rsValue, rtValue, result);
    
    setRegister(info.rd, result);
}

// ========================================
// Stub handlers (not implemented yet)
// ========================================

void CPU::handleREGIMM(const InstructionInfo& info) {
    handleUnknownInstruction(info.code, static_cast<uint32_t>(info.opcode));
}

void CPU::handleJ(const InstructionInfo& info) {
    // J - Jump
    // Format: J target
    // Operation: PC = (PC+4)[31:28] || target || 00
    
    uint32_t jumpTarget = _JumpTarget_(info.code, m_pc);
    
    ZONI_LOG_CPU_INSTRUCTION("J 0x%07x (jump to 0x%08x)", info.target, jumpTarget);
    
    // Set the next PC to the jump target
    m_nextPC = jumpTarget;
}

void CPU::handleJAL(const InstructionInfo& info) {
    // JAL - Jump and Link
    // Format: JAL target
    // Operation: R31 = PC+8, PC = (PC+4)[31:28] || target || 00
    
    uint32_t jumpTarget = _JumpTarget_(info.code, m_pc);
    uint32_t returnAddress = m_pc + 4; // PC+8 because PC is already PC+4 when this executes
    
    ZONI_LOG_CPU_INSTRUCTION("JAL 0x%07x (jump to 0x%08x, return addr: 0x%08x)", 
                             info.target, jumpTarget, returnAddress);
    
    // Store return address in R31 (link register)
    setRegister(31, returnAddress);
    
    // Set the next PC to the jump target
    m_nextPC = jumpTarget;
}

void CPU::handleBEQ(const InstructionInfo& info) {
    // BEQ - Branch if Equal
    // Format: BEQ rs, rt, offset
    // Operation: if (GPR[rs] == GPR[rt]) PC = PC + 4 + (sign_extend(offset) << 2)
    
    uint32_t rsValue = getRegister(info.rs);
    uint32_t rtValue = getRegister(info.rt);
    
    if (rsValue == rtValue) {
        uint32_t branchTarget = _BranchTarget_(info.code, m_pc);
        
        ZONI_LOG_CPU_INSTRUCTION("BEQ R%d, R%d, %d (0x%08x == 0x%08x: TRUE, branch to 0x%08x)", 
                                 info.rs, info.rt, info.imm, rsValue, rtValue, branchTarget);
        
        m_nextPC = branchTarget;
    } else {
        ZONI_LOG_CPU_INSTRUCTION("BEQ R%d, R%d, %d (0x%08x == 0x%08x: FALSE, no branch)", 
                                 info.rs, info.rt, info.imm, rsValue, rtValue);
    }
}

void CPU::handleBNE(const InstructionInfo& info) {
    // BNE - Branch if Not Equal
    // Format: BNE rs, rt, offset
    // Operation: if (GPR[rs] != GPR[rt]) PC = PC + 4 + (sign_extend(offset) << 2)
    
    uint32_t rsValue = getRegister(info.rs);
    uint32_t rtValue = getRegister(info.rt);
    
    if (rsValue != rtValue) {
        uint32_t branchTarget = _BranchTarget_(info.code, m_pc);
        
        ZONI_LOG_CPU_INSTRUCTION("BNE R%d, R%d, %d (0x%08x != 0x%08x: TRUE, branch to 0x%08x)", 
                                 info.rs, info.rt, info.imm, rsValue, rtValue, branchTarget);
        
        m_nextPC = branchTarget;
    } else {
        ZONI_LOG_CPU_INSTRUCTION("BNE R%d, R%d, %d (0x%08x != 0x%08x: FALSE, no branch)", 
                                 info.rs, info.rt, info.imm, rsValue, rtValue);
    }
}

void CPU::handleBLEZ(const InstructionInfo& info) {
    handleUnknownInstruction(info.code, static_cast<uint32_t>(info.opcode));
}

void CPU::handleBGTZ(const InstructionInfo& info) {
    handleUnknownInstruction(info.code, static_cast<uint32_t>(info.opcode));
}

void CPU::handleADDI(const InstructionInfo& info) {
    // ADDI - Add Immediate (with overflow exception)
    // Format: ADDI rt, rs, immediate
    // Operation: GPR[rt] = GPR[rs] + sign_extend(immediate)
    // Exception: Integer overflow
    
    if (info.rt == 0) return; // Can't write to register 0
    
    uint32_t rsValue = getRegister(info.rs);
    int32_t immediate = info.imm; // Already sign-extended
    uint32_t result = rsValue + immediate;
    
    // Check for signed overflow (same as PCSX-Redux)
    bool overflow = ((rsValue ^ result) & (immediate ^ result)) >> 31;
    if (overflow) {
        ZONI_LOG_ERROR(CPU, "ADDI overflow: R%d + %d (0x%08x + %d)", 
                       info.rs, immediate, rsValue, immediate);
        // TODO: Trigger arithmetic overflow exception
        // For now, just halt to prevent crashes
        m_halted = true;
        return;
    }
    
    ZONI_LOG_CPU_INSTRUCTION("ADDI R%d, R%d, %d (0x%08x + %d = 0x%08x)", 
                             info.rt, info.rs, immediate, rsValue, immediate, result);
    
    setRegister(info.rt, result);
}

void CPU::handleSLTI(const InstructionInfo& info) {
    handleUnknownInstruction(info.code, static_cast<uint32_t>(info.opcode));
}

void CPU::handleSLTIU(const InstructionInfo& info) {
    handleUnknownInstruction(info.code, static_cast<uint32_t>(info.opcode));
}

void CPU::handleANDI(const InstructionInfo& info) {
    // ANDI - AND Immediate
    // Format: ANDI rt, rs, immediate
    // Operation: GPR[rt] = GPR[rs] AND zero_extend(immediate)
    
    uint32_t rsValue = getRegister(info.rs);
    uint32_t immediate = static_cast<uint32_t>(info.imm) & 0xFFFF; // Zero-extend immediate
    uint32_t result = rsValue & immediate;
    
    ZONI_LOG_CPU_INSTRUCTION("ANDI R%d, R%d, 0x%04x (0x%08x & 0x%04x = 0x%08x)", 
                             info.rt, info.rs, immediate, rsValue, immediate, result);
    
    setRegister(info.rt, result);
}

void CPU::handleXORI(const InstructionInfo& info) {
    handleUnknownInstruction(info.code, static_cast<uint32_t>(info.opcode));
}

void CPU::handleLB(const InstructionInfo& info) {
    // LB - Load Byte (sign-extended)
    // Format: LB rt, offset(rs)
    // Operation: GPR[rt] = sign_extend(MEM[GPR[rs] + sign_extend(offset)])
    
    if (info.rt == 0) return; // Can't write to register 0
    
    uint32_t baseAddr = getRegister(info.rs);
    uint32_t address = baseAddr + info.imm; // imm is already sign-extended
    
    // Load byte from memory and sign-extend to 32 bits
    uint8_t byteValue = m_memory->read8(address);
    uint32_t value = static_cast<int32_t>(static_cast<int8_t>(byteValue)); // Sign extend
    
    ZONI_LOG_CPU_INSTRUCTION("LB R%d, %d(R%d) [0x%08x] = 0x%02x (extended: 0x%08x)", 
                             info.rt, info.imm, info.rs, address, byteValue, value);
    
    setRegister(info.rt, value);
}

void CPU::handleLH(const InstructionInfo& info) {
    handleUnknownInstruction(info.code, static_cast<uint32_t>(info.opcode));
}

void CPU::handleLBU(const InstructionInfo& info) {
    // LBU - Load Byte Unsigned (zero-extended)
    // Format: LBU rt, offset(rs)
    // Operation: GPR[rt] = zero_extend(MEM[GPR[rs] + sign_extend(offset)])
    
    if (info.rt == 0) return; // Can't write to register 0
    
    uint32_t baseAddr = getRegister(info.rs);
    uint32_t address = baseAddr + info.imm; // imm is already sign-extended
    
    // Load byte from memory and zero-extend to 32 bits
    uint8_t byteValue = m_memory->read8(address);
    uint32_t value = static_cast<uint32_t>(byteValue); // Zero extend
    
    ZONI_LOG_CPU_INSTRUCTION("LBU R%d, %d(R%d) [0x%08x] = 0x%02x (extended: 0x%08x)", 
                             info.rt, info.imm, info.rs, address, byteValue, value);
    
    setRegister(info.rt, value);
}

void CPU::handleLHU(const InstructionInfo& info) {
    handleUnknownInstruction(info.code, static_cast<uint32_t>(info.opcode));
}

void CPU::handleSB(const InstructionInfo& info) {
    // SB - Store Byte
    // Format: SB rt, offset(rs)
    // Operation: MEM[GPR[rs] + sign_extend(offset)] = GPR[rt][7:0]
    
    uint32_t baseAddr = getRegister(info.rs);
    uint32_t address = baseAddr + info.imm; // imm is already sign-extended
    uint8_t value = static_cast<uint8_t>(getRegister(info.rt));
    
    ZONI_LOG_CPU_INSTRUCTION("SB R%d, %d(R%d) [0x%08x] = 0x%02x", 
                             info.rt, info.imm, info.rs, address, value);
    
    // Store byte to memory
    m_memory->write8(address, value);
}

void CPU::handleSH(const InstructionInfo& info) {
    // SH - Store Halfword
    // Format: SH rt, offset(rs)
    // Operation: MEM[GPR[rs] + sign_extend(offset)] = GPR[rt][15:0]
    
    uint32_t baseAddr = getRegister(info.rs);
    uint32_t address = baseAddr + info.imm; // imm is already sign-extended
    uint16_t value = static_cast<uint16_t>(getRegister(info.rt));
    
    // Check for alignment (must be 2-byte aligned)
    if (address & 0x1) {
        ZONI_LOG_ERROR(CPU, "SH to unaligned address 0x%08x", address);
        // TODO: Trigger address error exception
        m_halted = true;
        return;
    }
    
    ZONI_LOG_CPU_INSTRUCTION("SH R%d, %d(R%d) [0x%08x] = 0x%04x", 
                             info.rt, info.imm, info.rs, address, value);
    
    // Store halfword to memory
    m_memory->write16(address, value);
}

void CPU::handleSLL(const InstructionInfo& info) {
    // SLL - Shift Left Logical
    // Format: SLL rd, rt, sa
    // Operation: GPR[rd] = GPR[rt] << sa
    
    if (info.rd == 0) return; // Can't write to register 0
    
    uint32_t value = getRegister(info.rt);
    uint32_t shamt = info.sa; // Shift amount (5 bits)
    uint32_t result = value << shamt;
    
    ZONI_LOG_CPU_INSTRUCTION("SLL R%d, R%d, %d (0x%08x << %d = 0x%08x)", 
                             info.rd, info.rt, shamt, value, shamt, result);
    
    setRegister(info.rd, result);
}

void CPU::handleSRL(const InstructionInfo& info) {
    // SRL - Shift Right Logical
    // Format: SRL rd, rt, sa
    // Operation: GPR[rd] = GPR[rt] >> sa (logical shift, zero-fill)
    
    if (info.rd == 0) return; // Can't write to register 0
    
    uint32_t value = getRegister(info.rt);
    uint32_t shamt = info.sa; // Shift amount (5 bits)
    uint32_t result = value >> shamt;
    
    ZONI_LOG_CPU_INSTRUCTION("SRL R%d, R%d, %d (0x%08x >> %d = 0x%08x)", 
                             info.rd, info.rt, shamt, value, shamt, result);
    
    setRegister(info.rd, result);
}

void CPU::handleSLLV(const InstructionInfo& info) {
    // SLLV - Shift Left Logical Variable
    // Format: SLLV rd, rt, rs
    // Operation: GPR[rd] = GPR[rt] << GPR[rs][4:0]
    
    if (info.rd == 0) return; // Can't write to register 0
    
    uint32_t value = getRegister(info.rt);
    uint32_t shamt = getRegister(info.rs) & 0x1F; // Only lower 5 bits used
    uint32_t result = value << shamt;
    
    ZONI_LOG_CPU_INSTRUCTION("SLLV R%d, R%d, R%d (0x%08x << %d = 0x%08x)", 
                             info.rd, info.rt, info.rs, value, shamt, result);
    
    setRegister(info.rd, result);
}

void CPU::handleSRLV(const InstructionInfo& info) {
    // SRLV - Shift Right Logical Variable
    // Format: SRLV rd, rt, rs
    // Operation: GPR[rd] = GPR[rt] >> GPR[rs][4:0] (logical shift, zero-fill)
    
    if (info.rd == 0) return; // Can't write to register 0
    
    uint32_t value = getRegister(info.rt);
    uint32_t shamt = getRegister(info.rs) & 0x1F; // Only lower 5 bits used
    uint32_t result = value >> shamt;
    
    ZONI_LOG_CPU_INSTRUCTION("SRLV R%d, R%d, R%d (0x%08x >> %d = 0x%08x)", 
                             info.rd, info.rt, info.rs, value, shamt, result);
    
    setRegister(info.rd, result);
}

void CPU::handleSRAV(const InstructionInfo& info) {
    // SRAV - Shift Right Arithmetic Variable  
    // Format: SRAV rd, rt, rs
    // Operation: GPR[rd] = GPR[rt] >> GPR[rs][4:0] (arithmetic shift, sign extend)
    
    if (info.rd == 0) return; // Can't write to register 0
    
    int32_t value = static_cast<int32_t>(getRegister(info.rt));
    uint32_t shamt = getRegister(info.rs) & 0x1F; // Only lower 5 bits used
    int32_t result = value >> shamt; // Arithmetic shift preserves sign
    
    ZONI_LOG_CPU_INSTRUCTION("SRAV R%d, R%d, R%d (0x%08x >> %d = 0x%08x)", 
                             info.rd, info.rt, info.rs, static_cast<uint32_t>(value), shamt, static_cast<uint32_t>(result));
    
    setRegister(info.rd, static_cast<uint32_t>(result));
}

void CPU::handleADD(const InstructionInfo& info) {
    // ADD - Add Word
    // Format: ADD rd, rs, rt  
    // Operation: GPR[rd] = GPR[rs] + GPR[rt] (with overflow exception)
    
    if (info.rd == 0) return; // Can't write to register 0
    
    int32_t rs_value = static_cast<int32_t>(getRegister(info.rs));
    int32_t rt_value = static_cast<int32_t>(getRegister(info.rt));
    
    // Check for overflow (simplified - would normally trigger exception)
    int64_t result64 = static_cast<int64_t>(rs_value) + static_cast<int64_t>(rt_value);
    uint32_t result = static_cast<uint32_t>(result64);
    
    ZONI_LOG_CPU_INSTRUCTION("ADD R%d, R%d, R%d (0x%08x + 0x%08x = 0x%08x)", 
                             info.rd, info.rs, info.rt, 
                             static_cast<uint32_t>(rs_value), static_cast<uint32_t>(rt_value), result);
    
    setRegister(info.rd, result);
}

void CPU::handleSRA(const InstructionInfo& info) {
    handleUnknownInstruction(info.code, static_cast<uint32_t>(info.opcode));
}

void CPU::handleJR(const InstructionInfo& info) {
    // JR - Jump Register
    // Format: JR rs
    // Operation: PC = GPR[rs]
    
    uint32_t jumpTarget = getRegister(info.rs);
    
    // Check for address alignment (must be 4-byte aligned)
    if (jumpTarget & 0x3) {
        ZONI_LOG_ERROR(CPU, "JR to unaligned address 0x%08x from R%d", jumpTarget, info.rs);
        // TODO: Trigger address error exception
        m_halted = true;
        return;
    }
    
    ZONI_LOG_CPU_INSTRUCTION("JR R%d (jump to 0x%08x)", info.rs, jumpTarget);
    
    // Set the next PC to the jump target
    m_nextPC = jumpTarget;
}

void CPU::handleJALR(const InstructionInfo& info) {
    // JALR - Jump and Link Register
    // Format: JALR rs, rd (rd defaults to 31 if not specified)
    // Operation: GPR[rd] = PC+8, PC = GPR[rs]
    
    uint32_t jumpTarget = getRegister(info.rs);
    uint32_t returnAddress = m_pc + 4; // PC+8 because PC is already PC+4 when this executes
    uint32_t linkRegister = info.rd ? info.rd : 31; // Default to R31 if rd is 0
    
    // Check for address alignment (must be 4-byte aligned)
    if (jumpTarget & 0x3) {
        ZONI_LOG_ERROR(CPU, "JALR to unaligned address 0x%08x from R%d", jumpTarget, info.rs);
        // TODO: Trigger address error exception
        m_halted = true;
        return;
    }
    
    ZONI_LOG_CPU_INSTRUCTION("JALR R%d, R%d (jump to 0x%08x, return addr: 0x%08x)", 
                             info.rs, linkRegister, jumpTarget, returnAddress);
    
    // Store return address in the link register
    if (linkRegister != 0) {  // Don't write to R0
        setRegister(linkRegister, returnAddress);
    }
    
    // Set the next PC to the jump target
    m_nextPC = jumpTarget;
}

void CPU::handleSYSCALL(const InstructionInfo& info) {
    // SYSCALL - System Call
    // Generates a syscall exception
    ZONI_LOG_CPU_INSTRUCTION("SYSCALL (code: 0x%05x)", (info.code >> 6) & 0xFFFFF);
    exception(Exception::Syscall, m_delaySlot);
}

void CPU::handleBREAK(const InstructionInfo& info) {
    // BREAK - Breakpoint  
    // Generates a break exception
    uint32_t code = (info.code >> 6) & 0xFFFFF;
    ZONI_LOG_CPU_INSTRUCTION("BREAK (code: 0x%05x)", code);
    exception(Exception::Break, m_delaySlot);
}

void CPU::handleMFHI(const InstructionInfo& info) {
    handleUnknownInstruction(info.code, static_cast<uint32_t>(info.opcode));
}

void CPU::handleMTHI(const InstructionInfo& info) {
    handleUnknownInstruction(info.code, static_cast<uint32_t>(info.opcode));
}

void CPU::handleMFLO(const InstructionInfo& info) {
    handleUnknownInstruction(info.code, static_cast<uint32_t>(info.opcode));
}

void CPU::handleMTLO(const InstructionInfo& info) {
    handleUnknownInstruction(info.code, static_cast<uint32_t>(info.opcode));
}

void CPU::handleMULT(const InstructionInfo& info) {
    handleUnknownInstruction(info.code, static_cast<uint32_t>(info.opcode));
}

void CPU::handleMULTU(const InstructionInfo& info) {
    handleUnknownInstruction(info.code, static_cast<uint32_t>(info.opcode));
}

void CPU::handleDIV(const InstructionInfo& info) {
    handleUnknownInstruction(info.code, static_cast<uint32_t>(info.opcode));
}

void CPU::handleDIVU(const InstructionInfo& info) {
    handleUnknownInstruction(info.code, static_cast<uint32_t>(info.opcode));
}

void CPU::handleSUB(const InstructionInfo& info) {
    handleUnknownInstruction(info.code, static_cast<uint32_t>(info.opcode));
}

void CPU::handleSUBU(const InstructionInfo& info) {
    handleUnknownInstruction(info.code, static_cast<uint32_t>(info.opcode));
}

void CPU::handleAND(const InstructionInfo& info) {
    // AND rd, rs, rt - Bitwise AND
    // Format: AND rd, rs, rt (SPECIAL funct=0x24)
    if (info.rd == 0) return; // Can't write to register 0
    
    uint32_t rsValue = getRegister(info.rs);
    uint32_t rtValue = getRegister(info.rt);
    uint32_t result = rsValue & rtValue;
    
    ZONI_LOG_CPU_INSTRUCTION("AND R%d, R%d, R%d (0x%08x & 0x%08x = 0x%08x)", 
                             info.rd, info.rs, info.rt, rsValue, rtValue, result);
    
    setRegister(info.rd, result);
}

void CPU::handleXOR(const InstructionInfo& info) {
    handleUnknownInstruction(info.code, static_cast<uint32_t>(info.opcode));
}

void CPU::handleNOR(const InstructionInfo& info) {
    handleUnknownInstruction(info.code, static_cast<uint32_t>(info.opcode));
}

void CPU::handleSLT(const InstructionInfo& info) {
    // SLT - Set Less Than (signed comparison)
    // Format: SLT rd, rs, rt
    // Operation: GPR[rd] = (GPR[rs] < GPR[rt]) ? 1 : 0 (signed comparison)
    
    if (info.rd == 0) return; // Can't write to register 0
    
    int32_t rsValue = static_cast<int32_t>(getRegister(info.rs));
    int32_t rtValue = static_cast<int32_t>(getRegister(info.rt));
    uint32_t result = (rsValue < rtValue) ? 1 : 0;
    
    ZONI_LOG_CPU_INSTRUCTION("SLT R%d, R%d, R%d (%d < %d = %s)", 
                             info.rd, info.rs, info.rt, rsValue, rtValue, 
                             result ? "true" : "false");
    
    setRegister(info.rd, result);
}

void CPU::handleSLTU(const InstructionInfo& info) {
    // SLTU - Set Less Than Unsigned (unsigned comparison)
    // Format: SLTU rd, rs, rt
    // Operation: GPR[rd] = (GPR[rs] < GPR[rt]) ? 1 : 0 (unsigned comparison)
    
    if (info.rd == 0) return; // Can't write to register 0
    
    uint32_t rsValue = getRegister(info.rs);
    uint32_t rtValue = getRegister(info.rt);
    uint32_t result = (rsValue < rtValue) ? 1 : 0;
    
    ZONI_LOG_CPU_INSTRUCTION("SLTU R%d, R%d, R%d (0x%08x < 0x%08x = %s)", 
                             info.rd, info.rs, info.rt, rsValue, rtValue, 
                             result ? "true" : "false");
    
    setRegister(info.rd, result);
}

// ========================================
// Dispatch tables (following PCSX-Redux pattern)  
// ========================================

const CPU::InstructionHandler CPU::s_primaryHandlers[64] = {
    &CPU::handleSPECIAL,    // 0x00 - SPECIAL
    &CPU::handleREGIMM,     // 0x01 - REGIMM
    &CPU::handleJ,          // 0x02 - J
    &CPU::handleJAL,        // 0x03 - JAL
    &CPU::handleBEQ,        // 0x04 - BEQ
    &CPU::handleBNE,        // 0x05 - BNE
    &CPU::handleBLEZ,       // 0x06 - BLEZ
    &CPU::handleBGTZ,       // 0x07 - BGTZ
    &CPU::handleADDI,       // 0x08 - ADDI
    &CPU::handleADDIU,      // 0x09 - ADDIU
    &CPU::handleSLTI,       // 0x0A - SLTI
    &CPU::handleSLTIU,      // 0x0B - SLTIU
    &CPU::handleANDI,       // 0x0C - ANDI
    &CPU::handleORI,        // 0x0D - ORI
    &CPU::handleXORI,       // 0x0E - XORI
    &CPU::handleLUI,        // 0x0F - LUI
    &CPU::handleCOP0,       // 0x10 - COP0 (System Control Processor)
    nullptr,                // 0x11 - COP1
    nullptr,                // 0x12 - COP2
    nullptr,                // 0x13 - COP3
    nullptr, nullptr, nullptr, nullptr,    // 0x14-0x17 - Reserved
    nullptr, nullptr, nullptr, nullptr,    // 0x18-0x1B - Reserved
    nullptr, nullptr, nullptr, nullptr,    // 0x1C-0x1F - Reserved
    &CPU::handleLB,         // 0x20 - LB
    &CPU::handleLH,         // 0x21 - LH
    &CPU::handleLWL,        // 0x22 - LWL
    &CPU::handleLW,         // 0x23 - LW
    &CPU::handleLBU,        // 0x24 - LBU
    &CPU::handleLHU,        // 0x25 - LHU
    &CPU::handleLWR,        // 0x26 - LWR
    nullptr,                // 0x27 - Reserved
    &CPU::handleSB,         // 0x28 - SB
    &CPU::handleSH,         // 0x29 - SH
    &CPU::handleSWL,        // 0x2A - SWL
    &CPU::handleSW,         // 0x2B - SW
    nullptr, nullptr, nullptr, nullptr,    // 0x2C-0x2F - Reserved/SWR
    nullptr, nullptr, nullptr, nullptr,    // 0x30-0x33 - Reserved/LWC2
    nullptr, nullptr, nullptr, nullptr,    // 0x34-0x37 - Reserved
    nullptr, nullptr, nullptr, nullptr,    // 0x38-0x3B - Reserved/SWC2
    nullptr, nullptr, nullptr, nullptr,    // 0x3C-0x3F - Reserved
};

const CPU::InstructionHandler CPU::s_specialHandlers[64] = {
    &CPU::handleSLL,        // 0x00 - SLL
    nullptr,                // 0x01 - Reserved
    &CPU::handleSRL,        // 0x02 - SRL
    &CPU::handleSRA,        // 0x03 - SRA
    &CPU::handleSLLV,       // 0x04 - SLLV
    nullptr,                // 0x05 - Reserved
    &CPU::handleSRLV,       // 0x06 - SRLV
    &CPU::handleSRAV,       // 0x07 - SRAV
    &CPU::handleJR,         // 0x08 - JR
    &CPU::handleJALR,       // 0x09 - JALR
    nullptr, nullptr,       // 0x0A-0x0B - Reserved
    &CPU::handleSYSCALL,    // 0x0C - SYSCALL
    &CPU::handleBREAK,      // 0x0D - BREAK
    nullptr, nullptr,       // 0x0E-0x0F - Reserved
    &CPU::handleMFHI,       // 0x10 - MFHI
    &CPU::handleMTHI,       // 0x11 - MTHI
    &CPU::handleMFLO,       // 0x12 - MFLO
    &CPU::handleMTLO,       // 0x13 - MTLO
    nullptr, nullptr, nullptr, nullptr,    // 0x14-0x17 - Reserved
    &CPU::handleMULT,       // 0x18 - MULT
    &CPU::handleMULTU,      // 0x19 - MULTU
    &CPU::handleDIV,        // 0x1A - DIV
    &CPU::handleDIVU,       // 0x1B - DIVU
    nullptr, nullptr, nullptr, nullptr,    // 0x1C-0x1F - Reserved
    &CPU::handleADD,        // 0x20 - ADD
    &CPU::handleADDU,       // 0x21 - ADDU
    &CPU::handleSUB,        // 0x22 - SUB
    &CPU::handleSUBU,       // 0x23 - SUBU
    &CPU::handleAND,        // 0x24 - AND
    &CPU::handleOR,         // 0x25 - OR
    &CPU::handleXOR,        // 0x26 - XOR
    &CPU::handleNOR,        // 0x27 - NOR
    nullptr, nullptr,       // 0x28-0x29 - Reserved
    &CPU::handleSLT,        // 0x2A - SLT
    &CPU::handleSLTU,       // 0x2B - SLTU
    nullptr, nullptr, nullptr, nullptr,    // 0x2C-0x2F - Reserved
    nullptr, nullptr, nullptr, nullptr,    // 0x30-0x33 - Reserved
    nullptr, nullptr, nullptr, nullptr,    // 0x34-0x37 - Reserved
    nullptr, nullptr, nullptr, nullptr,    // 0x38-0x3B - Reserved
    nullptr, nullptr, nullptr, nullptr,    // 0x3C-0x3F - Reserved
};

} // namespace ZonistationOne