#!/usr/bin/env python3
"""
BIOS Disassembler for PlayStation 1 Emulator
Disassembles MIPS instructions from PlayStation BIOS ROM

Usage: python3 bios_disasm.py [options]
"""

import sys
import struct
import argparse

# MIPS instruction opcodes and names
MIPS_OPCODES = {
    0x00: "SPECIAL",  # Special function (see SPECIAL_FUNCTIONS)
    0x01: "REGIMM",   # Register immediate (BEQ/BNE variants)
    0x02: "J",        # Jump
    0x03: "JAL",      # Jump and Link
    0x04: "BEQ",      # Branch Equal
    0x05: "BNE",      # Branch Not Equal
    0x06: "BLEZ",     # Branch Less/Equal Zero
    0x07: "BGTZ",     # Branch Greater Than Zero
    0x08: "ADDI",     # Add Immediate
    0x09: "ADDIU",    # Add Immediate Unsigned
    0x0A: "SLTI",     # Set Less Than Immediate
    0x0B: "SLTIU",    # Set Less Than Immediate Unsigned
    0x0C: "ANDI",     # AND Immediate
    0x0D: "ORI",      # OR Immediate
    0x0E: "XORI",     # XOR Immediate
    0x0F: "LUI",      # Load Upper Immediate
    0x10: "COP0",     # Coprocessor 0
    0x11: "COP1",     # Coprocessor 1
    0x12: "COP2",     # Coprocessor 2 (GTE)
    0x13: "COP3",     # Coprocessor 3
    0x20: "LB",       # Load Byte
    0x21: "LH",       # Load Halfword
    0x22: "LWL",      # Load Word Left
    0x23: "LW",       # Load Word
    0x24: "LBU",      # Load Byte Unsigned
    0x25: "LHU",      # Load Halfword Unsigned
    0x26: "LWR",      # Load Word Right
    0x28: "SB",       # Store Byte
    0x29: "SH",       # Store Halfword
    0x2A: "SWL",      # Store Word Left
    0x2B: "SW",       # Store Word
    0x2E: "SWR",      # Store Word Right
    0x30: "LWC0",     # Load Word Coprocessor 0
    0x31: "LWC1",     # Load Word Coprocessor 1
    0x32: "LWC2",     # Load Word Coprocessor 2
    0x33: "LWC3",     # Load Word Coprocessor 3
    0x38: "SWC0",     # Store Word Coprocessor 0
    0x39: "SWC1",     # Store Word Coprocessor 1
    0x3A: "SWC2",     # Store Word Coprocessor 2
    0x3B: "SWC3",     # Store Word Coprocessor 3
}

# SPECIAL function codes (when opcode = 0x00)
SPECIAL_FUNCTIONS = {
    0x00: "SLL",      # Shift Left Logical
    0x02: "SRL",      # Shift Right Logical
    0x03: "SRA",      # Shift Right Arithmetic
    0x04: "SLLV",     # Shift Left Logical Variable
    0x06: "SRLV",     # Shift Right Logical Variable
    0x07: "SRAV",     # Shift Right Arithmetic Variable
    0x08: "JR",       # Jump Register
    0x09: "JALR",     # Jump and Link Register
    0x0C: "SYSCALL",  # System Call
    0x0D: "BREAK",    # Break
    0x10: "MFHI",     # Move From HI
    0x11: "MTHI",     # Move To HI
    0x12: "MFLO",     # Move From LO
    0x13: "MTLO",     # Move To LO
    0x18: "MULT",     # Multiply
    0x19: "MULTU",    # Multiply Unsigned
    0x1A: "DIV",      # Divide
    0x1B: "DIVU",     # Divide Unsigned
    0x20: "ADD",      # Add
    0x21: "ADDU",     # Add Unsigned
    0x22: "SUB",      # Subtract
    0x23: "SUBU",     # Subtract Unsigned
    0x24: "AND",      # AND
    0x25: "OR",       # OR
    0x26: "XOR",      # XOR
    0x27: "NOR",      # NOR
    0x2A: "SLT",      # Set Less Than
    0x2B: "SLTU",     # Set Less Than Unsigned
}

# Register names
REGISTER_NAMES = [
    "$zero", "$at", "$v0", "$v1", "$a0", "$a1", "$a2", "$a3",
    "$t0", "$t1", "$t2", "$t3", "$t4", "$t5", "$t6", "$t7",
    "$s0", "$s1", "$s2", "$s3", "$s4", "$s5", "$s6", "$s7",
    "$t8", "$t9", "$k0", "$k1", "$gp", "$sp", "$fp", "$ra"
]

def disassemble_instruction(instruction, address):
    """Disassemble a single MIPS instruction"""
    
    # Extract instruction fields
    opcode = (instruction >> 26) & 0x3F
    rs = (instruction >> 21) & 0x1F
    rt = (instruction >> 16) & 0x1F
    rd = (instruction >> 11) & 0x1F
    shamt = (instruction >> 6) & 0x1F
    funct = instruction & 0x3F
    imm = instruction & 0xFFFF
    imm_signed = struct.unpack('>h', struct.pack('>H', imm))[0]
    target = instruction & 0x3FFFFFF
    
    # Get instruction name
    if opcode in MIPS_OPCODES:
        name = MIPS_OPCODES[opcode]
    else:
        return f"UNKNOWN_0x{opcode:02X}"
    
    # Handle special cases
    if opcode == 0x00:  # SPECIAL
        if funct in SPECIAL_FUNCTIONS:
            name = SPECIAL_FUNCTIONS[funct]
            
            if name in ["SLL", "SRL", "SRA"]:
                if instruction == 0:  # NOP (SLL $zero, $zero, 0)
                    return "NOP"
                return f"{name} {REGISTER_NAMES[rd]}, {REGISTER_NAMES[rt]}, {shamt}"
            elif name in ["SLLV", "SRLV", "SRAV"]:
                return f"{name} {REGISTER_NAMES[rd]}, {REGISTER_NAMES[rt]}, {REGISTER_NAMES[rs]}"
            elif name == "JR":
                return f"{name} {REGISTER_NAMES[rs]}"
            elif name == "JALR":
                if rd == 31:  # Default return register
                    return f"{name} {REGISTER_NAMES[rs]}"
                return f"{name} {REGISTER_NAMES[rd]}, {REGISTER_NAMES[rs]}"
            elif name in ["MFHI", "MFLO"]:
                return f"{name} {REGISTER_NAMES[rd]}"
            elif name in ["MTHI", "MTLO"]:
                return f"{name} {REGISTER_NAMES[rs]}"
            elif name in ["MULT", "MULTU", "DIV", "DIVU"]:
                return f"{name} {REGISTER_NAMES[rs]}, {REGISTER_NAMES[rt]}"
            elif name in ["ADD", "ADDU", "SUB", "SUBU", "AND", "OR", "XOR", "NOR", "SLT", "SLTU"]:
                return f"{name} {REGISTER_NAMES[rd]}, {REGISTER_NAMES[rs]}, {REGISTER_NAMES[rt]}"
            else:
                return f"{name}"
        else:
            return f"SPECIAL_UNK_0x{funct:02X}"
    
    # Regular instructions
    elif name == "J":
        target_addr = (address & 0xF0000000) | (target << 2)
        return f"{name} 0x{target_addr:08X}"
    elif name == "JAL":
        target_addr = (address & 0xF0000000) | (target << 2)
        return f"{name} 0x{target_addr:08X}"
    elif name in ["BEQ", "BNE"]:
        branch_addr = address + 4 + (imm_signed << 2)
        return f"{name} {REGISTER_NAMES[rs]}, {REGISTER_NAMES[rt]}, 0x{branch_addr:08X}"
    elif name in ["BLEZ", "BGTZ"]:
        branch_addr = address + 4 + (imm_signed << 2)
        return f"{name} {REGISTER_NAMES[rs]}, 0x{branch_addr:08X}"
    elif name in ["ADDI", "ADDIU", "SLTI", "SLTIU"]:
        return f"{name} {REGISTER_NAMES[rt]}, {REGISTER_NAMES[rs]}, {imm_signed}"
    elif name in ["ANDI", "ORI", "XORI"]:
        return f"{name} {REGISTER_NAMES[rt]}, {REGISTER_NAMES[rs]}, 0x{imm:04X}"
    elif name == "LUI":
        return f"{name} {REGISTER_NAMES[rt]}, 0x{imm:04X}"
    elif name in ["LB", "LH", "LW", "LBU", "LHU", "LWL", "LWR"]:
        return f"{name} {REGISTER_NAMES[rt]}, {imm_signed}({REGISTER_NAMES[rs]})"
    elif name in ["SB", "SH", "SW", "SWL", "SWR"]:
        return f"{name} {REGISTER_NAMES[rt]}, {imm_signed}({REGISTER_NAMES[rs]})"
    else:
        return f"{name} [TODO: decode parameters]"

def main():
    parser = argparse.ArgumentParser(description="PlayStation 1 BIOS Disassembler")
    parser.add_argument("bios", nargs='?', default="roms/SCPH1001.BIN", 
                       help="BIOS ROM file (default: roms/SCPH1001.BIN)")
    parser.add_argument("--start", type=lambda x: int(x, 0), default=0,
                       help="Start offset in BIOS (default: 0)")
    parser.add_argument("--count", type=int, default=50,
                       help="Number of instructions to disassemble (default: 50)")
    parser.add_argument("--address", type=lambda x: int(x, 0), default=0xBFC00000,
                       help="Starting address for display (default: 0xBFC00000)")
    args = parser.parse_args()
    
    try:
        with open(args.bios, "rb") as f:
            f.seek(args.start)
            data = f.read(args.count * 4)
            
        if len(data) < args.count * 4:
            print(f"Warning: Only {len(data)//4} instructions available")
            args.count = len(data) // 4
        
        print(f"PlayStation 1 BIOS Disassembly")
        print(f"File: {args.bios}")
        print(f"Address Range: 0x{args.address:08X} - 0x{args.address + args.count*4:08X}")
        print("=" * 80)
        
        for i in range(args.count):
            offset = i * 4
            instruction = struct.unpack(">I", data[offset:offset+4])[0]  # Big-endian
            address = args.address + offset
            
            disasm = disassemble_instruction(instruction, address)
            print(f"0x{address:08X}: 0x{instruction:08X}  {disasm}")
            
        print("=" * 80)
        print(f"Disassembled {args.count} instructions")
        
    except FileNotFoundError:
        print(f"Error: BIOS file '{args.bios}' not found")
        print("Make sure the BIOS is in the roms/ directory")
        sys.exit(1)
    except Exception as e:
        print(f"Error: {e}")
        sys.exit(1)

if __name__ == "__main__":
    main()