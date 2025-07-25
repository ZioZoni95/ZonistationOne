# Technical Overview: Modular PS1 Emulator Core

## Architecture Summary

This project is a modular reimplementation of a PlayStation 1 emulator core, inspired by the structure and code flow of [PCSX-ReARMed](https://github.com/notaz/pcsx_rearmed). Each major hardware and emulation subsystem is separated into its own C module and header, with clear responsibilities and interfaces.

### Subsystem Modules
- **CPU Core** (`cpu_core.c/h`): Interpreter loop, instruction decode/execute, opcode dispatch tables
- **CPU Control** (`cpu_control.c/h`): Exception handling, BIOS boot, CPU state management
- **Disassembler** (`disasm_mips.c/h`): MIPS instruction disassembly utilities
- **Memory Map** (`mem_map.c/h`): RAM, BIOS, and memory mapping logic
- **DMA Engine** (`dma_engine.c/h`): DMA controller, channel management, DMA interrupts
- **Timer Unit** (`timer_unit.c/h`): Timers/counters, timer interrupts
- **Event Loop** (`event_loop.c/h`): Event/timer scheduling and dispatch
- **CDROM Drive** (`cdrom_drive.c/h`): CDROM command processing, data transfer
- **SIO Port** (`sio_port.c/h`): Serial I/O, controller/memory card communication
- **Sound SPU** (`sound_spu.c/h`): Sound processing unit, audio, SPU DMA
- **GPU Core** (`gpu_core.c/h`): Graphics processing, command processing, rendering
- **GTE Unit** (`gte_unit.c/h`): Geometry transformation engine, GTE instructions
- **MDEC Unit** (`mdec_unit.c/h`): Motion decompression engine, MDEC instructions
- **Misc/Debug/Plugins**: Utility, debug, and plugin management modules (scaffolded)

Each module contains:
- A state struct for subsystem data
- Initialization and main operation functions
- Clear comments and TODOs referencing the reference implementation

---

## Implementation Plan: Step-by-Step

### 1. **CPU Core**
- Implement interpreter loop, opcode dispatch, and instruction handlers
- Integrate exception handling and BIOS boot via CPU Control
- Reference: `psxinterpreter.c`, `r3000a.c`, `psxcommon.c` in pcsx_rearmed_reference

### 2. **Memory Subsystem**
- Implement RAM/BIOS allocation, memory mapping, and read/write logic
- Reference: `psxmem.c`, `psxmem_map.h`, `memmap.h`

### 3. **DMA Engine**
- Implement DMA channel state, transfer logic, and interrupt signaling
- Reference: `psxdma.c`

### 4. **Timers/Counters**
- Implement timer/counter state, stepping, and interrupt logic
- Reference: `psxcounters.c`, `psxevents.c`

### 5. **Event Loop**
- Implement event queue, scheduling, and dispatching for timers, VBlank, etc.
- Reference: `psxevents.c`, event scheduling in main loop

### 6. **CDROM, SIO, SPU, GPU, GTE, MDEC**
- Implement each subsystem's state, command/data processing, and integration
- Reference: `cdrom.c`, `sio.c`, `spu.c`, `gpu.c`, `gte.c`, `mdec.c`

### 7. **Integration**
- Wire up all subsystems in the main emulator loop
- Ensure correct event, interrupt, and memory interactions
- Reference: `frontend/main.c`, `EmuInit`, `psxCpu->Execute`, plugin flow

### 8. **Testing and Debugging**
- Incrementally test each subsystem after implementation
- Use the disassembler and debug modules for tracing and validation

### 9. **Frontend, Plugins, and Config**
- Add frontend, plugin management, and configuration as needed
- Reference: `frontend/`, `plugins.c`, `config.h`

---

## Code Flow Reference (PCSX-ReARMed)
- **Startup:** Parse config/args → Initialize subsystems → Load BIOS/game → Main loop
- **Main Loop:** Step CPU → Step DMA/timers/events → Render frame → Handle input/audio → Check for exit/reset
- **Shutdown:** Close plugins/subsystems → Save state if needed

---

## Comments & Guidance
- Each module is designed for clarity, maintainability, and ease of porting from the reference.
- Follow the reference code for logic, but use modern names and clear comments.
- Fill in TODOs and stubs incrementally, testing as you go.
- This document should be updated as the implementation progresses. 