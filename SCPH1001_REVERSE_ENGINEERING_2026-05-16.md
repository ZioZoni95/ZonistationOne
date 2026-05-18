# SCPH1001 BIOS Reverse Engineering - Core Components

## Scope and sources
- Disassembly (rebased): [SCPH1001_DISASM_BFC00000_2026-05-16.md](SCPH1001_DISASM_BFC00000_2026-05-16.md)
- Hardware and BIOS docs: [DOCS/kernelbios.md](DOCS/kernelbios.md), [DOCS/iomap.md](DOCS/iomap.md), [DOCS/interrupts.md](DOCS/interrupts.md), [DOCS/graphicsprocessingunitgpu.md](DOCS/graphicsprocessingunitgpu.md), [DOCS/serialinterfacessio.md](DOCS/serialinterfacessio.md), [DOCS/timers.md](DOCS/timers.md), [DOCS/cdrominternalinfoonpsxcdromcontroller.md](DOCS/cdrominternalinfoonpsxcdromcontroller.md)

## Addressing and layout
- BIOS ROM is mapped at 0xBFC00000 (uncached), with header strings near 0xBFC00100 and relocated kernel code in low RAM. See [DOCS/kernelbios.md](DOCS/kernelbios.md).
- Hardware registers are accessed via the 0x1F801000 I/O region. See [DOCS/iomap.md](DOCS/iomap.md).

## High-level boot flow (observed)
1. Reset vector writes memory control registers (BIOS ROM delay/size and RAM size) at 0x1F801010 and 0x1F801060. See [reset writes](SCPH1001_DISASM_BFC00000_2026-05-16.md#L18) and [RAM_SIZE write](SCPH1001_DISASM_BFC00000_2026-05-16.md#L22).
2. Control jumps to the main init routine at 0xBFC00150. See [jump](SCPH1001_DISASM_BFC00000_2026-05-16.md#L24).
3. Expansion bases and delays are configured under 0x1F801000. See [init base](SCPH1001_DISASM_BFC00000_2026-05-16.md#L51) and [delay setup](SCPH1001_DISASM_BFC00000_2026-05-16.md#L61).
4. Registers are zeroed and a RAM clear loop runs over low memory. See [clear loop start](SCPH1001_DISASM_BFC00000_2026-05-16.md#L114) and [loop branch](SCPH1001_DISASM_BFC00000_2026-05-16.md#L122).
5. Subsystem init includes SPU register clears and IRQ controller reset (I_STAT/I_MASK). See [SPU base](SCPH1001_DISASM_BFC00000_2026-05-16.md#L6594) and [I_STAT/I_MASK base](SCPH1001_DISASM_BFC00000_2026-05-16.md#L6624).
6. GPU init reads GPUSTAT and writes GP0 draw-mode values. See [GPU init routine](SCPH1001_DISASM_BFC00000_2026-05-16.md#L64556).

## Component notes (core)
### Memory control and RAM init
- Reset writes to 0x1F801010 (BIOS ROM delay/size) and 0x1F801060 (RAM_SIZE) match the I/O map. See [DOCS/iomap.md](DOCS/iomap.md), [reset writes](SCPH1001_DISASM_BFC00000_2026-05-16.md#L18), and [RAM_SIZE write](SCPH1001_DISASM_BFC00000_2026-05-16.md#L22).
- The main init block configures expansion bases and delay registers (EXP1/EXP2, SPU, CDROM, EXP2 delay). See [init base](SCPH1001_DISASM_BFC00000_2026-05-16.md#L51) and [delay setup](SCPH1001_DISASM_BFC00000_2026-05-16.md#L61).
- A RAM clear loop zeros a low memory region before continuing. See [clear loop start](SCPH1001_DISASM_BFC00000_2026-05-16.md#L114).

### IRQ and timers
- The init sequence clears I_STAT/I_MASK at 0x1F801070/74, consistent with IRQ controller setup. See [I_STAT/I_MASK base](SCPH1001_DISASM_BFC00000_2026-05-16.md#L6624) and [DOCS/interrupts.md](DOCS/interrupts.md).
- BIOS data tables reference 0x1F801070 and 0x1F801100 (timer base), indicating timer/IRQ service use. See [IRQ/timer table](SCPH1001_DISASM_BFC00000_2026-05-16.md#L20989) and [DOCS/timers.md](DOCS/timers.md).

### GPU
- The routine at 0xBFC424AC reads GPUSTAT (0x1F801814) and writes GP0 with a masked 0xE1001000 value, consistent with draw-mode setup. See [GPU init routine](SCPH1001_DISASM_BFC00000_2026-05-16.md#L64556) and [DOCS/graphicsprocessingunitgpu.md](DOCS/graphicsprocessingunitgpu.md).
- BIOS data tables include GPU port addresses (0x1F801810/0x1F801814), indicating GPU service tables. See [GPU table](SCPH1001_DISASM_BFC00000_2026-05-16.md#L14059).

### DMA
- BIOS data tables include DMA channel 2 and DMA control registers (0x1F8010A0..A8, 0x1F8010F0/F4). See [DMA table](SCPH1001_DISASM_BFC00000_2026-05-16.md#L14153) and [DOCS/iomap.md](DOCS/iomap.md).

### CDROM
- BIOS data tables include CDROM registers 0x1F801800..0x1F801803, indicating CDROM service routines and helpers. See [CDROM table](SCPH1001_DISASM_BFC00000_2026-05-16.md#L14200) and [DOCS/iomap.md](DOCS/iomap.md).

### SIO/Pad
- BIOS data tables include SIO0 base 0x1F801040 and IRQ base 0x1F801070, aligned with controller driver strings nearby. See [SIO table](SCPH1001_DISASM_BFC00000_2026-05-16.md#L21269) and [DOCS/serialinterfacessio.md](DOCS/serialinterfacessio.md).

### BIOS syscall/utility services
- A0/B0/C0 vectors and jump tables are defined in low RAM as documented, and are used by the BIOS service layer. See [DOCS/kernelbios.md](DOCS/kernelbios.md).

## Data vs code note
- Large portions of the disassembly are data tables or strings. Objdump renders these as instructions (for example, lines that look like branches to 0x1F801810). Treat those as data tables, not executable code. See [GPU table](SCPH1001_DISASM_BFC00000_2026-05-16.md#L14059).

## Diagram
- Draw.io flow diagram: [SCPH1001_CORE_FLOW_2026-05-16.drawio](SCPH1001_CORE_FLOW_2026-05-16.drawio)
