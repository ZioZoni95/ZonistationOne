# Emulator Implementation Roadmap

## Milestones & Subsystem Progress

### 1. Core Loop & CPU Execution
- [x] Main emulation loop (fetch-decode-execute)
- [x] Table-driven instruction decode/dispatch
- [x] Exception and interrupt handling
- [x] BIOS instruction stream execution
- [ ] Complete all MIPS instructions (COP1, COP3, LWL, LWR, SWL, SWR, LWCx, SWCx, GTE integration)
- [ ] GTE coprocessor integration and instruction support

### 2. Memory Map & Interconnect
- [x] RAM, BIOS, and hardware register mapping
- [x] Memory aliases (KSEG0/KSEG1, cached/uncached)
- [x] Scratchpad (1KB data cache RAM)
- [x] Comprehensive hardware register regions (IRQ, DMA, timers, SIO, CDROM, SPU, GPU, MemCtrl)
- [ ] Expansion region edge cases and hardware probe handling

### 3. Hardware Register Emulation
- [x] IRQ: I_STAT/I_MASK register logic
- [x] DMA: Control/interrupt register stubs
- [x] Timers: Counter reads, basic stubs
- [x] SIO, CDROM, SPU, GPU, MemCtrl: Register stubs
- [ ] DMA: Per-channel register emulation, transfer logic, interrupts, polling
- [ ] Timers: Register writes, event scheduling, interrupts
- [ ] GPU: Command processing, VRAM access, renderer integration
- [ ] CDROM/SIO: Command/state machine, status polling
- [ ] SPU: Status/control, audio stub

### 4. Event System
- [x] Event queue and scheduling
- [x] VBlank and DMA event handlers (stub)
- [ ] Timer events, accurate cycle timing
- [ ] Integration of hardware events with interrupt controller

### 5. Debugging & Validation
- [x] Logging for memory, hardware, and opcode execution
- [ ] BIOS boot sequence validation
- [ ] Test ROMs for instruction and hardware coverage
- [ ] Logging improvements and error reporting
- [ ] Debugger integration (breakpoints, watchpoints)

### 6. Long-Term & Stretch Goals
- [ ] Software renderer and/or OpenGL backend
- [ ] Audio (SPU) and input (SIO/controller) emulation
- [ ] Save states, debugging UI, performance optimization
- [ ] Peripheral integration (memory card, multitap, etc.)

## Current Bottlenecks & Known Issues
- [ ] BIOS stuck in hardware probe loop (expansion region)
- [ ] DMA and timer event system incomplete
- [ ] Hardware register stubs need expansion for full BIOS compatibility
- [ ] No real test/validation suite yet

## Next Steps (Actionable)
- [ ] Expand DMA register emulation (per-channel registers, status, polling)
- [ ] Implement real DMA channel stepping and event scheduling
- [ ] Implement timer register writes, event scheduling, and interrupts
- [ ] Continue stubbing/implementing other hardware (CDROM, SIO, GPU, SPU, etc.)
- [ ] Use BIOS execution logs to guide further development
- [ ] Add test ROMs and BIOS boot validation 