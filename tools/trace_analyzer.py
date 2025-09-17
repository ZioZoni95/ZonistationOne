#!/usr/bin/env python3
"""
CPU Execution Trace Analyzer for PlayStation 1 Emulator
Parses CPU execution logs to analyze instruction patterns and frequency

Usage: python3 trace_analyzer.py [log_file]
"""

import sys
import re
import argparse
from collections import defaultdict, Counter

class TraceAnalyzer:
    def __init__(self):
        self.instructions = []
        self.instruction_counts = Counter()
        self.pc_addresses = []
        self.register_changes = defaultdict(list)
        self.memory_accesses = defaultdict(list)
        
    def parse_log_file(self, filename):
        """Parse emulator log file for CPU trace information"""
        try:
            with open(filename, 'r') as f:
                for line_num, line in enumerate(f, 1):
                    line = line.strip()
                    if not line:
                        continue
                        
                    # Parse CPU instruction execution: [CPU] PC=0xBFC00000, INSTR=0x3C080013
                    cpu_match = re.match(r'\[CPU\] PC=(0x[0-9A-Fa-f]+), INSTR=(0x[0-9A-Fa-f]+)', line)
                    if cpu_match:
                        pc = int(cpu_match.group(1), 16)
                        instr = int(cpu_match.group(2), 16)
                        self.instructions.append((pc, instr))
                        self.pc_addresses.append(pc)
                        continue
                    
                    # Parse instruction execution: [CPU] LUI r8, 0x0013 -> 0x00130000
                    instr_match = re.match(r'\[CPU\] ([A-Z]+) (.+)', line)
                    if instr_match:
                        opcode = instr_match.group(1)
                        params = instr_match.group(2)
                        self.instruction_counts[opcode] += 1
                        
                        # Extract register writes
                        reg_write = re.search(r'-> (0x[0-9A-Fa-f]+)', params)
                        if reg_write:
                            value = int(reg_write.group(1), 16)
                            # Extract register name
                            reg_match = re.search(r'r(\d+)', params)
                            if reg_match:
                                reg_num = int(reg_match.group(1))
                                self.register_changes[reg_num].append(value)
                        continue
                    
                    # Parse memory operations: [MEMORY] Read/Write
                    mem_match = re.match(r'\[MEMORY\] (.+)', line)
                    if mem_match:
                        mem_op = mem_match.group(1)
                        # Extract address from memory operations
                        addr_match = re.search(r'at (0x[0-9A-Fa-f]+)', mem_op)
                        if addr_match:
                            addr = int(addr_match.group(1), 16)
                            self.memory_accesses[addr].append(mem_op)
                        continue
                        
        except FileNotFoundError:
            print(f"Error: Log file '{filename}' not found")
            sys.exit(1)
        except Exception as e:
            print(f"Error parsing log file: {e}")
            sys.exit(1)
    
    def analyze_instruction_flow(self):
        """Analyze instruction execution flow"""
        if not self.instructions:
            return
            
        print("=== INSTRUCTION EXECUTION FLOW ===")
        print(f"Total instructions executed: {len(self.instructions)}")
        
        # Show first 20 instructions
        print("\nFirst 20 instructions:")
        for i, (pc, instr) in enumerate(self.instructions[:20]):
            print(f"{i+1:3d}: PC=0x{pc:08X} INSTR=0x{instr:08X}")
        
        # Analyze PC progression
        if len(self.pc_addresses) > 1:
            consecutive = 0
            jumps = 0
            for i in range(1, len(self.pc_addresses)):
                if self.pc_addresses[i] == self.pc_addresses[i-1] + 4:
                    consecutive += 1
                else:
                    jumps += 1
            
            print(f"\nExecution pattern:")
            print(f"  Consecutive instructions: {consecutive}")
            print(f"  Jumps/branches: {jumps}")
            
        # Find most common PC addresses
        pc_counter = Counter(self.pc_addresses)
        print(f"\nMost frequently executed addresses:")
        for pc, count in pc_counter.most_common(10):
            print(f"  0x{pc:08X}: {count} times")
    
    def analyze_instructions(self):
        """Analyze instruction usage statistics"""
        if not self.instruction_counts:
            return
            
        print("\n=== INSTRUCTION USAGE STATISTICS ===")
        total = sum(self.instruction_counts.values())
        print(f"Total instructions: {total}")
        print(f"Unique instruction types: {len(self.instruction_counts)}")
        
        print(f"\nTop 15 most used instructions:")
        for instr, count in self.instruction_counts.most_common(15):
            percentage = (count / total) * 100
            print(f"  {instr:8s}: {count:6d} times ({percentage:5.1f}%)")
        
        # Show which instructions are missing (common MIPS instructions)
        common_mips = {
            "LUI", "ORI", "SW", "LW", "ADDI", "ADDIU", "BEQ", "BNE", 
            "J", "JAL", "JR", "NOP", "AND", "OR", "ADD", "ADDU"
        }
        
        used_instructions = set(self.instruction_counts.keys())
        missing = common_mips - used_instructions
        implemented = common_mips & used_instructions
        
        print(f"\nCommon MIPS instructions status:")
        print(f"  Implemented: {sorted(implemented)}")
        if missing:
            print(f"  Not yet used: {sorted(missing)}")
    
    def analyze_registers(self):
        """Analyze register usage patterns"""
        if not self.register_changes:
            return
            
        print("\n=== REGISTER USAGE ANALYSIS ===")
        
        # Register names for reference
        reg_names = [
            "$zero", "$at", "$v0", "$v1", "$a0", "$a1", "$a2", "$a3",
            "$t0", "$t1", "$t2", "$t3", "$t4", "$t5", "$t6", "$t7", 
            "$s0", "$s1", "$s2", "$s3", "$s4", "$s5", "$s6", "$s7",
            "$t8", "$t9", "$k0", "$k1", "$gp", "$sp", "$fp", "$ra"
        ]
        
        print("Most frequently modified registers:")
        reg_counts = [(reg, len(changes)) for reg, changes in self.register_changes.items()]
        reg_counts.sort(key=lambda x: x[1], reverse=True)
        
        for reg_num, count in reg_counts[:10]:
            reg_name = reg_names[reg_num] if reg_num < len(reg_names) else f"$r{reg_num}"
            values = self.register_changes[reg_num]
            unique_values = len(set(values))
            last_value = values[-1] if values else 0
            print(f"  {reg_name:6s} (r{reg_num:2d}): {count:3d} changes, {unique_values:3d} unique values, last=0x{last_value:08X}")
    
    def analyze_memory_access(self):
        """Analyze memory access patterns"""
        if not self.memory_accesses:
            return
            
        print("\n=== MEMORY ACCESS ANALYSIS ===")
        print(f"Unique memory addresses accessed: {len(self.memory_accesses)}")
        
        # Categorize memory accesses by region
        regions = {
            "BIOS": (0xBFC00000, 0xBFC80000),
            "RAM": (0x00000000, 0x00200000),  
            "Scratchpad": (0x1F800000, 0x1F800400),
            "Hardware": (0x1F801000, 0x1F803000),
        }
        
        region_counts = defaultdict(int)
        for addr in self.memory_accesses:
            for region, (start, end) in regions.items():
                if start <= addr < end:
                    region_counts[region] += len(self.memory_accesses[addr])
                    break
            else:
                region_counts["Other"] += len(self.memory_accesses[addr])
        
        print(f"\nMemory access by region:")
        for region, count in sorted(region_counts.items(), key=lambda x: x[1], reverse=True):
            print(f"  {region:12s}: {count:6d} accesses")
        
        # Show most frequently accessed addresses
        addr_counts = [(addr, len(ops)) for addr, ops in self.memory_accesses.items()]
        addr_counts.sort(key=lambda x: x[1], reverse=True)
        
        print(f"\nMost accessed memory addresses:")
        for addr, count in addr_counts[:10]:
            region = "Unknown"
            for reg, (start, end) in regions.items():
                if start <= addr < end:
                    region = reg
                    break
            print(f"  0x{addr:08X} ({region:10s}): {count:3d} accesses")
    
    def generate_report(self):
        """Generate complete analysis report"""
        print("PlayStation 1 Emulator - CPU Trace Analysis Report")
        print("=" * 60)
        
        self.analyze_instruction_flow()
        self.analyze_instructions()
        self.analyze_registers()
        self.analyze_memory_access()
        
        print("\n" + "=" * 60)
        print("Analysis complete")

def main():
    parser = argparse.ArgumentParser(description="PlayStation 1 CPU Trace Analyzer")
    parser.add_argument("logfile", nargs='?', default="emulator.log",
                       help="Emulator log file to analyze (default: emulator.log)")
    parser.add_argument("--instructions-only", action="store_true",
                       help="Show only instruction analysis")
    args = parser.parse_args()
    
    analyzer = TraceAnalyzer()
    analyzer.parse_log_file(args.logfile)
    
    if args.instructions_only:
        analyzer.analyze_instructions()
    else:
        analyzer.generate_report()

if __name__ == "__main__":
    main()