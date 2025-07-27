# PSX Emulator (newcore)

## Overview
This project is a modular, frontend-driven PlayStation emulator core, inspired by the codeflow and structure of PCSX-ReARMed, but with a modern, maintainable, and extensible architecture. All major subsystems are implemented as stubs, ready for incremental development.

## Codeflow (Reference-Aligned)
- **main.c** orchestrates the frontend, initialization, and main loop:
  1. Calls `emu_core_preinit()` for config/log setup
  2. Initializes input, platform (stub), and menu
  3. Calls `emu_core_init()` for core setup
  4. Loads plugins (stub), CD/file (stub), and savestate (stub)
  5. If ready, prepares emulation and enters the main emulation loop (`emulator_frame()` in a loop)
  6. If not ready, enters the menu loop (stub)
  7. On exit, performs shutdown/cleanup for all subsystems

- **emulator.c** provides modular core entry points:
  - `emu_core_preinit`: Pre-initialize config/logging
  - `emu_core_init`: Initialize core state
  - `emu_core_ask_exit`: Set quit flag
  - `emulator_frame`: Run a single emulation frame (stub)
  - `emu_core_should_quit`: Check quit flag

## Main Components
- **Config**: Loads emulator configuration (stub, to be implemented)
- **Input**: Handles input initialization and polling (stub)
- **Menu**: Manages menu system and navigation (stub)
- **CD Image**: Handles CD image loading/closing (stub)
- **Savestate**: Handles savestate load/save (stub)
- **Renderer**: Handles video output (stub)
- **Core**: Emulation logic, memory, BIOS, etc. (stub)

## Build & Run
```
cd newcore
make clean && make
./psxemu
```

## Recommended Implementation Order
1. Configuration & Logging
2. Input Subsystem
3. Menu System
4. Core Initialization
5. CD/File Loading
6. Savestate Handling
7. Main Emulation Frame
8. Shutdown/Cleanup

## Contribution & Expansion
- Each subsystem is modular and can be developed/tested independently.
- Follow @pcs style for comments and modularity.
- Expand stubs incrementally, following the reference codeflow.

---

For more details, see the code comments and stubs in each source/header file. 