# Emulator Implementation Roadmap

## Current Progress
- BIOS loads and executes.
- CPU instruction set is mostly complete for BIOS boot (no unimplemented instruction errors in logs).
- Memory map and hardware register stubs are in place.
- DMA controller and event system are partially implemented (minimal DMA stepping, event queue, VBlank/DMA events scheduled).
- BIOS is currently stuck in a hardware probe loop, executing NOPs in the expansion/hardware region.

## Next Steps
- Expand DMA register emulation (per-channel registers, status, polling).
- Improve timer/event system (timer register writes, event scheduling, interrupts).
- Continue stubbing/implementing other hardware (CDROM, SIO, GPU, SPU, etc.).
- Use BIOS execution logs to guide further development. 