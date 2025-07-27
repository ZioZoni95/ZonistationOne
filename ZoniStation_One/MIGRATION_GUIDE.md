# ZoniStation One - Migration Guide

## Overview
This guide outlines the step-by-step migration process from PCSX-ReARMed to ZoniStation One, following the original codeflow and architecture.

## Migration Status: Phase 1 - Core Infrastructure (IN PROGRESS)

### ✅ COMPLETED STEPS:

#### Step 1: Enhanced Memory System ✅ COMPLETED
- **Source**: `psxmem.c/h`, `psxmem_map.h`, `memmap.h` → `src/memory/`
- **Status**: ✅ **COMPLETED**
- **Achievements**:
  - ✅ Memory mapping functionality migrated from PCSX-ReARMed
  - ✅ Address translation and fast memory access functions
  - ✅ Memory mapping hooks system
  - ✅ Fast memory access functions (similar to PCSX-ReARMed's `psxMu8`, `psxMu16`, etc.)
  - ✅ Memory pointer functions for direct access
  - ✅ Build errors fixed and system working

#### Step 2.1: Enhanced CPU Registers Structure ✅ COMPLETED
- **Source**: `r3000a.c/h` → `src/cpu/`
- **Status**: ✅ **COMPLETED**
- **Achievements**:
  - ✅ Comprehensive register structures migrated from PCSX-ReARMed
  - ✅ GPR (General Purpose Registers) - Complete 32-register set plus LO/HI
  - ✅ CP0 (Coprocessor 0) - System control registers for exceptions, memory management
  - ✅ GTE (Geometry Transform Engine) - Complete CP2 register set for 3D graphics
  - ✅ PAIR Union - Byte/halfword access with endianness support
  - ✅ Exception handling system - All MIPS R3000A exception types
  - ✅ Advanced CPU features - Interrupt cycle tracking, event timing, delay slot handling
  - ✅ Instruction format structures - R-Format, I-Format, J-Format
  - ✅ Enhanced initialization with proper default values from PCSX-ReARMed

#### Step 2.2: BIOS Configuration ✅ COMPLETED
- **Source**: Configuration system enhancement
- **Status**: ✅ **COMPLETED**
- **Achievements**:
  - ✅ Automatic BIOS detection from `roms/` directory
  - ✅ Multiple BIOS support (SCPH1001.BIN, SCPH5500.BIN, etc.)
  - ✅ Smart file detection with size validation (512KB-1MB)
  - ✅ Fallback mechanism for BIOS detection
  - ✅ Comprehensive logging system
  - ✅ Successfully detected user's SCPH1001.BIN (524,288 bytes)

---

### 🔄 CURRENT PHASE: BIOS Boot Preparation

**Goal**: Get to PlayStation BIOS menu for testing and validation

#### Step 2.3: CPU Instruction Set Implementation 🔄 NEXT
- **Source**: `r3000a.c` instruction implementations → `src/cpu/cpu_instructions.c`
- **Priority**: 🔴 **HIGH** (Required for BIOS boot)
- **Target**: Basic MIPS R3000A instruction set
- **Scope**:
  - Essential arithmetic instructions (ADD, SUB, ADDI, etc.)
  - Load/Store instructions (LW, SW, LB, SB, etc.)
  - Branch instructions (BEQ, BNE, J, JAL, etc.)
  - System instructions (SYSCALL, BREAK, etc.)
  - Basic coprocessor instructions (MTC0, MFC0, etc.)

#### Step 2.4: BIOS Loading System 🔄 NEXT
- **Source**: BIOS loading from PCSX-ReARMed → `src/bios/bios.c`
- **Priority**: 🔴 **HIGH** (Required for BIOS boot)
- **Target**: Load BIOS file into memory at correct address
- **Scope**:
  - BIOS file loading and validation
  - Memory mapping to `0xBFC00000` (BIOS entry point)
  - BIOS checksum verification
  - Region detection and configuration

#### Step 2.5: Basic CPU Execution Engine 🔄 NEXT
- **Source**: `r3000a.c` main execution loop → `src/cpu/cpu_interpreter.c`
- **Priority**: 🔴 **HIGH** (Required for BIOS boot)
- **Target**: Basic instruction execution loop
- **Scope**:
  - Instruction fetch and decode
  - Basic execution pipeline
  - Cycle counting
  - Exception handling framework

#### Step 2.6: Memory Mapping Integration 🔄 NEXT
- **Source**: Memory mapping from Step 1 → Integration with CPU
- **Priority**: 🟡 **MEDIUM** (Required for proper execution)
- **Target**: Proper memory access during CPU execution
- **Scope**:
  - Memory access integration with CPU
  - Address translation during execution
  - Memory region protection

---

### 🎯 BIOS BOOT MILESTONE

**Target**: Successfully boot to PlayStation BIOS menu
**Estimated completion**: After Steps 2.3-2.6
**Validation**: BIOS menu appears and responds to input

---

### 📋 REMAINING PHASES (After BIOS Boot):

#### Phase 2: Hardware Emulation
- **Step 3**: GPU (Graphics Processing Unit)
- **Step 4**: SPU (Sound Processing Unit)  
- **Step 5**: CD-ROM Controller
- **Step 6**: DMA Controller
- **Step 7**: Interrupt Controller

#### Phase 3: Advanced Features
- **Step 8**: Dynamic Recompiler
- **Step 9**: Plugin System
- **Step 10**: Save States
- **Step 11**: Netplay

#### Phase 4: Optimization & Polish
- **Step 12**: Performance Optimization
- **Step 13**: UI/UX Implementation
- **Step 14**: Testing & Bug Fixes

---

## Current Build Status
✅ **Build Status**: SUCCESSFUL
✅ **BIOS Detection**: WORKING (SCPH1001.BIN detected)
✅ **Memory System**: FUNCTIONAL
✅ **CPU Registers**: COMPREHENSIVE
✅ **Configuration**: AUTO-DETECTING

## Next Action
**Proceed with Step 2.3: CPU Instruction Set Implementation** to enable BIOS boot capability.

---

## Migration Notes
- All steps follow PCSX-ReARMed architecture and codeflow
- Each step includes comprehensive testing and validation
- Build must remain successful after each step
- BIOS boot is the primary milestone for Phase 1 completion 