# Emulator Implementation Roadmap

## 1. Core Loop & CPU Execution
- [x] Implement the main emulation loop (fetch-decode-execute).
- [x] Integrate CPU instruction execution, including:
  - [x] Instruction fetch from memory (via interconnect)
  - [x] Decoding and executing all MIPS instructions (most major opcodes implemented, some stubs remain)
  - [x] Handling exceptions, interrupts, and delay slots
- [x] Integrate debugger hooks and breakpoints (scaffolded).

## 2. Memory Map & Interconnect
- [x] Expand the interconnect to fully map all PS1 address regions (RAM, BIOS, DMA; VRAM/hardware in progress).
- [x] Route memory accesses to RAM, VRAM, BIOS, hardware registers, etc.
- [ ] Implement hardware register read/write logic for all mapped devices (DMA stubs present).

## 3. BIOS & Boot Process
- [x] Ensure BIOS is loaded and mapped correctly.
- [x] Implement the boot sequence: reset vector, BIOS calls, and initial hardware state (in progress).

## 4. DMA Controller
- [x] Implement DMA channel logic and region handlers (scaffolded, stubs for now).
- [ ] Integrate DMA with GPU, CDROM, SPU, and other peripherals (next step).
- [ ] Handle DMA interrupts and event scheduling (event system scaffolded).

## 5. Event/Timer System
- [x] Implement modular event queue and dispatcher (scaffolded).
- [ ] Integrate with timers, VBlank, and DMA events (next step).

## 6. GPU & Renderer
- [ ] Implement GPU command processing (GP0/GP1).
- [ ] Route drawing commands to the renderer plugin.
- [ ] Implement VRAM access, display timing, and VBlank events.
- [ ] Expand the renderer plugin (software, OpenGL, etc.).

## 7. Subsystem Integration & Testing
- [ ] Test each subsystem as it is implemented, using logs and test cases for validation.
- [ ] Add targeted tests for DMA, event/timer, and BIOS boot.

## Current Status
- Modular CPU, interconnect, DMA, and event/timer system scaffolded and partially integrated.
- Handler stubs and opcode table cleanup needed for a clean build.
- Next: focus on DMA/event integration and minimal handler stubs for BIOS boot. 