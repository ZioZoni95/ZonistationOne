#!/usr/bin/env python3
"""
Memory Dump Utility for PlayStation 1 Emulator
Extracts and analyzes memory regions from emulator state

Usage: python3 memory_dump.py [options]
"""

import sys
import struct
import argparse
import os

class MemoryDumper:
    def __init__(self):
        self.regions = {
            "BIOS": {
                "start": 0xBFC00000,
                "size": 0x80000,  # 512KB
                "description": "PlayStation BIOS ROM"
            },
            "RAM": {
                "start": 0x00000000, 
                "size": 0x200000,   # 2MB
                "description": "Main RAM"
            },
            "SCRATCHPAD": {
                "start": 0x1F800000,
                "size": 0x400,      # 1KB
                "description": "Scratchpad RAM"
            },
            "HARDWARE": {
                "start": 0x1F801000,
                "size": 0x2000,     # 8KB
                "description": "Hardware Registers"
            }
        }
    
    def dump_hex(self, data, start_addr=0, bytes_per_line=16):
        """Format binary data as hex dump"""
        lines = []
        for i in range(0, len(data), bytes_per_line):
            addr = start_addr + i
            chunk = data[i:i+bytes_per_line]
            
            # Hex bytes
            hex_bytes = ' '.join(f"{b:02X}" for b in chunk)
            hex_bytes = hex_bytes.ljust(bytes_per_line * 3 - 1)
            
            # ASCII representation
            ascii_chars = ''.join(chr(b) if 32 <= b <= 126 else '.' for b in chunk)
            
            lines.append(f"0x{addr:08X}: {hex_bytes} |{ascii_chars}|")
        
        return '\n'.join(lines)
    
    def dump_region_from_file(self, region_name, filename, output_file=None):
        """Dump a memory region from a binary file"""
        if region_name not in self.regions:
            print(f"Error: Unknown region '{region_name}'")
            print(f"Available regions: {', '.join(self.regions.keys())}")
            return False
        
        region = self.regions[region_name]
        
        try:
            with open(filename, 'rb') as f:
                # For BIOS file, read from beginning
                if region_name == "BIOS":
                    data = f.read(region["size"])
                else:
                    # For other regions, this would need memory dump format
                    print(f"Warning: {region_name} dump from file not implemented")
                    return False
                
            if len(data) == 0:
                print(f"Error: No data read from {filename}")
                return False
                
            # Generate dump
            print(f"Memory Dump: {region_name}")
            print(f"Description: {region['description']}")
            print(f"Address Range: 0x{region['start']:08X} - 0x{region['start'] + len(data):08X}")
            print(f"Size: {len(data)} bytes ({len(data)//1024}KB)" if len(data) >= 1024 else f"Size: {len(data)} bytes")
            print("=" * 80)
            
            hex_dump = self.dump_hex(data, region["start"])
            
            if output_file:
                with open(output_file, 'w') as f:
                    f.write(f"Memory Dump: {region_name}\n")
                    f.write(f"Description: {region['description']}\n")
                    f.write(f"Address Range: 0x{region['start']:08X} - 0x{region['start'] + len(data):08X}\n")
                    f.write("=" * 80 + "\n")
                    f.write(hex_dump)
                print(f"\nDump saved to: {output_file}")
            else:
                print(hex_dump)
                
            print("=" * 80)
            return True
            
        except FileNotFoundError:
            print(f"Error: File '{filename}' not found")
            return False
        except Exception as e:
            print(f"Error: {e}")
            return False
    
    def analyze_bios_structure(self, filename):
        """Analyze BIOS structure and find interesting sections"""
        try:
            with open(filename, 'rb') as f:
                data = f.read()
            
            print("BIOS Structure Analysis")
            print("=" * 50)
            
            # Look for text strings
            print("Text strings found in BIOS:")
            current_string = ""
            strings = []
            
            for i, byte in enumerate(data):
                if 32 <= byte <= 126:  # Printable ASCII
                    current_string += chr(byte)
                else:
                    if len(current_string) >= 4:  # Minimum string length
                        strings.append((0xBFC00000 + i - len(current_string), current_string))
                    current_string = ""
            
            # Show interesting strings
            for addr, string in strings[:20]:  # First 20 strings
                if any(keyword in string.upper() for keyword in ["SONY", "PLAYSTATION", "COPYRIGHT", "BIOS", "SYSTEM"]):
                    print(f"  0x{addr:08X}: \"{string}\"")
            
            # Analyze instruction patterns
            print(f"\nInstruction Analysis:")
            lui_count = 0
            ori_count = 0
            sw_count = 0
            
            for i in range(0, len(data) - 3, 4):
                instr = struct.unpack(">I", data[i:i+4])[0]
                opcode = (instr >> 26) & 0x3F
                
                if opcode == 0x0F:  # LUI
                    lui_count += 1
                elif opcode == 0x0D:  # ORI
                    ori_count += 1
                elif opcode == 0x2B:  # SW
                    sw_count += 1
            
            total_instructions = len(data) // 4
            print(f"  Total instructions: {total_instructions}")
            print(f"  LUI instructions: {lui_count} ({(lui_count/total_instructions)*100:.1f}%)")
            print(f"  ORI instructions: {ori_count} ({(ori_count/total_instructions)*100:.1f}%)")
            print(f"  SW instructions: {sw_count} ({(sw_count/total_instructions)*100:.1f}%)")
            
            # Entry points
            print(f"\nEntry Point Analysis:")
            entry_point = struct.unpack(">I", data[0:4])[0]
            print(f"  First instruction: 0x{entry_point:08X}")
            print(f"  Reset vector: 0xBFC00000")
            
        except Exception as e:
            print(f"Error analyzing BIOS: {e}")
    
    def extract_instructions(self, filename, start_offset=0, count=100):
        """Extract and display raw instructions from binary file"""
        try:
            with open(filename, 'rb') as f:
                f.seek(start_offset)
                data = f.read(count * 4)
            
            if len(data) < count * 4:
                actual_count = len(data) // 4
                print(f"Warning: Only {actual_count} instructions available")
                count = actual_count
            
            print(f"Raw Instructions Extract")
            print(f"File: {filename}")
            print(f"Offset: 0x{start_offset:X}")
            print(f"Count: {count} instructions")
            print("=" * 60)
            
            base_addr = 0xBFC00000 + start_offset
            
            for i in range(count):
                offset = i * 4
                instr = struct.unpack(">I", data[offset:offset+4])[0]
                addr = base_addr + offset
                
                # Basic instruction decode
                opcode = (instr >> 26) & 0x3F
                opcode_name = {
                    0x0F: "LUI", 0x0D: "ORI", 0x2B: "SW", 0x23: "LW",
                    0x08: "ADDI", 0x09: "ADDIU", 0x04: "BEQ", 0x05: "BNE",
                    0x02: "J", 0x03: "JAL"
                }.get(opcode, f"OP_{opcode:02X}")
                
                print(f"0x{addr:08X}: 0x{instr:08X} ({opcode_name})")
                
        except Exception as e:
            print(f"Error extracting instructions: {e}")

def main():
    parser = argparse.ArgumentParser(description="PlayStation 1 Memory Dump Utility")
    
    subparsers = parser.add_subparsers(dest='command', help='Available commands')
    
    # Dump command
    dump_parser = subparsers.add_parser('dump', help='Dump memory region')
    dump_parser.add_argument('region', choices=['BIOS', 'RAM', 'SCRATCHPAD', 'HARDWARE'],
                            help='Memory region to dump')
    dump_parser.add_argument('file', help='Source file (e.g., BIOS ROM)')
    dump_parser.add_argument('-o', '--output', help='Output file for dump')
    
    # Analyze command
    analyze_parser = subparsers.add_parser('analyze', help='Analyze BIOS structure')
    analyze_parser.add_argument('bios', nargs='?', default='roms/SCPH1001.BIN',
                               help='BIOS file to analyze')
    
    # Extract command
    extract_parser = subparsers.add_parser('extract', help='Extract raw instructions')
    extract_parser.add_argument('file', help='Binary file to extract from')
    extract_parser.add_argument('--offset', type=lambda x: int(x, 0), default=0,
                               help='Start offset in file')
    extract_parser.add_argument('--count', type=int, default=100,
                               help='Number of instructions to extract')
    
    args = parser.parse_args()
    
    if not args.command:
        parser.print_help()
        return
    
    dumper = MemoryDumper()
    
    if args.command == 'dump':
        dumper.dump_region_from_file(args.region, args.file, args.output)
    elif args.command == 'analyze':
        dumper.analyze_bios_structure(args.bios)
    elif args.command == 'extract':
        dumper.extract_instructions(args.file, args.offset, args.count)

if __name__ == "__main__":
    main()