# Full Implementation Plan: newcore PS1 Emulator

## Progress Summary
- Modular CPU core, interconnect, DMA, and event/timer system are scaffolded and partially integrated.
- Most major MIPS instruction handlers are implemented; some stubs remain for unneeded or untested instructions.
- DMA and event/timer region handlers are present in the interconnect.
- Handler stubs and opcode table cleanup is needed for a clean build.

## Next Steps
1. Clean up handler stubs and opcode table for a minimal, working build (only define stubs for instructions actually used by BIOS/test cases).
2. Focus on DMA and event/timer integration and test that before moving forward.
3. Integrate and test each subsystem incrementally (GPU, SPU, CDROM, etc.).
4. Add targeted tests for DMA, event/timer, and BIOS boot.

## Completed Steps
- Modular project structure and build system.
- Modular CPU core with table-driven decode/dispatch, exceptions, and delay slots.
- Table-driven interconnect with RAM, BIOS, and DMA regions.
- DMA subsystem scaffolded and region handlers integrated.
- Event/timer system scaffolded and modular event queue implemented.

## In Progress
- DMA and event/timer integration and testing.
- Opcode table and handler stub cleanup for a clean build.

## To Do
- Integrate and test GPU, SPU, CDROM, and other peripherals.
- Expand memory map and region handlers as needed.
- Add more instruction handlers as required by BIOS/games.
- Add more tests and validation for each subsystem. 