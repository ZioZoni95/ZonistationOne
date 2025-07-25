# PS1 Emulator Implementation Plan

## Milestones
- **[Initial]** Project setup, reference to oldcode and PCSX ReARMed.
- **[CPU Core]** MIPS R3000A CPU core implemented, instruction decode and execution.
- **[Memory Map]** RAM, BIOS, hardware registers, expansion regions mapped.
- **[Hardware Stubs]** GPU, CDROM, SIO, timers, DMA, IRQ, SPU, scratchpad stubs added.
- **[BIOS Boot]** BIOS loads and executes from reset vector.
- **[Expansion Probe Handling]** Expansion region probe returns 0xFFFFFFFF, matching hardware/PCSX ReARMed.
- **[Log Flood Suppression]** Log flooding from unmapped/expansion regions suppressed.
- **[Exception Loop]** Emulator currently stuck in exception loop after BIOS probe (likely exception/RFE logic).

## Current Progress
- BIOS loads and executes.
- Hardware stubs and memory map are correct and match PCSX ReARMed.
- Expansion region probe handling is done.
- Log flooding from unmapped regions is suppressed.
- Emulator is currently stuck in an exception loop after the BIOS probes the expansion region.

## Immediate Next Steps
1. **Audit and fix exception and RFE (Return From Exception) logic in the CPU core**
   - Ensure EPC and SR are set/restored exactly as in PCSX ReARMed.
   - Add detailed logging for EPC, SR, and PC transitions during exception and RFE handling.
   - Confirm that after an exception, the BIOS can return and continue execution.
2. **Verify BIOS moves past hardware probe**
   - BIOS should only probe expansion region a few times, then continue boot sequence.
3. **Continue toward BIOS logo/menu**
   - Ensure VBlank, IRQ0, and GPU display are working.
   - Test with no disc and with a valid disc image. 

## Missing / Incomplete Components
- **MIPS Instruction Set:** Some instructions (COP1, COP3, LWL, LWR, SWL, SWR, LWCx, SWCx, GTE) are not fully implemented or are stubs. GTE coprocessor integration is incomplete.
- **Exception Handling:** Some edge cases (e.g., PC alignment, RFE logic) may not match hardware/PCSX ReARMed.
- **DMA Controller:** Per-channel register emulation, transfer logic, interrupts, and polling are incomplete. Only minimal DMA stepping is present.
- **Timer System:** Timer register writes, event scheduling, and interrupt generation are stubbed or incomplete.
- **GPU:** Command processing, VRAM access, and renderer integration are minimal or stubbed. No display output yet.
- **CDROM/SIO:** Command/state machines, status polling, and register logic are stubs. No real disc or controller emulation.
- **SPU (Audio):** Status/control and audio output are stubbed.
- **Debugger:** No integrated debugger (breakpoints, watchpoints, etc.).
- **Test Suite:** No automated test/validation suite for instruction or hardware coverage.
- **Peripheral Integration:** No memory card, multitap, or other peripheral support.
- **Performance/Optimization:** No benchmarking or optimization work yet. 