# DMA / Interrupts / GTE (+ pipeline timings) / MDEC / Hardware numbers — audit, 2026-08-17

Source of truth: `psx-spx-docs/docs/*.md` (fresh clone of psx-spx/psx-spx.github.io).
Line numbers are that clone's, **not** the older `DOCS/` fork's.

Primary files, read end to end: `dmachannels.md` (237), `interrupts.md` (67),
`geometrytransformationenginegte.md` (664), `gtepipelinetimings.md` (280),
`macroblockdecodermdec.md` (425), `hardwarenumbers.md` (440). Supporting files
read for the rules these subsystems depend on: `unpredictablethings.md` (209),
`partialwordwrites.md` (312), `memorymap.md` (204), `iomap.md` register map, and
`cpuspecifications.md` §COP0 / §Coprocessor Opcodes (the interrupt and COP2
rules live there, not in `interrupts.md`).

Code audited line by line: `src/core/dma.c`, `include/dma.h`, the DMA engine in
`src/core/bus.c:740-1178`, `src/core/bus_irq.c`, the IRQ/DMA/MDEC register
handlers in `src/core/bus.c:224-393`, `src/core/event_scheduler.c` (DMA/MDEC
slots), `src/gte/gte.c`, `src/gte/gte_internal.h`, `src/gte/gte_ops.c`,
`include/gte.h`, the COP2 path in `src/cpu/cpu_instructions.c:739-797`,
`:932-975` and `src/cpu/cpu_execution.c:28-49`, `src/core/mdec.c`,
`include/mdec.h`.

Rule of this audit, same as `CDROM_AUDIT_2026-08-17.md`: no entry may say
"correct" without citing both a documentation line and a code line. Anything not
actually compared is marked `UNVERIFIED`, not "ok".

Status legend: `OK` verified both sides · `DIVERGES` measured difference ·
`MISSING` documented behaviour absent · `PLAUSIBLE` shape agrees, exactness not
established · `UNVERIFIED` not decidable from the documentation.

---

## Part 1 — rules extracted from the documentation

### 1.1 DMA (`dmachannels.md`)

| # | Doc | Rule |
|---|---|---|
| D1 | :18-34 | MADR: bits 0-23 address. SyncMode 0 does **not** update MADR (unless chopping); SyncMode 1/2 **do** — at end it holds the end address (1) or the end marker (2). Bits 0-1 writeable but forced to zero when used |
| D2 | :36-57 | BCR: sync0 = BC words (0 = 10000h); sync1 = BS blocksize + BA block count (total BS*BA); sync2 unused. SyncMode 1 decrements BA to zero; sync0+chopping decrements BC |
| D3 | :59-86 | CHCR bit layout: 0 direction, 1 step, 8 chopping, 9-10 SyncMode, 16-18 chop DMA window, 20-22 chop CPU window, 24 start/busy, 28 force-start, 29 pause/hold, 30 bus snooping |
| D4 | :87-91 | Bit 28 is cleared upon **begin**; bit 24 is cleared upon **completion** |
| D5 | :92-94 | DMA6/OTC: only bits 24, 28, 30 are writeable; bit 1 always reads 1; all other bits read 0 |
| D6 | :96-117 | DPCR: 3-bit priority + enable per channel, CPU priority in 28-30. Reset value 07654321h. Equal priorities are broken by channel number |
| D7 | :119-134 | DICR: bits 0-6 per-channel "IRQ on every slice/LL block", 15 bus-error (forces bit31), 16-22 per-channel masks, 23 master enable, 24-30 flags, 31 master flag. Flags in 24+n are set **only** if both 16+n and 23 are enabled |
| D8 | :135-146 | Bit31 recompute rule: `b31 = b15 OR (b23 AND b(24-30)>0)`. **The per-channel enables (b16-22) do not factor into b31.** Flags persist until acknowledged by writing 1. On the 0→1 transition of b31, I_STAT.3 is set |
| D9 | :148-157 | 1F8010F8h/1F8010FCh hold strange read-only values |
| D10 | :159-171 | Conventional CHCR values per channel (OTC 11000002h, CDROM 11000000h, …) |
| D11 | :173-192 | Linked list node: bits 0-23 next address, 24-31 extra word count. End marker FFFFFFh; any address above 8 MB is invalid and **triggers a DMA error reflected into DICR** |
| D12 | :194-213 | Transfer rates: ch0/1/2/6 = 1 clk/word, ch3 = 24 (BIOS) or 40 (games), ch4 = 4, ch5 = 20 |
| D13 | :215-220 | DRAM hyper-page: ~17 clks per 16 words on top of the per-word rate |
| D14 | :222-232 | CPU keeps running during DMA only for cache/scratchpad/COP0/GTE and up to 4 queued writes; any RAM/IO read stalls it until DMA finishes. It resumes between SyncMode 1 blocks and SyncMode 2 list entries |
| D15 | `unpredictablethings.md:151-153`, `memorymap.md:126` | The seven CHCR registers at 1F8010x8h are **mirrored** at 1F8010xCh, readable and writeable |
| D16 | `unpredictablethings.md:23-27` | Write datasize: DMAx.ADDR and DMAx.CTRL are (w32) for 8/16-bit writes, DMAx.LEN is OK at any width, DPCR/DICR are (w32), 1F8010F8h-FFh ignore writes |

### 1.2 Interrupts (`interrupts.md` + `cpuspecifications.md`)

| # | Doc | Rule |
|---|---|---|
| N1 | :7-21 | I_STAT/I_MASK bits 0-10 (VBLANK, GPU, CDROM, DMA, TMR0-2, PAD, SIO, SPU, IRQ10); bits 11-15 always zero; bits 16-31 garbage |
| N2 | :4-6 | I_STAT: read = status, write = acknowledge, where **writing 0 clears and writing 1 keeps**. I_MASK is plain R/W |
| N3 | :26-31 | I_STAT bits are **edge-triggered** — set only on a false→true transition of the source. `(I_STAT AND I_MASK) != 0` sets cop0r13.bit10 |
| N4 | :33-47 | Acknowledge order: I_STAT first, then the device's own register. The reverse order can lose interrupts permanently |
| N5 | :55-62 | cop0r13.bit10 is **not a latch**: it clears by itself as soon as `(I_STAT AND I_MASK) = 0` |
| N6 | `cpuspecifications.md:624-634` | SR: bit0 IEc, bits 8-15 Im, bit 30 CU2 (GTE enable) |
| N7 | `cpuspecifications.md:687-710` | If an interrupt occurs **on** a cop2 command: the GTE command **is executed**, and EPC still points at it. The handler must detect `[EPC] AND FE000000h = 4A000000h` and skip it. Crash Bandicoot, Spyro, Jinx depend on this |
| N8 | `cpuspecifications.md:752-770` | Exception priority order (Int sits below Ovf and above the decode-time exceptions) |
| N9 | `partialwordwrites.md:80-128`, `unpredictablethings.md:22`, `:71-82` | On-die MMIO (IRQ control, DMA control, GPU, MDEC): byte enables are **ignored**; a partial store latches the shifted source word **in full** (`sb+N` → `src << N*8`, `sh+2` → `src << 16`), previous contents do not contribute. Misaligned `sh` traps AdES before the bus cycle |

### 1.3 GTE (`geometrytransformationenginegte.md`)

| # | Doc | Rule |
|---|---|---|
| G1 | :45-63 | COP2 imm25 encoding: bit19 sf, bits 17-18 MVMVA matrix, 15-16 vector, 13-14 translation, bit10 lm, bits 0-5 real opcode; bits 20-24 fake opcode ignored |
| G2 | :65-98 | Data (cop2r0-31) and control (cop2r32-63) register map |
| G3 | :105-119 | Matrices are packed two 16-bit elements per register; RT33/L33/LB3 read back **sign-extended** |
| G4 | :146-162 | OFX/OFY 1:15:16, H unsigned 16-bit — **BUG: reading cop2r58 sign-extends it**, while RTPS/RTPT calculations use it unsigned. DQA 1:7:8, DQB 32-bit |
| G5 | :164-170 | ZSF3/ZSF4 signed 16-bit inputs; OTZ unsigned 16-bit result |
| G6 | :172-189 | SZ FIFO 4 stages, SXY FIFO 3 stages; SXYP mirrors SXY2 and **moves the FIFO on write**, whereas writing SXY0/1/2 or SZn moves nothing |
| G7 | :191-200 | VZn and IRn occupy a whole register and read back sign-extended |
| G8 | :202-212 | RGBC and the RGB0-2 FIFO; RES1 (cop2r23) is read/writeable and mirrors nothing |
| G9 | :232-257 | IRGB write expands 5:5:5 to IR1-3 (×80h); ORGB is a read-only mirror collapsing IR1-3 (÷80h, saturated 00h..1Fh, no flags) |
| G10 | :259-262 | LZCR = leading-zero count of LZCS if positive, leading-one count if negative; **result range 1..32** |
| G11 | :269-310 | FLAG bit meanings; bit31 = bits 30..23 and 18..13 ORed; bits 30-12 are software-writeable but **all bits are automatically reset at the begin of a new GTE command**; software writes to 16-bit registers set no flags |
| G12 | :315-352 | Opcode table with cycle counts (RTPS 15, RTPT 23, NCLIP 8, MVMVA 8, NCDS 19, NCDT 44, NCCS 17, NCCT 39, CC 11, CDP 13, NCS 14, NCT 30, SQR 5, DCPL 8, DPCS 8, DPCT 17, AVSZ3 5, AVSZ4 6, GPF 5, GPL 5, OP 6) |
| G13 | :409-439 | RTPS/RTPT formulas, SZ3 = MAC3 SAR ((1-sf)*12), the three MAC0 steps, and the rule that **FLAG.22 is always evaluated as if lm=0 while the stored IR3 respects lm** |
| G14 | :441-449 | NCLIP formula |
| G15 | :451-464 | AVSZ3/AVSZ4 formulas; OTZ saturated 0..FFFFh |
| G16 | :469-492 | MVMVA; **FC translation is bugged** (first product dropped); **mx=3 selects a garbage matrix** `-R*10h, +R*10h, IR0, RT13, RT13, RT13, RT22, RT22, RT22` |
| G17 | :494-512 | SQR (lm has no effect) and OP (D1/D2/D3 = RT11/RT22/RT33) |
| G18 | :520-548 | NCS/NCT/NCCS/NCCT/NCDS/NCDT and CC/CDP pipelines |
| G19 | :550-567 | DCPL/DPCS/DPCT/INTPL; **DPCT reads R,G,B from RGB0** (bottom of the FIFO) each of its three passes, CODE still from RGBC |
| G20 | :569-578 | GPF/GPL |
| G21 | :580-587 | "MAC+(FC-MAC)*IR0" detail: the intermediate (FC-MAC) is saturated as if lm=0 |
| G22 | :594-607 | Color FIFO receives MACn/16 saturated 00h..FFh, older entries shift down; CODE is the GP0 command byte |
| G23 | :611-664 | UNR division algorithm, `unr_table[257]`, overflow → 1FFFFh + FLAG.17/31, and the min(1FFFFh) clamp for cases like FE3Fh/7F20h |
| G24 | :29-43 | MFC2/CFC2 have a **1-instruction load delay** (Tekken 2); an instruction that reads a GTE register or issues a GTE command **while one is in flight stalls the CPU** |
| G25 | `cpuspecifications.md:438-458` | Coprocessor load delay is one opcode; **writes to cop2 registers take 2-3 clock cycles** (3 for IRGB) |

### 1.4 GTE pipeline timings (`gtepipelinetimings.md`)

| # | Doc | Rule |
|---|---|---|
| P1 | :10-28 | Each (instruction, input register) pair has a latch boundary `N`: the smallest number of nops after the cop2 op at which an `MTC2`/`CTC2` to that register no longer changes the result. Hardware-verified on one SCPH-5501 |
| P2 | :33-39 | `MTC2`/`CTC2` do **not** stall while a GTE op is in flight |
| P3 | :78-198 | The measured tables (RTPS/RTPT, NCS/NCCS/NCDS, NCT/NCCT/NCDT, CC/CDP, DPCS/DPCT/DCPL/INTPL, SQR/OP/NCLIP, AVSZ3/4/GPF/GPL, MVMVA) |
| P4 | :203-236 | Patterns: inputs are snapshotted in the first ~4 cycles; triple-vertex variants push V2 out 2-4 nops; **RGBC for NCCT/NCDT latches at cycle 12-15**; DQA/DQB at cycle 3-4 of RTPS |
| P5 | :239-280 | Caveats: ±1-2 nop icache noise, single console, saturation collapse, `LWC2` excluded |

### 1.5 MDEC (`macroblockdecodermdec.md`)

| # | Doc | Rule |
|---|---|---|
| M1 | :13-29 | 1F801820h write = command/parameters, read = data. Data always leaves as 8x8 blocks; for colour, DMA1 does the 16x16 re-ordering |
| M2 | :31-50 | Status bits 31 out-empty, 30 in-full, 29 busy, 28 in-request, 27 out-request, 26-25 depth, 24 signed, 23 bit15, 18-16 current block (0..3 = Y1..Y4, 4 = Cr, 5 = Cb; mono always 4), 15-0 **parameter words remaining minus 1, FFFFh = none** |
| M3 | :52-63 | Control: bit31 reset (status becomes **80040000h**), bit30 enable DMA0, bit29 enable DMA1. The out-request flag clears after the first few words of a block are read |
| M4 | :65-75 | DMA0/DMA1 usage with blocksize 20h; parameters padded with FE00h |
| M5 | :80-90 | MDEC(1): depth/signed/bit15 in bits 28-25, word count in bits 15-0 |
| M6 | :92-100 | MDEC(2): 64 luma bytes, plus 64 chroma bytes if bit0 set |
| M7 | :102-109 | MDEC(3): 64 signed halfwords, 14-bit fraction |
| M8 | :111-118 | **MDEC(0) and MDEC(4..7) have no function**: bits 25-28 reflect to status, bits 0-15 reflect to status without the minus-1, and **no parameters are expected** |
| M9 | :123-136 | Macroblock decode order: Cr, Cb, Y1..Y4 with yuv_to_rgb at (0,0), (8,0), (0,8), (8,8); mono = one Y block |
| M10 | :138-158 | rl_decode_block: FE00h padding skip, q_scale from bits 15-10, first value without q_scale/8, `(…*qt[k]*q_scale+4)/8` afterwards, saturation to -400h..+3FFh, zigzag store unless q_scale=0 |
| M11 | :192-218 | real_idct_core: two passes of `sum += src[y+z*8] * (scaletable[x+z*8]/8)`, `dst = (sum+0FFFh)/2000h`; the doc says hardware is only approximately this |
| M12 | :220-236 | yuv_to_rgb coefficients and MinMax(-128,127), xor 808080h when unsigned; exact fixed-point resolution unknown |
| M13 | :238-247 | y_to_mono: AND 1FFh, saturate to -128..127, xor 80h when unsigned |
| M14 | :271-295 | zigzag / scalezag / zagzig tables |
| M15 | :297-322 | The standard scale table values and their derivation |
| M16 | :326-359 | Colour macroblocks are 16x16 from six blocks; mono output is 8x8 |
| M17 | :386-425 | Block stream format: DCT halfword (Q in 15-10, DC in 9-0), RLE halfwords (LEN in 15-10, AC in 9-0), EOB = FE00h, and FE00h as padding at block start/after EOB |

### 1.6 Hardware numbers (`hardwarenumbers.md`)

This file is a catalogue, not a behavioural specification. The only things it can
be audited against are the model identifiers this project claims:

| # | Doc | Rule |
|---|---|---|
| H1 | :5-6, :54 | SCPH-1001 = NTSC-U/C PlayStation, SCPH-7502 = European PlayStation with DualShock — the two BIOS images this project targets |
| H2 | :46 | SCPH-5903 is the Asia-only PlayStation with the built-in VCD decoder — the only model with CDROM command 1Fh |
| H3 | :30, :25 | SCPH-1200 = DualShock (two motors); SCPH-1150 = analog pad with one vibration motor |
| H4 | :114, :105-107 | DTL-H2000 dev board and DTL-H100x debugging stations (the CDROM firmware version table's outliers) |
| H5 | :404-433 | Game-code prefixes (SCES/SLES/SLUS/SCPS…); multi-disc games have one code per disc, and the code is used as the boot executable name in SYSTEM.CNF |

---

## Part 2 — code audit

### 2.1 DMA registers — `src/core/dma.c`, `include/dma.h`

| Item | Code | Doc | Verdict |
|---|---|---|---|
| MADR write masked to 24 bits | `dma.c:221` | D1 (:20-21) | OK |
| **MADR never updated during/after a transfer** | `dma.c` has no writeback; `bus.c:1047`, `:1111-1147` keep the address in a local | D1 (:27-29) — SyncMode 1/2 must leave the end address / end marker in MADR | **DIVERGES** |
| BCR read/write as BS + BA | `dma.c:180`, `:224-225` | D2 (:36-50) | OK |
| **BCR never decremented** | `dma.c` — `block_count` untouched | D2 (:55-57) — SyncMode 1 decrements BA to zero | **DIVERGES** |
| Word count 0 means 10000h | `bus.c:958-968` | D2 (:51) | OK |
| CHCR field decode (dir, step, chop, sync, chop sizes, start, trigger) | `dma.c:49-66`, `:18-30` | D3 (:60-86) | OK |
| CHCR bits 29/30 dropped | `dma.c:28`, `:65` (commented out) | D3 (:81-85) | MISSING — bit30 (bus snooping) and bit29 (hold) are neither stored nor read back |
| Bit 24 cleared on completion | `dma.c:80-83` via `dma_channel_done` | D4 (:90-91) | OK |
| **Bit 28 cleared on completion, not on begin** | `dma.c:82` | D4 (:87-89) | DIVERGES (observable only mid-transfer) |
| **DMA6/OTC register restrictions** | `dma.c:49-66` applies every field to every channel | D5 (:92-94) — only bits 24/28/30 writeable, bit1 reads 1 | **MISSING** |
| DPCR reset value | `dma.c:128` `0x07654321` | D6 (:115) | OK |
| **DPCR priorities** | `bus.c:296` checks only the per-channel enable bit | D6 (:96-117) | MISSING — no arbitration; single-channel-at-a-time execution hides it |
| DICR field layout on read | `dma.c:191-202` | D7 (:119-131) | OK |
| DICR bits 0-6 stored as `dicr_unknown_rw` | `dma.c:194`, `:262`, `dma.h:66` | D7 (:121-124) — they enable an interrupt per slice / per linked-list block | **MISSING** (stored and echoed, never acted on) |
| Flags set only when channel-enable and master-enable are set | `bus.c:770-773`, `:865-868`, `:1174-1177` | D7 (:132-134) | OK |
| **Master flag formula includes the per-channel enables** | `dma.c:105-107` `force_irq \|\| (master_enable && (flags & enables))` | D8 (:139-142) — "the per-channel enable bits do not factor into the bit 31 calculation… once a flag bit is set it contributes regardless" | **DIVERGES**: clearing a channel's enable after its flag was set silently drops the interrupt |
| Flags acknowledged by writing 1 | `dma.c:271-276` | D8 (:145-146) | OK |
| I_STAT.3 set on the 0→1 edge of bit31 | `dma.c:116-117` | D8 (:144) | OK |
| **I_STAT.3 cleared when bit31 falls** | `dma.c:118-121` clears `inter->irq_status` | N2 (:4-5), N3 (:26-31) — I_STAT is cleared only by writing 0 to it | **DIVERGES** — a genuine pending IRQ3 disappears if the guest acks DICR before I_STAT |
| **Bus-error flag (DICR.15) never set** | no writer; `bus.c:786`, `:810`, `:885`, `:907`, `:1113` only log | D7 (:126), D11 (:186-192) — an out-of-range transfer raises it and forces bit31 | **MISSING** |
| 1F8010F8h/FCh | `dma.c:203-206` — logged as an error, returns 0 | D9 (:148-157) | DIVERGES (harmless; hardware returns fixed junk) |
| **CHCR mirrors at 1F8010xCh** | `dma.c:176-186` `default:` warns and returns 0 | D15 (`unpredictablethings.md:151-153`) | **MISSING** |
| Sub-word DMA register writes preserve the untouched lanes | `bus.c:277-292` (deliberate, modelled on PCSX-Redux) | N9 (`partialwordwrites.md:96-119`) — the register is **fully overwritten** with the shifted source word; "the pre-write contents do not contribute" | **DIVERGES** — hardware-measured behaviour is the opposite of what this code does. `unpredictablethings.md:80-82` names the SCPH-7xxx Soundscope as software that depends on it |
| Sub-word DMA register reads | `bus.c:267-273` | D16, `unpredictablethings.md:57-65` | OK |
| DICR bits 24-31 excluded from the sub-word merge | `bus.c:287-291` | D8 (:145) | OK — the right call given the (wrong) merge model |

### 2.2 DMA transfer engine — `src/core/bus.c:740-1178`, `src/core/event_scheduler.c`

| Item | Code | Doc | Verdict |
|---|---|---|---|
| Linked-list node decode (24-bit next, 8-bit count) | `bus.c:790-793` | D11 (:181-184) | OK |
| End marker: any address with bit23 set | `bus.c:805-808` | D11 (:186-192) | OK for stopping; **the DMA error the doc requires is not raised** (see DICR.15 above) |
| Linked-list address masked to word alignment | `bus.c:793`, `:796` | D1 (:30-31) | OK |
| OTC (ch6) descending pointer fill, FFFFFFh at the end | `bus.c:1132` | D10 (:167), `graphicsprocessingunitgpu.md` OT chapter | OK |
| CDROM (ch3) TO_RAM via `cdrom_dma_read_word` | `bus.c:1134` | D12 (:199-200) | OK |
| SPU (ch4) both directions in halfword pairs | `bus.c:1120-1123`, `:1135-1139` | D12 (:201) | OK |
| GPU (ch2) linked list sliced; block FROM_RAM read at kick time | `bus.c:1029-1036`, `:1063-1078` | D14 (:230-232) | PLAUSIBLE — the slicing matches "CPU resumes between list entries"; the synchronous block read is a deliberate deviation documented in the code comment |
| **GPU DMA pacing** | `bus.c:744-745` 64 words per 1000 cycles ≈ 15.6 clk/word | D12 (:198) — 1 clk/word, D13 (:215-220) — ~17 clks per 16 words | **DIVERGES** — GPU list DMA runs ~15x slower than hardware |
| MDEC DMA pacing charged as real word cost | `bus.c:759`, `:948-955` | D12 (:196-197), D13 (:215-220) | OK — `words + ceil(words/16)` is exactly the hyper-page model |
| MDEC slices gated on the FIFO flags | `bus.c:883`, `:905` | M3 (:59-63), M4 (:65-75) | OK |
| Per-channel stall on the synchronous path | `bus.c:1162-1171` `{1,1,1,40,4,20,1}` + 17 per 16 words | D12 (:194-204), D13 (:215-220) | OK (ch3 hardcodes the 40-clk "games" value; the 24-clk BIOS default is not modelled) |
| Sliced GPU completion charges a flat 1000 cycles | `bus.c:767-768` | D12 (:198) | DIVERGES (same root cause as the pacing row) |
| Re-kick while a slice is in flight: ch2 drains, ch0/ch1 dropped | `bus.c:1004-1019` | D3 (:78-79), D14 | PLAUSIBLE — hardware cannot lose the transfer; the ch0/ch1 drop is a known compromise, flagged in the code |
| CHCR write clearing bit24 cancels an in-flight slice | `dma.c:241`, `:36-45` | D3 (:90-91) | OK |
| CPU stall model during DMA | `bus.c:768`, `:953`, `:1170` — a downcount charge, no read-stall | D14 (:222-232) | PLAUSIBLE, not equivalent: a guest that reads RAM mid-DMA is not stalled |
| `EVQ_DMA_CDROM` handler is dead | `event_scheduler.c:229-234` | — | Housekeeping: CDROM DMA is synchronous; the empty slot is documented in place |

### 2.3 Interrupts — `src/core/bus_irq.c`, `src/core/bus.c:224-257`, `src/cpu/cpu_execution.c`

| Item | Code | Doc | Verdict |
|---|---|---|---|
| I_STAT/I_MASK addresses and 11-bit width | `interconnect.h:61-62`, `:104-113`, `bus.c:239`, `:253` | N1 (:7-19) | OK |
| I_STAT write semantics (0 clears, 1 keeps) | `bus.c:239-243` | N2 (:4-5) | OK |
| Edge-triggered latching of I_STAT | `bus_irq.c:17-31` — sets the bit only on a low→high transition of the internal line | N3 (:26-28) | OK |
| cop0r13.bit10 recomputed from `(I_STAT & I_MASK)`, not latched | `cpu_execution.c:28-33` | N5 (:55-62) | OK |
| Interrupt taken when `SR.IEc` and `(SR & CAUSE) & 0xFF00` | `cpu_execution.c:36-38` | N6 (`cpuspecifications.md:626-634`) | OK |
| Line state cleared on I_STAT acknowledge so a device can re-fire | `bus.c:244` | N4 (:33-47) | PLAUSIBLE — models the documented acknowledge order without modelling the device side |
| SPU IRQ9 device-side acknowledge folded into the I_STAT write | `bus.c:245-250` | N4 (:39-47) | OK (device-specific; SPU audit territory) |
| **An interrupt pending on a cop2 opcode is deferred instead of taken** | `cpu_execution.c:41-44` — returns without raising if the next opcode is `4A000000h`-shaped | N7 (`cpuspecifications.md:687-710`) — hardware **executes** the GTE command and leaves EPC pointing at it; the handler skips it | **DIVERGES** in mechanism. It produces the same visible result for BIOS-style handlers and avoids the double-execution the doc warns about, but interrupt latency stretches across a whole GTE chain, and a game with its own handler that relies on the EPC+4 skip sees different behaviour |
| I_STAT bits 16-31 read as zero | `bus.c:225-228` | N1 (:20) — garbage | DIVERGES (harmless) |
| **Partial-word writes to I_STAT/I_MASK are not shifted** | `bus.c:231-232` ignores `sz`, uses `val & 0x7FF` | N9 (`partialwordwrites.md:85-95`) — `sb +N` latches `src << N*8`, `sh +2` latches `src << 16` | **DIVERGES** |
| IRQ10 (lightpen/PIO) never raised | no writer | N1 (:18) | MISSING (no source implemented) |
| DMA drives IRQ3 through one authority | `dma.c:103-122`, `dma.h:99-102` | D8 (:144) | OK |

### 2.4 GTE — `src/gte/*`, `src/cpu/cpu_instructions.c`

| Item | Code | Doc | Verdict |
|---|---|---|---|
| Opcode decode (bits 0-5, sf bit19, lm bit10, MVMVA fields) | `gte.c:120`, `gte_ops.c:95-96`, `:136-140` | G1 (:45-57) | OK |
| **Cycle counts for all 22 ops** | `gte.c:124-145` | G12 (:315-352) | OK — every entry matches |
| Cycles charged to the CPU, next GTE op stalls until completion | `cpu_instructions.c:752-761` | G24 (:40-43) | OK |
| MFC2/CFC2 stall on an in-flight op, then take a 1-instruction load delay | `cpu_instructions.c:766-786` + `cpu_execution.c:59-62` | G24 (:29-32, :40-43) | OK — this is the Tekken 2 rule |
| **SWC2 does not stall on an in-flight op** | `cpu_instructions.c:964-975` | G24 (:40-43) — any instruction reading a GTE register holds the CPU | **DIVERGES** |
| **LWC2 writes a GTE register without regard to an in-flight op** | `cpu_instructions.c:932-943` | G24 (:40-43), P5 (:271-274) | DIVERGES / UNVERIFIED (the pipeline doc excludes LWC2 from its measurements) |
| MTC2/CTC2 do not stall | `cpu_instructions.c:787-792` | P2 (:33-39) | OK |
| **MTC2/CTC2 take effect immediately, not after 2-3 cycles** | `cpu_instructions.c:787-792` | G25 (`cpuspecifications.md:452-455`) | DIVERGES (minor; affects only code that reads back within 2 cycles) |
| COP2 usable check via SR.CU2 | `cpu_instructions.c:740-744`, `:933`, `:965` | N6 (`:666`) | OK |
| Data-register read widths (VZn/IRn sign-extended, OTZ/SZn zero-extended) | `gte.c:41-44` | G7 (:198-200), G5 (:168), G6 (:178-181) | OK |
| SXYP reads as SXY2; writing SXYP moves the FIFO, writing SXY0-2 does not | `gte.c:45-46`, `:71-75` | G6 (:177, :185-189) | OK |
| IRGB write expands to IR1-3; IRGB/ORGB read collapses IR1-3 saturated 0..1Fh | `gte.c:47-55`, `:76-83` | G9 (:232-257) | OK |
| **ORGB (cop2r29) is writeable** | `gte.c:90-92` default case stores it | G9 (:245) — read-only | DIVERGES (harmless: reads are computed from IR) |
| LZCS write updates LZCR | `gte.c:84-87` | G10 (:259-262) | OK for positive inputs |
| **LZCR for a negative LZCS with all bits set is undefined behaviour** | `gte.c:61-66` — `__builtin_clz(~s)` with `s = 0xFFFFFFFF` calls `__builtin_clz(0)` | G10 (:261-262) — result range is 1..32, so LZCS=FFFFFFFFh must give 32 | **DIVERGES** (UB in C; whatever the compiler emits is not 32) |
| LZCR write ignored | `gte.c:88-89` | G10 (:260) | OK |
| Control-register widths: RT33/L33/LB3/H/DQA/ZSF3/ZSF4 sign-extended on write | `gte.c:104-105` | G3 (:118-119), G4 (:155-161), G5 (:166-167) | OK |
| **H register read-back bug** | stored sign-extended (`gte.c:104`), used unsigned in RTPS (`gte_ops.c:58`) | G4 (:155-159) | OK — the documented bug is reproduced, and the calculation stays correct |
| FLAG write mask (bits 12-30) and bit31 recompute | `gte.c:107-112`, `gte_internal.h:397-401` | G11 (:279, :301-306) | OK — mask `0x7f87e000` is exactly bits 30..23 + 18..13 |
| **MVMVA does not reset FLAG** | `gte_ops.c:135-175` — no `control[GTE_CTL_FLAG] = 0`, unlike every other op | G11 (:302-303) — "all bits are automatically reset at the begin of a new GTE command" | **DIVERGES** — MVMVA inherits the previous command's flags; a game polling FLAG after MVMVA reads stale saturation bits |
| MAC overflow flags (43-bit for MAC1-3, 31-bit for MAC0) | `gte_internal.h:121-139` | G11 (:280-285, :296-297) | OK |
| IR saturation flags and lm handling | `gte_internal.h:148-164` | G11 (:286-288) | OK |
| IR0 clamp 0..1000h with flag 12 | `gte_internal.h:149-153` | G11 (:298) | OK |
| Colour FIFO push: MACn/16 saturated, flags 21/20/19, CODE preserved | `gte_internal.h:172-197` | G22 (:594-603) | OK |
| SZ FIFO push with flag 18 | `gte_internal.h:199-208` | G11 (:292) | OK |
| SXY FIFO push with flags 14/13 | `gte_internal.h:210-218` | G11 (:296-297) | OK |
| **UNR division** | `gte_internal.h:225-240` + table `:89-107` | G23 (:611-664) | OK — table matches all 257 entries; the `(0x80 - d*u) >> 8` form is algebraically identical to the doc's `(0x2000080 - d*u) >> 8` minus the 0x20000 that is added back in the next line; overflow test, FLAG.17/31 and the 1FFFFh clamp all match |
| RTPS/RTPT: MAC1-3, SZ3 = MAC3 SAR ((1-sf)*12), SX/SY/IR0 | `gte_ops.c:33-75` | G13 (:416-422) | OK — `push_sz(mac3 >> 12)` on the pre-shift value reproduces the `(1-sf)` shift for both sf settings |
| RTPS IR3 flag evaluated as lm=0 while IR3 is stored per lm | `gte_ops.c:44-54` | G13 (:435-439) | OK |
| RTPT repeats for V0/V1/V2, depth cue only on the last | `gte_ops.c:108-119` | G13 (:410-414) | OK |
| NCLIP | `gte_ops.c:122-132` | G14 (:443) | OK |
| AVSZ3/AVSZ4 with OTZ saturation | `gte_ops.c:413-447` | G15 (:454-456) | OK |
| MVMVA matrix/vector/translation selection | `gte_ops.c:143-174` | G16 (:471-481) | OK |
| MVMVA FC translation bug | `gte_ops.c:173`, `gte_internal.h:260-279` | G16 (:485-488) | OK |
| **MVMVA garbage matrix (mx=3) last row** | `gte_ops.c:151-152` uses **RT21** for `m[6..8]` | G16 (:489-491) — the elements are `…, RT22, RT22, RT22` | **DIVERGES** |
| SQR / OP | `gte_ops.c:178-215` | G17 (:496-509) | OK |
| NCS/NCT/NCCS/NCCT/NCDS/NCDT pipelines | `gte_ops.c:79-91`, `:218-312`, `gte_internal.h:282-348` | G18 (:530-535) | OK |
| CC / CDP | `gte_ops.c:264-283` | G18 (:543-547) | OK |
| DPCS / INTPL / DCPL | `gte_ops.c:315-363` | G19 (:557-562) | OK |
| DPCT reads RGB0 on each of its three passes | `gte_ops.c:328-340` | G19 (:564-567) | OK |
| GPF / GPL (including GPL's `SHL (sf*12)` on the previous MAC) | `gte_ops.c:366-410` | G20 (:572-575) | OK |
| "MAC+(FC-MAC)*IR0" with the lm=0 intermediate saturation | `gte_internal.h:351-379`, `:166-170` | G21 (:582-587) | OK |
| **No input-latch pipeline** | every op reads all its inputs at issue (`gte_ops.c` throughout) | P1/P3/P4 (:19-236) | **UNVERIFIED by construction** — our model is "every input latches at N=0". Code that clobbers an input register a few instructions after the cop2 op gets the *new* value on hardware for any register whose boundary is >0 (RGBC at N=12-15 for NCCT/NCDT, DQA/DQB at N=3-7 for RTPS/RTPT, LB3/LG2LG3 at N=5-8 for the triples) and the *old* value here |
| Unimplemented opcodes warn once per opcode | `gte.c:146-155` | G12 (:353) — "unknown if/what happens" | OK (doc has no answer) |

### 2.5 MDEC — `src/core/mdec.c`, `include/mdec.h`, `src/core/bus.c:371-381`

| Item | Code | Doc | Verdict |
|---|---|---|---|
| Port addresses and read/write split | `mdec.c:503-534`, `bus.c:112` | M1 (:13-29) | OK |
| Status bits 31/30/29 | `mdec.c:62-65` | M2 (:33-35) | OK |
| Status bits 28/27 gated on the DMA enables | `mdec.c:66-69` | M2 (:36-37), M3 (:59-60) | OK in shape |
| **In-request threshold of 64 halfwords** | `mdec.c:66` | M2 (:36) — no threshold is documented | UNVERIFIED |
| **Out-request stays set for the whole block** | `mdec.c:67` | M3 (:60-63) — it clears after the first few words are read | DIVERGES |
| Status depth/signed/bit15 | `mdec.c:70-72` | M2 (:38-40) | OK |
| Status current-block encoding | `mdec.c:73` `(current_block + 4) % 6` | M2 (:42-50) — 0..3 = Y1..Y4, 4 = Cr, 5 = Cb; mono always 4 | OK (our internal order is Cr, Cb, Y1..Y4) |
| **Status "words remaining" reports 0 instead of FFFFh when idle** | `mdec.c:74-75` | M2 (:43) | DIVERGES (minor) |
| Reset via control bit31 produces status 80040000h | `mdec.c:518-521` + `mdec_get_status` | M3 (:54) | OK — verified by construction: empty output FIFO + idle + current_block 0 gives exactly 80040000h |
| DMA enable bits 30/29 | `mdec.c:523-524` | M3 (:55-56) | OK |
| MDEC(1) parameter decode | `mdec.c:410-421` | M5 (:82-87) | OK |
| MDEC(2) 16 (+16) words | `mdec.c:423-425`, `:350-368` | M6 (:94-100) | OK |
| MDEC(3) 32 words, table stored untransposed | `mdec.c:427-429`, `:370-394` | M7 (:104-108), M11 (:192-196) | OK — the orientation is tied to the IDCT indexing and the code says so |
| **MDEC(0)/(4..7) consume parameter words** | `mdec.c:431-435`, `:479-487` | M8 (:111-118) — no parameters are expected | **DIVERGES** |
| Output FIFO cleared on every new command | `mdec.c:415` | M2/M3 | UNVERIFIED (not documented) |
| RLE decode: FE00h skip, q_scale, first-value rule, `(…+4)/8`, saturation, zigzag | `mdec.c:105-155` | M10 (:138-158), M17 (:386-425) | OK |
| Block ends at coefficient 63 without consuming a trailing EOB | `mdec.c:149-152` | M17 (:415-417) — EOB after a complete block is padding | OK (the next block's FE00h skip absorbs it) |
| real_idct_core two passes, `scaletable/8`, `(sum+0FFFh) >> 13` | `mdec.c:172-186` | M11 (:198-211) | OK; the `/8` on a negative int16 truncates toward zero where hardware would shift — PLAUSIBLE, and the doc (:213-218) says hardware is only approximately this anyway |
| **The 9-bit clip is applied to every block, including Cr/Cb** | `mdec.c:188-190` | M13 (:238-247) applies it in y_to_mono; M12 (:235-236) only suspects "probably also some 9bit limit" for yuv_to_rgb | **UNVERIFIED / DIVERGES** — clamping the chroma blocks to ±127 before yuv_to_rgb is a decision the documentation does not support |
| yuv_to_rgb coefficients, MinMax, unsigned bias | `mdec.c:199-221` | M12 (:220-234) | OK |
| y_to_mono | `mdec.c:225-231` | M13 (:238-247) | OK |
| Macroblock order Cr, Cb, Y1..Y4 and the four yuv_to_rgb quadrants | `mdec.c:326-343` | M9 (:123-136) | OK |
| Output packing 4/8/24/15 bpp | `mdec.c:237-301` | M2 (:38), M16 (:326-359) | OK |
| **Manual reads from 1F801820h return the DMA-reordered stream** | `mdec.c:509-511` feeds the same FIFO the 16x16 assembly filled | M1 (:25-29) — the hardware port yields 8x8 blocks and the re-ordering is DMA1's job | **DIVERGES** (only affects software that reads the port directly) |
| Empty output read returns FFFFFFFFh | `mdec.c:546-550` | M1 (:23) — "garbage" | OK |
| Input FIFO 2048 halfwords, output 768 words | `mdec.h:37-38` | M2 (:34) | UNVERIFIED — hardware sizes are not documented; the sizes chosen let a whole burst land at once, which is why `bus.c:872-874` has to gate the DMA slices instead |
| **8/16-bit MDEC writes are dropped** | `bus.c:377-381` | N9 (`partialwordwrites.md:80-95`), `unpredictablethings.md:34-35` (marked "?") | DIVERGES against the on-die rule; the doc itself does not measure MDEC |
| 8/16-bit MDEC reads truncate the word | `bus.c:371-376` | `unpredictablethings.md:57-65` | PLAUSIBLE |

### 2.6 Hardware numbers cross-check

| Item | Code | Doc | Verdict |
|---|---|---|---|
| BIOS images named SCPH-1001 (US) and SCPH-7502 (PAL) | `CLAUDE.md:36`, `README.md:60`, `:138` | H1 (:5-6, :54) | OK — both models exist and the regions match |
| CDROM controller version reported as 95h,05h,16h,C1h (LATE-PU-8) | `cdrom_commands.c:494-497` | `cdromdrive.md:1157`; H1 (:43-44) places LATE-PU-8 in the SCPH-5500 era | OK, but **inconsistent with the SCPH-7502 BIOS we run**: that console carries a vC2 controller (`cdromdrive.md:1163-1164`). The code comment at `cdrom_commands.c:487-493` states the reason (vC2 needs a real ReadTOC) |
| VideoCD command 1Fh refused | `cdrom_commands.c:585-587` | H2 (:46), `cdromdrive.md:1655-1660` | OK — only SCPH-5903 has it |
| DualShock rumble comments cite SCPH-1200 / SCPH-1150 | `sio.c:156`, `:165-166` | H3 (:25, :30) | OK — the two-motor pad is SCPH-1200 and the one-motor analog pad is SCPH-1150 |
| Nothing else in `hardwarenumbers.md` constrains this emulator | — | H4, H5 (:97-135, :404-433) | Noted: the devkit and game-code sections are catalogue data; the only latent use is deriving the boot executable name from SYSTEM.CNF, which the BIOS does for us |

---

## Part 3 — findings, ordered by what they would change

1. **Sub-word writes to the DMA and interrupt registers use the wrong model**
   (`bus.c:277-292`, `bus.c:231-232`; `partialwordwrites.md:85-119`). Hardware
   latches the *shifted source word in full* and the previous contents do not
   contribute; we merge lanes. The measured table exists precisely because
   software depends on it (`unpredictablethings.md:80-82` names the SCPH-7xxx
   Soundscope, which halfword-writes DMA registers).
2. **DICR's master flag is computed with the per-channel enables**
   (`dma.c:105-107`; `dmachannels.md:139-142`). A game that sets a flag, then
   clears the channel enable before acknowledging, loses the interrupt here and
   keeps it on hardware.
3. **DMA completion clears I_STAT.3 behind the CPU's back** (`dma.c:118-121`;
   `interrupts.md:4-5`). I_STAT is supposed to be cleared only by a write of 0.
4. **The DMA bus-error flag is never raised** (`dmachannels.md:126`, `:186-192`;
   no writer in the tree). Games that end a linked list by setting the high bit
   of the next-address field expect the error path, and out-of-range transfers
   are exactly the class of bug this project has already been bitten by.
5. **MVMVA never resets the FLAG register** (`gte_ops.c:135-175`;
   `geometrytransformationenginegte.md:302-303`). Every other opcode does. Any
   code that checks FLAG after an MVMVA sees the previous command's saturation
   bits.
6. **GTE LZCR is undefined for LZCS = FFFFFFFFh** (`gte.c:61-66`;
   `:261-262`). `__builtin_clz(0)` is UB; the documented answer is 32.
7. **MVMVA's garbage matrix uses RT21 where the documentation says RT22**
   (`gte_ops.c:151-152`; `:489-491`).
8. **SWC2 does not stall on an in-flight GTE command** (`cpu_instructions.c:964-975`;
   `:40-43`), so a store of a GTE register issued too early reads a stale value
   that hardware would have made the CPU wait for.
9. **MDEC command 0 (and 4-7) consumes parameter words** (`mdec.c:431-435`;
   `macroblockdecodermdec.md:111-118`). On hardware those commands expect no
   parameters, so a stream using MDEC(0) as a status poke desynchronises here.
10. **GPU linked-list DMA runs at ~15.6 clocks per word** (`bus.c:744-745`)
    against the documented 1 clk/word plus hyper-page overhead
    (`dmachannels.md:194-220`). The MDEC path already does this correctly
    (`bus.c:759`), so the model to copy is in the same file.
11. **MADR and BCR are never written back** (`dmachannels.md:23-34`, `:55-57`),
    and **DMA6's register restrictions are not modelled** (`:92-94`), and the
    **CHCR mirrors at 1F8010xCh do not exist** (`unpredictablethings.md:151-153`).
12. **DICR bits 0-6 (per-slice / per-list-entry interrupts) are stored and
    ignored** (`dma.c:194`, `:262`; `dmachannels.md:121-124`).
13. **The MDEC 9-bit clip is applied to the chroma blocks too**
    (`mdec.c:188-190`), which the documentation does not support
    (`:220-236` explicitly leaves the colour path's limits open). Suspect this
    first if FMV colour looks desaturated in saturated regions.
14. **Interrupts on a cop2 opcode are deferred rather than taken**
    (`cpu_execution.c:41-44`; `cpuspecifications.md:687-710`). Safe against the
    double-execution the doc warns about, but it is a different mechanism, and
    it delays interrupts across a whole GTE chain.
15. **Manual reads of the MDEC data port return DMA-ordered data**
    (`mdec.c:509-511`; `:25-29`), and the **out-request flag never clears
    mid-block** (`:60-63`).
16. Smaller, recorded so they are not re-discovered: CHCR bit 28 clears at the
    wrong time (`dma.c:82`), CHCR bits 29-30 are not stored (`dma.c:28`), DPCR
    priorities are absent (`bus.c:296`), ORGB is writeable (`gte.c:90-92`),
    MTC2/CTC2 have no 2-3 cycle store delay (`cpuspecifications.md:452-455`),
    I_STAT's upper bits read 0 instead of garbage, MDEC's "words remaining"
    reports 0 instead of FFFFh, and 1F8010F8h/FCh log an error instead of
    returning their fixed values.

Not defects, recorded with both citations so they are not re-investigated: all
22 GTE cycle counts match the opcode table; the UNR divider matches the
published algorithm and all 257 table entries; the H-register sign-extension bug
is reproduced while the RTPS maths stays unsigned; RTPS's SZ3 shift and the
lm-independent FLAG.22 rule are right; DPCT does read RGB0; the MVMVA far-colour
bug is modelled; MFC2/CFC2 carry the one-instruction load delay Tekken 2 needs;
MDEC's reset status is exactly 80040000h; the RLE decoder, IDCT, yuv_to_rgb and
y_to_mono follow the published pseudocode; the MDEC DMA pacing uses the
documented DRAM hyper-page cost; I_STAT is edge-triggered and cop0r13.bit10 is
recomputed rather than latched; DPCR's reset value is 07654321h.

---

## Part 4 — what is UNVERIFIED, and why

- **GTE pipeline input latching** (`gtepipelinetimings.md` in full). We execute
  each op atomically at issue, so every input behaves as if its boundary were
  N=0. Reproducing the measured boundaries needs an in-flight input snapshot the
  emulator does not have; until then no code line can be compared against P3's
  tables. The doc itself caps its own precision at ±1-2 nops (:241-246) and one
  console (:247-252).
- **MDEC FIFO depths** (`mdec.h:37-38`). The hardware sizes are not published;
  ours were chosen large and the DMA gating in `bus.c:872-938` compensates.
- **MDEC in/out request thresholds and the out-request clear-after-first-words
  behaviour** (`macroblockdecodermdec.md:36`, `:60-63`). Documented
  qualitatively only.
- **IDCT rounding** (`macroblockdecodermdec.md:213-218`). The doc states the
  hardware "isn't perfect" and lists three places where extra rounding may
  happen; exactness cannot be claimed from it.
- **MDEC 8/16-bit port writes** (`unpredictablethings.md:34-35` marks them "?").
  The on-die rule in `partialwordwrites.md` suggests they should latch a shifted
  word; nobody has measured MDEC directly.
- **DMA priority arbitration and the CPU stall model** (`dmachannels.md:96-117`,
  `:222-232`). We run one channel at a time to completion, so priorities have no
  observable effect here; whether any title depends on interleaving is untested.
- **CHCR bits 29/30** (`dmachannels.md:81-85`). The doc's own description of
  bit29 ("no effect when the transfer was caused by a DREQ") is conditional, and
  bit30 (bus snooping) has no known consumer.
- **1F8010F8h/1F8010FCh contents** (`dmachannels.md:148-157`). The doc records
  observed values and admits it does not know what they are.
- **`hardwarenumbers.md` beyond the models this project names.** It is a
  catalogue; the devkit, licensed-peripheral and game-code sections constrain no
  code here. The one live consequence is recorded in Part 2.6: we report a vC1
  CDROM controller while running an SCPH-7502-class BIOS.
