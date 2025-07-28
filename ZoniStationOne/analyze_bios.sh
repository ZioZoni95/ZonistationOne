#!/bin/bash

# PlayStation BIOS Analysis Script
# Uses standard Linux tools to analyze BIOS files

BIOS_FILE="$1"
OUTPUT_FILE="${2:-bios_analysis.txt}"

if [ -z "$BIOS_FILE" ]; then
    echo "Usage: $0 <bios_file.bin> [output_file.txt]"
    echo ""
    echo "This script analyzes PlayStation BIOS files using:"
    echo "- xxd for hex dump"
    echo "- grep for pattern matching"
    echo "- wc for statistics"
    echo "- Basic MIPS instruction analysis"
    exit 1
fi

if [ ! -f "$BIOS_FILE" ]; then
    echo "ERROR: BIOS file not found: $BIOS_FILE"
    exit 1
fi

echo "PlayStation BIOS Analysis Script"
echo "==============================="
echo "File: $BIOS_FILE"
echo "Output: $OUTPUT_FILE"
echo ""

# Get file info
FILE_SIZE=$(stat -c%s "$BIOS_FILE")
echo "File size: $FILE_SIZE bytes ($(($FILE_SIZE / 1024)) KB)"
echo "Expected instructions: $(($FILE_SIZE / 4))"
echo ""

# Create output file
{
    echo "PlayStation BIOS Analysis Report"
    echo "==============================="
    echo "File: $BIOS_FILE"
    echo "Size: $FILE_SIZE bytes"
    echo "Instructions: $(($FILE_SIZE / 4))"
    echo "Analysis date: $(date)"
    echo ""
    
    echo "1. FILE HEADER ANALYSIS"
    echo "======================="
    echo "First 256 bytes (64 instructions):"
    xxd -g 4 -e "$BIOS_FILE" | head -16
    echo ""
    
    echo "2. STRING ANALYSIS"
    echo "=================="
    echo "ASCII strings found in BIOS:"
    strings "$BIOS_FILE" | head -20
    echo ""
    
    echo "3. PATTERN ANALYSIS"
    echo "==================="
    echo "SYSCALL instructions (0x0000000C):"
    xxd -g 4 -e "$BIOS_FILE" | grep -n "0c000000" | head -10
    echo ""
    
    echo "BREAK instructions (0x0000000D):"
    xxd -g 4 -e "$BIOS_FILE" | grep -n "0d000000" | head -10
    echo ""
    
    echo "4. INSTRUCTION FREQUENCY"
    echo "========================"
    echo "Most common instruction opcodes (first 1000 instructions):"
    xxd -g 4 -e "$BIOS_FILE" | head -250 | awk '{print substr($2,1,2)}' | sort | uniq -c | sort -nr | head -10
    echo ""
    
    echo "5. EXCEPTION VECTORS"
    echo "===================="
    echo "Checking common exception vector addresses:"
    
    # Calculate offsets for common vectors
    VECTORS=(
        "0x80000000:0x00000000"
        "0x80000040:0x00000040" 
        "0x80000048:0x00000048"
        "0x80000080:0x00000080"
        "0x80000100:0x00000100"
        "0x80000180:0x00000180"
    )
    
    for vector in "${VECTORS[@]}"; do
        addr=$(echo $vector | cut -d: -f1)
        offset=$(echo $vector | cut -d: -f2)
        offset_dec=$((16#$offset))
        
        if [ $offset_dec -lt $FILE_SIZE ]; then
            instruction=$(xxd -g 4 -e -s $offset_dec -l 4 "$BIOS_FILE" | awk '{print $2}')
            echo "  $addr (offset 0x$offset): $instruction"
        fi
    done
    echo ""
    
    echo "6. BIOS SIGNATURE ANALYSIS"
    echo "========================="
    echo "Checking for PlayStation BIOS signatures:"
    
    # Look for common BIOS strings
    if strings "$BIOS_FILE" | grep -q "Sony"; then
        echo "  ✓ Found 'Sony' signature"
    fi
    
    if strings "$BIOS_FILE" | grep -q "Entertainment"; then
        echo "  ✓ Found 'Entertainment' signature"
    fi
    
    if strings "$BIOS_FILE" | grep -q "CEX"; then
        echo "  ✓ Found 'CEX' signature"
    fi
    
    if strings "$BIOS_FILE" | grep -q "K.S."; then
        echo "  ✓ Found 'K.S.' signature"
    fi
    
    echo ""
    echo "7. STATISTICS"
    echo "============="
    echo "Total SYSCALL instructions: $(xxd -g 4 -e "$BIOS_FILE" | grep -c "0c000000" || echo "0")"
    echo "Total BREAK instructions: $(xxd -g 4 -e "$BIOS_FILE" | grep -c "0d000000" || echo "0")"
    echo "Total JAL instructions: $(xxd -g 4 -e "$BIOS_FILE" | grep -c "0c000000" || echo "0")"
    echo "Total LW instructions: $(xxd -g 4 -e "$BIOS_FILE" | grep -c "8c" || echo "0")"
    echo "Total SW instructions: $(xxd -g 4 -e "$BIOS_FILE" | grep -c "ac" || echo "0")"
    echo ""
    
    echo "8. RAW HEX DUMP (FIRST 1KB)"
    echo "============================"
    xxd -g 4 -e "$BIOS_FILE" | head -64
    
} > "$OUTPUT_FILE"

echo "Analysis complete! Results saved to: $OUTPUT_FILE"
echo ""
echo "Quick summary:"
echo "- File size: $FILE_SIZE bytes"
echo "- Instructions: $(($FILE_SIZE / 4))"
echo "- SYSCALL count: $(xxd -g 4 -e "$BIOS_FILE" | grep -c "0c000000" || echo "0")"
echo "- BREAK count: $(xxd -g 4 -e "$BIOS_FILE" | grep -c "0d000000" || echo "0")" 