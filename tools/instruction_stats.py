#!/usr/bin/env python3
"""
Instruction Statistics Analyzer for PlayStation 1 Emulator
Analyzes instruction implementation priority based on BIOS and game usage

Usage: python3 instruction_stats.py [options]
"""

import sys
import struct
import argparse
from collections import Counter, defaultdict

# Complete MIPS instruction set for PlayStation 1
MIPS_INSTRUCTION_INFO = {
    # Arithmetic and Logical
    "ADD": {"priority": "HIGH", "complexity": "EASY", "description": "Add (with overflow check)"},
    "ADDI": {"priority": "HIGH", "complexity": "EASY", "description": "Add Immediate"},  
    "ADDIU": {"priority": "HIGH", "complexity": "EASY", "description": "Add Immediate Unsigned"},
    "ADDU": {"priority": "HIGH", "complexity": "EASY", "description": "Add Unsigned"},
    "AND": {"priority": "HIGH", "complexity": "EASY", "description": "Bitwise AND"},
    "ANDI": {"priority": "HIGH", "complexity": "EASY", "description": "AND Immediate"},
    "NOR": {"priority": "MEDIUM", "complexity": "EASY", "description": "Bitwise NOR"},
    "OR": {"priority": "HIGH", "complexity": "EASY", "description": "Bitwise OR"},
    "ORI": {"priority": "HIGH", "complexity": "EASY", "description": "OR Immediate"},
    "SLT": {"priority": "HIGH", "complexity": "EASY", "description": "Set Less Than"},
    "SLTI": {"priority": "HIGH", "complexity": "EASY", "description": "Set Less Than Immediate"},
    "SLTIU": {"priority": "HIGH", "complexity": "EASY", "description": "Set Less Than Immediate Unsigned"},
    "SLTU": {"priority": "HIGH", "complexity": "EASY", "description": "Set Less Than Unsigned"},
    "SUB": {"priority": "HIGH", "complexity": "EASY", "description": "Subtract"},
    "SUBU": {"priority": "HIGH", "complexity": "EASY", "description": "Subtract Unsigned"},
    "XOR": {"priority": "MEDIUM", "complexity": "EASY", "description": "Bitwise XOR"},
    "XORI": {"priority": "MEDIUM", "complexity": "EASY", "description": "XOR Immediate"},
    
    # Shift Operations
    "SLL": {"priority": "HIGH", "complexity": "EASY", "description": "Shift Left Logical"},
    "SLLV": {"priority": "MEDIUM", "complexity": "EASY", "description": "Shift Left Logical Variable"},
    "SRA": {"priority": "MEDIUM", "complexity": "EASY", "description": "Shift Right Arithmetic"},
    "SRAV": {"priority": "MEDIUM", "complexity": "EASY", "description": "Shift Right Arithmetic Variable"},
    "SRL": {"priority": "HIGH", "complexity": "EASY", "description": "Shift Right Logical"},
    "SRLV": {"priority": "MEDIUM", "complexity": "EASY", "description": "Shift Right Logical Variable"},
    
    # Load/Store Operations
    "LB": {"priority": "HIGH", "complexity": "MEDIUM", "description": "Load Byte"},
    "LBU": {"priority": "HIGH", "complexity": "MEDIUM", "description": "Load Byte Unsigned"},
    "LH": {"priority": "HIGH", "complexity": "MEDIUM", "description": "Load Halfword"},
    "LHU": {"priority": "HIGH", "complexity": "MEDIUM", "description": "Load Halfword Unsigned"},
    "LUI": {"priority": "CRITICAL", "complexity": "EASY", "description": "Load Upper Immediate"},
    "LW": {"priority": "CRITICAL", "complexity": "MEDIUM", "description": "Load Word"},
    "LWL": {"priority": "LOW", "complexity": "HARD", "description": "Load Word Left"},
    "LWR": {"priority": "LOW", "complexity": "HARD", "description": "Load Word Right"},
    "SB": {"priority": "HIGH", "complexity": "MEDIUM", "description": "Store Byte"},
    "SH": {"priority": "HIGH", "complexity": "MEDIUM", "description": "Store Halfword"},
    "SW": {"priority": "CRITICAL", "complexity": "MEDIUM", "description": "Store Word"},
    "SWL": {"priority": "LOW", "complexity": "HARD", "description": "Store Word Left"},
    "SWR": {"priority": "LOW", "complexity": "HARD", "description": "Store Word Right"},
    
    # Branch/Jump Operations
    "BEQ": {"priority": "CRITICAL", "complexity": "MEDIUM", "description": "Branch Equal"},
    "BGEZ": {"priority": "HIGH", "complexity": "MEDIUM", "description": "Branch Greater/Equal Zero"},
    "BGTZ": {"priority": "MEDIUM", "complexity": "MEDIUM", "description": "Branch Greater Than Zero"},
    "BLEZ": {"priority": "MEDIUM", "complexity": "MEDIUM", "description": "Branch Less/Equal Zero"},
    "BLTZ": {"priority": "HIGH", "complexity": "MEDIUM", "description": "Branch Less Than Zero"},
    "BNE": {"priority": "CRITICAL", "complexity": "MEDIUM", "description": "Branch Not Equal"},
    "J": {"priority": "CRITICAL", "complexity": "MEDIUM", "description": "Jump"},
    "JAL": {"priority": "CRITICAL", "complexity": "MEDIUM", "description": "Jump and Link"},
    "JALR": {"priority": "HIGH", "complexity": "MEDIUM", "description": "Jump and Link Register"},
    "JR": {"priority": "CRITICAL", "complexity": "MEDIUM", "description": "Jump Register"},
    
    # Multiply/Divide
    "DIV": {"priority": "HIGH", "complexity": "HARD", "description": "Divide"},
    "DIVU": {"priority": "HIGH", "complexity": "HARD", "description": "Divide Unsigned"},
    "MFHI": {"priority": "HIGH", "complexity": "EASY", "description": "Move From HI"},
    "MFLO": {"priority": "HIGH", "complexity": "EASY", "description": "Move From LO"},
    "MTHI": {"priority": "MEDIUM", "complexity": "EASY", "description": "Move To HI"},
    "MTLO": {"priority": "MEDIUM", "complexity": "EASY", "description": "Move To LO"},
    "MULT": {"priority": "HIGH", "complexity": "MEDIUM", "description": "Multiply"},
    "MULTU": {"priority": "HIGH", "complexity": "MEDIUM", "description": "Multiply Unsigned"},
    
    # System/Special
    "BREAK": {"priority": "LOW", "complexity": "EASY", "description": "Breakpoint"},
    "NOP": {"priority": "HIGH", "complexity": "TRIVIAL", "description": "No Operation"},
    "SYSCALL": {"priority": "MEDIUM", "complexity": "MEDIUM", "description": "System Call"},
    
    # Coprocessor Operations
    "MFC0": {"priority": "HIGH", "complexity": "MEDIUM", "description": "Move From Coprocessor 0"},
    "MTC0": {"priority": "HIGH", "complexity": "MEDIUM", "description": "Move To Coprocessor 0"},
    "RFE": {"priority": "MEDIUM", "complexity": "HARD", "description": "Return From Exception"},
    
    # Coprocessor 2 (GTE) Operations  
    "LWC2": {"priority": "MEDIUM", "complexity": "MEDIUM", "description": "Load Word Coprocessor 2"},
    "SWC2": {"priority": "MEDIUM", "complexity": "MEDIUM", "description": "Store Word Coprocessor 2"},
    "CTC2": {"priority": "LOW", "complexity": "MEDIUM", "description": "Copy To Coprocessor 2"},
    "CFC2": {"priority": "LOW", "complexity": "MEDIUM", "description": "Copy From Coprocessor 2"},
}

class InstructionStatsAnalyzer:
    def __init__(self):
        self.bios_usage = Counter()
        self.implementation_status = {}
        self.priority_groups = defaultdict(list)
        
        # Group instructions by priority
        for instr, info in MIPS_INSTRUCTION_INFO.items():
            self.priority_groups[info["priority"]].append(instr)
    
    def analyze_bios_usage(self, bios_file, instruction_count=1000):
        """Analyze which instructions are used in BIOS startup"""
        try:
            with open(bios_file, 'rb') as f:
                data = f.read(instruction_count * 4)
            
            print(f"Analyzing first {instruction_count} instructions from BIOS...")
            
            for i in range(0, len(data) - 3, 4):
                instr = struct.unpack(">I", data[i:i+4])[0]
                opcode = (instr >> 26) & 0x3F
                funct = instr & 0x3F
                
                # Decode instruction
                instr_name = self.decode_instruction(opcode, funct, instr)
                if instr_name:
                    self.bios_usage[instr_name] += 1
                    
        except FileNotFoundError:
            print(f"Error: BIOS file '{bios_file}' not found")
        except Exception as e:
            print(f"Error analyzing BIOS: {e}")
    
    def decode_instruction(self, opcode, funct, full_instr):
        """Decode MIPS instruction to name"""
        # Handle SPECIAL instructions (opcode 0x00)
        if opcode == 0x00:
            special_map = {
                0x00: "SLL", 0x02: "SRL", 0x03: "SRA", 0x04: "SLLV",
                0x06: "SRLV", 0x07: "SRAV", 0x08: "JR", 0x09: "JALR",
                0x0C: "SYSCALL", 0x0D: "BREAK", 0x10: "MFHI", 0x11: "MTHI",
                0x12: "MFLO", 0x13: "MTLO", 0x18: "MULT", 0x19: "MULTU",
                0x1A: "DIV", 0x1B: "DIVU", 0x20: "ADD", 0x21: "ADDU",
                0x22: "SUB", 0x23: "SUBU", 0x24: "AND", 0x25: "OR",
                0x26: "XOR", 0x27: "NOR", 0x2A: "SLT", 0x2B: "SLTU"
            }
            
            if full_instr == 0:  # NOP is encoded as SLL $zero, $zero, 0
                return "NOP"
            
            return special_map.get(funct)
        
        # Regular instructions
        opcode_map = {
            0x02: "J", 0x03: "JAL", 0x04: "BEQ", 0x05: "BNE",
            0x06: "BLEZ", 0x07: "BGTZ", 0x08: "ADDI", 0x09: "ADDIU",
            0x0A: "SLTI", 0x0B: "SLTIU", 0x0C: "ANDI", 0x0D: "ORI",
            0x0E: "XORI", 0x0F: "LUI", 0x20: "LB", 0x21: "LH",
            0x22: "LWL", 0x23: "LW", 0x24: "LBU", 0x25: "LHU",
            0x26: "LWR", 0x28: "SB", 0x29: "SH", 0x2A: "SWL",
            0x2B: "SW", 0x2E: "SWR", 0x32: "LWC2", 0x3A: "SWC2"
        }
        
        # Handle REGIMM instructions (opcode 0x01)
        if opcode == 0x01:
            rt = (full_instr >> 16) & 0x1F
            if rt == 0x00: return "BLTZ"
            elif rt == 0x01: return "BGEZ"
            
        # Handle COP0 instructions (opcode 0x10)
        if opcode == 0x10:
            rs = (full_instr >> 21) & 0x1F
            if rs == 0x00: return "MFC0"
            elif rs == 0x04: return "MTC0"
            elif rs == 0x10: return "RFE"
        
        return opcode_map.get(opcode)
    
    def check_current_implementation(self, emulator_source_dir="src"):
        """Check which instructions are currently implemented"""
        try:
            import os
            cpu_file = os.path.join(emulator_source_dir, "psx_cpu.c")
            
            if not os.path.exists(cpu_file):
                print(f"CPU source file not found: {cpu_file}")
                return
            
            with open(cpu_file, 'r') as f:
                source_code = f.read()
            
            # Look for implemented instructions in switch statements
            for instr in MIPS_INSTRUCTION_INFO.keys():
                if f'"{instr}"' in source_code or f"case {instr}" in source_code:
                    self.implementation_status[instr] = "IMPLEMENTED"
                else:
                    self.implementation_status[instr] = "TODO"
                    
        except Exception as e:
            print(f"Warning: Could not check implementation status: {e}")
    
    def generate_priority_report(self):
        """Generate instruction implementation priority report"""
        print("\n" + "="*80)
        print("INSTRUCTION IMPLEMENTATION PRIORITY ANALYSIS")
        print("="*80)
        
        priority_order = ["CRITICAL", "HIGH", "MEDIUM", "LOW"]
        
        for priority in priority_order:
            instructions = self.priority_groups[priority]
            print(f"\n{priority} PRIORITY ({len(instructions)} instructions):")
            print("-" * 50)
            
            # Sort by BIOS usage frequency
            instr_with_usage = []
            for instr in instructions:
                usage = self.bios_usage.get(instr, 0)
                info = MIPS_INSTRUCTION_INFO[instr]
                status = self.implementation_status.get(instr, "UNKNOWN")
                instr_with_usage.append((instr, usage, info, status))
            
            # Sort by usage (most used first)
            instr_with_usage.sort(key=lambda x: x[1], reverse=True)
            
            for instr, usage, info, status in instr_with_usage:
                complexity = info["complexity"]
                description = info["description"]
                
                status_indicator = {
                    "IMPLEMENTED": "✓", 
                    "TODO": "✗", 
                    "UNKNOWN": "?"
                }.get(status, "?")
                
                print(f"  {status_indicator} {instr:8s} ({complexity:6s}) - {description:30s} [Used {usage:3d} times]")
    
    def generate_implementation_roadmap(self):
        """Generate suggested implementation roadmap"""
        print("\n" + "="*80)
        print("SUGGESTED IMPLEMENTATION ROADMAP")
        print("="*80)
        
        # Phase 1: Critical instructions (needed for basic BIOS execution)
        critical_used = []
        critical_unused = []
        
        for instr in self.priority_groups["CRITICAL"]:
            usage = self.bios_usage.get(instr, 0)
            if usage > 0:
                critical_used.append((instr, usage))
            else:
                critical_unused.append(instr)
        
        critical_used.sort(key=lambda x: x[1], reverse=True)
        
        print("\nPHASE 1 - CRITICAL INSTRUCTIONS (Implement First)")
        print("These are essential for BIOS execution:")
        for i, (instr, usage) in enumerate(critical_used, 1):
            info = MIPS_INSTRUCTION_INFO[instr]
            print(f"  {i:2d}. {instr:8s} - {info['description']} (Used {usage} times)")
        
        # Phase 2: High priority instructions used in BIOS
        high_used = [(instr, self.bios_usage.get(instr, 0)) 
                     for instr in self.priority_groups["HIGH"] 
                     if self.bios_usage.get(instr, 0) > 0]
        high_used.sort(key=lambda x: x[1], reverse=True)
        
        print("\nPHASE 2 - HIGH PRIORITY INSTRUCTIONS")
        print("Implement after Phase 1 for extended BIOS functionality:")
        for i, (instr, usage) in enumerate(high_used[:10], 1):  # Top 10
            info = MIPS_INSTRUCTION_INFO[instr]
            print(f"  {i:2d}. {instr:8s} - {info['description']} (Used {usage} times)")
        
        # Phase 3: Game compatibility
        print("\nPHASE 3 - GAME COMPATIBILITY")
        print("These will be needed for running games:")
        
        game_critical = ["DIV", "DIVU", "MULT", "MULTU", "MFHI", "MFLO", 
                        "BEQ", "BNE", "BGEZ", "BLTZ", "MFC0", "MTC0"]
        
        for i, instr in enumerate(game_critical, 1):
            if instr in MIPS_INSTRUCTION_INFO:
                info = MIPS_INSTRUCTION_INFO[instr]
                usage = self.bios_usage.get(instr, 0)
                print(f"  {i:2d}. {instr:8s} - {info['description']} (BIOS usage: {usage})")
    
    def generate_quick_wins(self):
        """Identify easy instructions to implement next"""
        print("\n" + "="*80)
        print("QUICK WINS - EASY INSTRUCTIONS TO IMPLEMENT")
        print("="*80)
        
        # Find easy instructions that are used in BIOS but not implemented
        easy_wins = []
        for instr, usage in self.bios_usage.most_common():
            if instr in MIPS_INSTRUCTION_INFO:
                info = MIPS_INSTRUCTION_INFO[instr]
                status = self.implementation_status.get(instr, "TODO")
                
                if (info["complexity"] in ["TRIVIAL", "EASY"] and 
                    status == "TODO" and usage > 0):
                    easy_wins.append((instr, usage, info))
        
        print("These instructions are easy to implement and used by BIOS:")
        for i, (instr, usage, info) in enumerate(easy_wins[:10], 1):
            print(f"  {i:2d}. {instr:8s} ({info['complexity']:8s}) - {info['description']} [Used {usage} times]")

def main():
    parser = argparse.ArgumentParser(description="PlayStation 1 Instruction Statistics Analyzer")
    parser.add_argument("--bios", default="roms/SCPH1001.BIN",
                       help="BIOS file to analyze (default: roms/SCPH1001.BIN)")
    parser.add_argument("--instructions", type=int, default=1000,
                       help="Number of BIOS instructions to analyze (default: 1000)")
    parser.add_argument("--source-dir", default="src",
                       help="Source directory to check implementation status")
    args = parser.parse_args()
    
    analyzer = InstructionStatsAnalyzer()
    
    print("PlayStation 1 Instruction Priority Analysis")
    print("Analyzing BIOS usage patterns...")
    
    analyzer.analyze_bios_usage(args.bios, args.instructions)
    analyzer.check_current_implementation(args.source_dir)
    
    analyzer.generate_priority_report()
    analyzer.generate_implementation_roadmap()
    analyzer.generate_quick_wins()
    
    print("\n" + "="*80)
    print("Analysis complete. Use this report to prioritize CPU implementation.")

if __name__ == "__main__":
    main()