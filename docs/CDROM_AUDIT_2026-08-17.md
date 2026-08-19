# CDROM audit against psx-spx (official clone, 2026-08-17)

Source of truth: `psx-spx-docs/docs/*.md` (fresh clone of psx-spx/psx-spx.github.io,
five CDROM files: `cdromdrive.md` 2193, `cdromformat.md` 1831, `cdromfileformats.md`
16373, `cdrominternalinfoonpsxcdromcontroller.md` 2141, `cdromvideocdsvcd.md` 887).
Line numbers below are that clone's, **not** the older `DOCS/` fork's.

Rule of this audit: no entry may say "correct" without citing both a doc line and a
code line. Anything not actually checked is marked `UNVERIFIED`, not "ok".

Status legend: `OK` verified both sides · `DIVERGES` measured difference · `MISSING`
documented behaviour absent · `UNVERIFIED` not yet checked.

---

## Part 1 — rules extracted from cdromdrive.md

### Host interface registers

| # | Doc | Rule |
|---|---|---|
| R1 | :58-70 | HSTS: bit0-1 bank (R/W), 2 ADPBUSY, 3 PRMEMPT, 4 PRMWRDY, 5 RSLRRDY, 6 DRQSTS, 7 BUSYSTS |
| R2 | :94-101 | Parameter FIFO 16 bytes; PRMWRDY clears when full |
| R3 | :131-141 | Result FIFO 16 bytes; reads past the response pad with 00h to 16, then wrap to the first byte, same 16 until a new response |
| R4 | :103-112 | HCHPCTL: bit5 SMEN, bit6 BFWR, bit7 BFRD |
| R5 | :114-124 | RDDATA: 800h or 924h sectors; reads past the end repeat the byte at \[800h-8\] / \[924h-4\] |
| R6 | :143-149 | HINTSTS: bit0-2 INT type, 3 BFEMPT, 4 BFWRDY, 5-7 always 1 |
| R7 | :171-181 | HINTMSK bit0-4; IRQ fires whenever (HINTMSK & HINTSTS) != 0 |
| R8 | :183-197 | HCLRCTL: bit0-2 CLRINT, 3 CLRBFEMPT, 4 CLRBFWRDY, 5 SMADPCLR, 6 CLRPRM, 7 CHPRST. After acknowledge the result FIFO is drained and a pending command is sent to the controller |
| R9 | :163-169 | Response interrupts queue: INT3 first, INT5 not delivered until INT3 acknowledged. INT5 may also come unsolicited on lid open |
| R10 | :227-247 | ATV0-ATV3 volume matrix, saturation up to double volume; CHNGATV in ADPCTL applies |
| R11 | :249-255 | ADPCTL: bit0 ADPMUTE, bit5 CHNGATV |
| R12 | :315-320 | 32-bit read of 1F801800h returns HSTS four times (auto-increment off) |

### Commands

| # | Doc | Rule |
|---|---|---|
| C1 | :380-421 | Opcode table: unused opcodes answer INT5(11h,40h); 58h-5Fh crash |
| C2 | :506-513 | Setfilter selects file/channel for ADPCM only; does not affect data-sector reading |
| C3 | :515-533 | Setmode bits: 7 speed, 6 XA-ADPCM, 5 sector size, 4 ignore, 3 XA-filter, 2 report, 1 autopause, 0 CDDA |
| C4 | :535-540 | Init: mode=20h, motor on, standby, abort all commands. **An Init while another Init's second response is pending is silently dropped — no INT3, no INT5** |
| C5 | :542-554 | Reset: INT3 only, then software must wait 1/8 s (400000h cycles) |
| C6 | :556-570 | MotorOn: works only if motor was off, else INT5(stat,20h); no disc → INT5(stat,80h); does not pause anything |
| C7 | :572-578 | Stop: first response has bit5 already cleared, second has bit1 cleared; head moves to start of first track |
| C8 | :580-588 | Pause: first response still has bit5 set, second has it cleared. **Fails INT5(stat,80h) during certain seek phases, including the implicit seek of ReadN/ReadS/Play** |
| C9 | :590-612 | Sector delivery algorithm, in this order, with the documented two-attempt data-delivery bug |
| C10 | :617-628 | Setloc: BCD amm/ass/asect, ass < 60h, asect < 75h, else INT5(stat,10h); target memorised, no seek started |
| C11 | :630-639 | SeekL: data-mode seek; **stops any current or pending ReadN/ReadS**; lands N-8..N-0 (1x) or N-5..N+2 (2x); on audio CDs second response errors (stat+4, 04h) |
| C12 | :641-651 | SeekP: subq-mode seek, same stop rule, stat.bit7=0 after seek |
| C13 | :653-681 | SetSession: error 80h during active read/play; session 00h → INT5(03h,10h) |
| C14 | :686-700 | ReadN: repeated INT1 until Pause. Unlicensed disc → INT5 with stat 03h, error 40h, no INT3/INT1. Audio-CD sectors need CDDA mode, else 40h; **region mismatch also gives 40h unless CDDA** |
| C15 | :749-756 | ReadS: read without retry; both read sequentially from the Setloc sector |
| C16 | :758-784 | Buffer overrun: sectors are lost silently; data already requested stays intact |
| C17 | :799-814 | Setloc/Read/Pause ordering rules: Read with unprocessed Setloc seeks; Read without one continues, or after Pause resumes at the most recently received sector |
| C18 | :819-860 | stat bits and error bytes: 10h invalid param, 20h wrong param count, 40h invalid command, 80h cannot respond yet; 04h seek failed (second response); 08h door opened |
| C19 | :862-867 | Only ONE of Play/Seek/Read set at a time; Read does not set until seek completes (Gran Turismo 1 relies on it) |
| C20 | :869-876 | Nop additionally resets the shell-open flag; other commands do not |
| C21 | :878-880 | Getparam returns stat, mode, 00h, file, channel |
| C22 | :882-901 | GetlocL: header+subheader of the **newest buffered sector**. Fails 80h on audio CDs/tracks. **Fails 80h while in the Seek phase** (e.g. shortly after ReadN/ReadS) — the guest retries until the seek completes |
| C23 | :903-916 | GetlocP: track, index, rel mm:ss:sect, abs mm:ss:sect, all BCD |
| C24 | :920-934 | GetTN BCD first/last; GetTD param 00h = end of last track, > NN → 10h, non-BCD → 10h, values rounded down to second |
| C25 | :969-1010 | GetID response matrix per drive/disc state, SCEx bytes, flags bit7 denied / bit6 missing / bit4 audio |
| C26 | :1018-1029 | Mute/Demute affect CD-DA and XA-ADPCM; muting only forces output volume to zero, sectors are still processed internally |
| C27 | :1031-1050 | Play: optional track param, INT4 at end of disc with motor off |
| C28 | :1052-1066 | Forward/Backward only work in Play state, else INT5(stat+1,80h) |
| C29 | :1068-1075 | During Play only Setmode bits 7, 2, 1 are used |
| C30 | :1077-1095 | Report INT1 packet: stat, track, index, mm/amm, ss+80h/ass, sect/asect, peaklo, peakhi; absolute on asect 00/20/40/60h, in-track on 10/30/50/70h |
| C31 | :1097-1116 | AutoPause: INT4 + pause at end of track (bit1=1) or INT4 + stop at end of disc (bit1=0) |
| C32 | :1118-1135 | XA-ADPCM: INT1 only for non-ADPCM sectors when XA/XA-filter enabled; sector-size bit is don't-care for ADPCM forwarding |
| C33 | :1149-1234 | Test commands: 19h,20h version bytes, 19h,21h switches, 19h,22h region string, 19h,23h-25h chip IDs, 19h,04h/05h SCEx counters |
| C34 | :1606-1646 | Secret unlock 50h-57h always answer INT5(11h,40h) |

### Timing

| # | Doc | Rule |
|---|---|---|
| T1 | :1877-1894 | First response: Nop average 0xc4e1 (50401 cy), min 0x4a73, max 0x3115b; when stopped 0x5cf4. **Init first response 0x13cce** (81102 cy) |
| T2 | :1896-1906 | Second response: GetID 0x4a00; Pause 1x 0x21181c, 2x 0x10bd93, when paused 0x1df2; Stop 1x 0xd38aca, 2x 0x18a6076, when stopped 0x1d7b |
| T3 | :1907-1921 | Seek/Play/Read/SetSession/MotorOn/Init/ReadTOC second responses depend on seek time, plus spin-up if the motor was off |
| T4 | :1923-1932 | INT1 rate: 1x 0x6e1cd, 2x 0x36cd2; exact value SystemClock*930h/4/44100 |
| T5 | :1788-1796 | Command sequence: busy set, result FIFO populated, command processed, busy cleared + parameter FIFO cleared, IRQ 1000-6000 cycles later |
| T6 | :1814-1830 | A command executes only when no INT is pending; the mainloop may generate INT1/INT2 before executing it |
| T7 | :1799-1812 | One undelivered INT1 maximum; effectively three sector slots are usable |

### Sector buffer behaviour

| # | Doc | Rule |
|---|---|---|
| B1 | :1939-1956 | 8 slots exist, only the oldest and the newest are reachable; skipped sectors are lost with no error flag |
| B2 | :1958-1999 | Test cases: after a delay, INT1 jumps from the oldest to the newest sector |
| B3 | :2001-2045 | GetlocL vs sector buffer: GetlocL reports the newest sector even while INT1s for older ones are pending |
| B4 | :2047-2087 | Pause vs buffer: the drive keeps writing to the buffer during Pause because Pause is not processed until the INT1 is |
| B5 | :2089-2193 | Getloc+Pause command pairs, including a case where the second Pause response is lost |

---

## Part 2 — rules extracted from cdromformat.md (lines 1-1000 read)

| # | Doc | Rule |
|---|---|---|
| S1 | :466-504 | Sector layouts. Mode2/Form1: subheader at 010h, copy at 014h, 800h data at 018h. Mode2/Form2: 914h (2324) data at 018h, EDC at 92Ch or zero |
| S2 | :630-641 | Subheader byte 1 file, byte 2 channel (bits 0-4, 5-7 zero); Ace Combat 3 uses channel FFh for gaps |
| S3 | :643-657 | Submode bits: 0 EOR, 1 Video, 2 Audio, 3 Data, 4 Trigger, 5 Form2, 6 RealTime, 7 EOF |
| S4 | :659-671 | Codinginfo: bits 0-1 mono/stereo, bit 2 rate (0=37800, 1=18900), bits 4-5 bits per sample (0=4bit, 1=8bit), bit 6 emphasis |
| S5 | :673-721 | Interleave: 1/8 for 37800 stereo at 2x, 7/8 for video; unused sectors marked by channel FFh or submode 00h |
| S6 | :750-763 | ADPCM sector = 12h portions of 128 bytes (900h), remaining 14h bytes zero. Portion header: 4 copy bytes, 8 block headers at 04h-0Bh, copy at 0Ch-0Fh |
| S7 | :778-788 | Header byte: bits 0-3 shift (0..12; 13..15 behave as 9), bits 4-5 filter (0..3), bits 6-7 unused. 4-bit samples expand by <<12, 8-bit by <<8 |
| S8 | :790-807 | Data words: 4-bit → 8 blocks per 32-bit word; 8-bit → 4 blocks per word |
| S9 | :809-825 | decode_sector: 18 portions, per portion 4 blocks × (left, right) for stereo, or low-then-high nibbles for mono |
| S10 | :827-845 | decode_28_nibbles: shift = 12 - (hdr AND 0Fh); filter = (hdr AND 30h) >> 4; s = (t << shift) + ((old*f0 + older*f1 + 32)/64), clamped to 16 bits, **clamped value is fed back** |
| S11 | :843-847 | f0 = (0, +60, +115, +98, +122), f1 = (0, 0, -52, -55, -60); XA has only filters 0..3 |
| S12 | :849-854 | old/older carry across portions; whether a seek resets them is undocumented |
| S13 | :856-935 | 37800 → 44100: ring buffer of 32, sixstep counter of 6, seven ZigZagInterpolate outputs per six inputs, `sum += ringbuf[(p-i) AND 1Fh] * TableX[i] / 8000h` for i=1..29, result clamped. **Seven tables of 29 entries are given for 37800 only** |
| S14 | :930-935 | 18900 resampling is described only qualitatively ("lower-pitch zigzag, spread across about fifty 44100 Hz samples"). No tables are published |
| S15 | :952-958 | Six-step counter is uninitialised random at power-up and falls back to that value after each 900h-byte sector; a sector holds a multiple of six samples so it is unchanged across a sector |
| S16 | :469 | CD-DA sector: 2352 bytes as LeftLsb, LeftMsb, RightLsb, RightMsb |

---

## Part 3 — code audit

### src/cdrom/cdrom_audio.c — XA ADPCM decode and resampling

Checked line by line against S6-S16. Every row below names the code line and the
doc line that were compared.

| Item | Code | Doc | Verdict |
|---|---|---|---|
| Portions per sector = 18 | `cdrom_audio.c:283` `for (chunk = 0; chunk < 18; chunk++)` | S6, S9 (`:750`, `:812`) | OK |
| Portion stride 128 bytes | `:284` `xa_data + chunk * 128` | S6 `:752` | OK |
| Block headers at +4 | `:81` `headers = chunk + 4` | S6 `:755-762` | OK |
| Data words at +16 | `:82` `words = chunk + 16` | S6 `:767` | OK |
| Blocks: 8 (4-bit) / 4 (8-bit) | `:79` `num_blocks = bits8 ? 4 : 8` | S8 `:790-807` | OK |
| Shift amount | `:86,:128` `shift = hdr & 0x0F` then `nibble >>= shift` after `n4 << 12` | S7, S10 (`shift = 12 - (hdr AND 0Fh)`, `t SHL shift`) | OK — algebraically identical |
| Reserved shift 13..15 | `:99` `if (shift > 12) shift = 9` | S7 `:786-788` | OK |
| Filter is 2 bits | `:98` `(hdr >> 4) & 0x03` | S10 `:830`, S11 `:846` | OK |
| Filter coefficients | `:68-69` | S11 `:843-844` | OK (5th entry unreachable, harmless) |
| 4-bit nibble select | `:125` `(word >> (block*4)) & 0x0F` | S10 `:834` `src[16+blk+j*4] SHR (nibble*4)` | OK — our block index b maps to doc (blk = b/2, nibble = b&1) |
| 8-bit byte select and <<8 | `:121-123` | S7 `:784-787`, S8 `:803-806` | OK |
| Stereo channel assignment | `:103` `ch = block & 1` | S9 `:814-816` (nibble 0 = left, 1 = right) | OK |
| Stereo output interleave | `:108-109` | S9 `:814-816` | OK |
| Mono output order | `:111` `block * 28` | S9 `:817-819` (low nibbles then high) | OK |
| IIR accumulate | `:130-131` `+ ((prev0*fpos + prev1*fneg + 32) >> 6)` | S10 `:836` `/64` | OK in value, **rounding differs for negative sums** (`>>` floors, `/` truncates toward zero). Doc itself notes small rounding errors |
| Clamped feedback | `:134-136` | S10 `:837` | OK |
| old/older carried across sectors | `:280-288` via `xa->prev1/prev2` | S12 `:849-854` | OK — and the doc leaves seek behaviour open, so our carry-across-seek is **UNVERIFIED**, not proven right. Three `XA sequence break` warnings per 200 s run mean this path is being exercised at seeks |
| 37800 ring size 32, sixstep 6, 7 outputs | `:219-234` | S13 `:879-890` | OK |
| 37800 tap mapping | `:205` `ring[(p + 31 - i) & 0x1F] * tbl[i]`, i = 0..28 | S13 `:893` `ringbuf[(p-i) AND 1Fh] * TableX[i]`, i = 1..29 | OK — tbl[0] pairs with ring[p-1] |
| 37800 per-tap scaling | `:205` `>> 15` per tap | S13 `:893` `/8000h` per tap | OK in magnitude, same negative-rounding note |
| CD-DA 588 frames, LSB-first | `:326-330` | S16 `:469` | OK |
| **18900 tap mapping** | `:213` `ring[(p + 32 - 25 + i) & 0x1F]`, i = 0..24 → taps run **oldest → newest** | S13/S14 | **DIVERGES from the 37800 convention in the same file.** The 37800 path weights the newest sample with tbl[0]; this one weights it with tbl[24]. One of the two is wrong, and psx-spx publishes no 18900 table to settle it |
| **18900 tables** | `:167` `s_zigzag18[7][25]` | S14 `:930-935` | **UNVERIFIED — not from psx-spx.** The docs publish tables for 37800 only and describe 18900 as spread over ~50 output samples, which 25 taps cannot represent. Source of these 25 values is unknown |
| 18900 output ratio 7:3 | `:251-252` | 44100/18900 = 7/3 | OK arithmetically |
| 18900 scaling | `:214` one `>> 15` after summing full products | S13 `:893` divides per tap | DIVERGES — different rounding and a different overflow profile than the documented per-tap division |
| Mute keeps decoding | `:289` returns after the ADPCM decode, before resample | C26 (`cdromdrive.md:1018-1022`) | OK for ADPCM state; **the zigzag ring and sixstep do not advance while muted**, so the filter history is stale on unmute. Not covered by the docs either way — flagged, not claimed correct |

### Still to audit — none

Closed on 2026-08-17 (second session). The five CDROM doc files were read end to
end and every remaining source file was compared line by line: `cdrom.c`,
`cdrom_commands.c`, `cdrom_disc.c`, `cdrom.h`, `cdrom_disc.h`, `cdrom_audio.h`,
plus `src/core/bus.c`'s CDROM routing and `src/core/bus_irq.c`'s IRQ path, which
are part of the host interface the docs describe. Parts 5-8 below hold that work.
What could not be decided from the documentation is listed in Part 8 as
`UNVERIFIED`, not as correct.

## Part 4 — findings so far

1. **18900 Hz XA resampling is not derived from the documentation** and contradicts
   the 37800 path in this same file over tap order (`cdrom_audio.c:205` vs `:213`).
   Any title using 18900 Hz XA gets a filter nobody can check. Crash Team Racing is
   named in `cdromformat.md:691` as a 1/16 user.
2. **A re-issued `Init` must be dropped with no response at all**
   (`cdromdrive.md:538-540`). Ours answers INT3 to the retry — introduced
   2026-08-17 while fixing the endless-Init hang, and still wrong.
3. **`Pause` must fail INT5(stat,80h) during seek phases**, including the implicit
   seek of ReadN/ReadS/Play (`cdromdrive.md:586-588`). Not implemented.
4. **`GetlocL` must fail with 80h during the seek phase** (`cdromdrive.md:896-901`)
   and when playing audio tracks (`:892-895`). Today's latch answers unconditionally
   whenever a data sector has ever been seen.
5. **`Setloc` must validate BCD** (ass < 60h, asect < 75h) and answer INT5(stat,10h)
   otherwise (`cdromdrive.md:627-628`). Not implemented.
6. **`SeekL`/`SeekP` must stop any current or pending ReadN/ReadS**
   (`cdromdrive.md:635`, `:646`).
7. **Only one of the Play/Seek/Read stat bits may be set at a time**
   (`cdromdrive.md:862-867`), and Gran Turismo 1 depends on Read appearing only
   after the seek completes.
8. Items 2-7 are read off the documentation; each still needs the corresponding code
   line quoted before it is called a defect in our implementation. That is the next
   step, not a conclusion. **Done in Part 6** — every one of them is confirmed
   against a code line there (rows C4b, C8b, C22a, C10, C11a, C19).

---

## Part 5 — rules from the four remaining doc files

### 5.1 Subchannels (`cdromformat.md:1-465`)

| # | Doc | Rule |
|---|---|---|
| Q1 | :183-190 | ADR/Control byte: bit0-3 ADR, bit4 preemphasis, bit5 copy permitted, bit6 data, bit7 quad audio |
| Q2 | :219-227 | ADR=1 in the data region: track, index (**00h = pause**), track-relative MSF (**decreasing during pause**), reserved 00h, absolute MSF. Must be present in ≥9 of 10 sectors |
| Q3 | :229-236 | Lead-out: track **AAh**, index fixed 01h, relative MSF increasing from 00:00:00 |
| Q4 | :193-217 | Lead-in TOC: point 01h-99h = track start, A0h = first track + disc type (20h = CD-ROM-XA), A1h = last track, A2h = lead-out |
| Q5 | :240-272 | ADR=2 UPC/EAN and ADR=3 ISRC exist but are suppressed by GetlocP (readable only via test 19h,60h — `cdromdrive.md:1433-1441`) |
| Q6 | :274-308 | ADR=5 multisession lead-in (B0h/C0h/C1h) and lead-out (D1h) |
| Q7 | :452-462 | CRC-16-CCITT over the 10 SubQ bytes, inverted, stored big-endian |
| Q8 | :84-112 | Track/index/MSF are BCD; absolute MSF is what seeks use, local MSF is for display |
| Q9 | `cdrominternalinfoonpsxcdromcontroller.md:1596-1601` | The PSX checks only the low 2 bits of ADR, so ADR=5 is treated as ADR=1; during lead-in, bytes 7..9 are overwritten by bytes 3..5 |

### 5.2 Disc content and protection (`cdromformat.md:936-1831`)

| # | Doc | Rule |
|---|---|---|
| P1 | :995-1004 | Licence text in the system area (sectors 4-11); line 2 carries the region word — "…Euro pe", "…Amer ica", or the JP variant with none |
| P2 | :1006-1020 | The "PS" logo lives in sectors 5-11; PAL v4.0E+ and JP BIOSes refuse a modified copy |
| P3 | :1022-1077 | Primary Volume Descriptor at sector 16, terminator at 17, "CD-XA001" at offset 400h |
| P4 | :1442-1455 | SCEx is encoded in the disc wobble and verified **inside the drive firmware**, not by the BIOS |
| P5 | :1517-1521 | The secret unlock commands (50h-57h) are the documented software backdoor for that check |
| P6 | :1762-1815 | LibCrypt: ~100 PAL games store a 16-bit key as sectors with deliberately **wrong SubQ CRC**; the controller ignores those sectors, so **GetlocP keeps returning the previous sector's position**. Sector list at :1790-1806 |

### 5.3 Disc image formats (`cdromfileformats.md`)

| # | Doc | Rule |
|---|---|---|
| F1 | :14874-14905 | .BIN holds raw 930h-byte sectors **starting at 00:02:00**; the .CUE MSF values are logical, real address = CUE value + 2 s + any PREGAP |
| F2 | :14917-14927 | TRACK datatypes carry the sector size: AUDIO/MODE1/2352/MODE2/2352 = 930h, MODE1/2048 = 800h, MODE2/2336 = 920h |
| F3 | :14929-14933 | PREGAP/POSTGAP durations are **not stored in the BIN**; track 1 always has a 2-second pregap |
| F4 | :14903-14905 | The end of the last track is not in the .CUE — derive it from the BIN filesize |
| F5 | :14964-14977 | Malformed .CUEs with 1-digit TRACK/INDEX numbers exist in the wild |
| F6 | :14614-14747, :14981-15050, :12528 | CCD/IMG/SUB, CDI, MDS/MDF, NRG, CHD, PBP, ECM are the other image formats. Not required — we load .cue/.bin only |

### 5.4 Controller internals (`cdrominternalinfoonpsxcdromcontroller.md`)

| # | Doc | Rule |
|---|---|---|
| I1 | :1148-1162 | 32K SRAM map: 8 sector slots at 0C00h stride, then three 900h sound-map ADPCM buffers |
| I2 | :1173-1205 | Sub-CPU-side HIFSTS/CLRCTL/INTSTS — the other face of the host registers in Part 1 |
| I3 | :1228-1254 | ADPCI/RTCI coding-info bits, including ADPBUSY, and the same S/M-FS-BITLNGTH-EMPHASIS layout as the CI register and the XA subheader |
| I4 | :1483-1492 | SUBQ output carries a 15-bit unsigned peak plus an L/R flag; peak resets on each read, and report mode forwards SUBQ only every 10 frames, so 9 of 10 peaks are lost |
| I5 | :1855-1886 | Sled/track-jump mechanics. Explicitly: "emulators can just return increasing sectors without needing to handle special tracking commands", but the sled distance translation matters or the firmware times out |
| I6 | :1888-1895 | Focus/gain/balance can be stubbed as always-good — **except** that disc-missing (and ideally laser-off / spindle-stopped) failures should be emulated |
| I7 | :1758-1800 | New SCEx over the SPI bus on CXD2938Q; reachable only through test command 19h,50h/51h |

### 5.5 Video CD (`cdromvideocdsvcd.md`, 887 lines)

| # | Doc | Rule |
|---|---|---|
| V1 | whole file | VCD/SVCD ISO layout (INFO/ENTRIES/PSD/LOT/SCANDATA) and MPEG-1 multiplex/video/MP2 streams. Drive-relevant only through command 1Fh on the SCPH-5903 (`cdromdrive.md:1655-1720`), a model we do not emulate. `:1762-1764` notes SPUCNT.14 also mutes VCD audio |
| V2 | `cdromdrive.md:1722-1760` | The SCPH-5903 firmware is otherwise identical to vC1 — command 1Fh only toggles Port F, no reading/seeking behaviour changes |

---

## Part 6 — code audit, remaining files

Same rule as Part 3: each row names the doc line and the code line that were
compared. `OK` means both were read and they agree.

### 6.1 `src/cdrom/cdrom.c` — host registers, IRQ, events

| Item | Code | Doc | Verdict |
|---|---|---|---|
| Bank select bits 0-1, read back | `cdrom.c:282`, `:318` | R1 (`:61`, `:69-70`) | OK |
| HSTS PRMEMPT / PRMWRDY / RSLRRDY | `:283-285` | R1 (`:63-65`) | OK |
| HSTS DRQSTS | `:286-287` | R1 (`:66`) | OK |
| **HSTS ADPBUSY (bit2)** | `:279-289` — never set | R1 (`:62`), C26 (`:1020-1022`) | **MISSING.** Doc says the bit works as usual even while muted; Dino Crisis 1 mutes during modchip detection (`:1023`) |
| HSTS BUSYSTS (bit7) | `:288` (`pending_command != CDC_NONE`) | R1 (`:67`), `:280-297` | PLAUSIBLE — busy clears when the command event fires, not when the HC05 acks. Not measured |
| Parameter FIFO 16 bytes, PRMWRDY on full | `cdrom.h:24`, `cdrom.c:284`, `:344` | R2 (`:98-101`) | OK |
| Result FIFO 16 bytes | `cdrom.h:25` | R3 (`:135`) | OK |
| **Result FIFO read past the response** | `cdrom.c:293` — `fifo_pop` returns 0 and consumes | R3 (`:138-141`) — pad with 00h to 16 bytes, then **restart at the first response byte**, same 16 bytes until a new response | **DIVERGES** |
| **RDDATA read past the sector end** | `:297-299` — returns 0 | R5 (`:122-124`) — repeat the byte at \[800h-8\] / \[924h-4\] | **DIVERGES** |
| RDDATA 8-bit reads | `:296-300` | R5 (`:125-129`) | OK |
| HCHPCTL BFRD (bit7) | `:365-376` | R4 (`:108`) | OK |
| **HCHPCTL SMEN/BFWR (bits 5-6)** | `:365` — only bit7 tested | R4 (`:106-107`), sound map flow (`:348-372`) | **MISSING** |
| HINTSTS read, bits 5-7 = 1 | `:302-306` | R6 (`:143-149`) | OK for bits 0-2 |
| **HINTSTS BFEMPT/BFWRDY (bits 3-4)** | `:306` — never set | R6 (`:146-147`) | **MISSING** (sound map only) |
| HINTMSK write masked to 5 bits, read back with 0xE0 | `:345`, `:304` | R7 (`:174-177`) | OK |
| **IRQ condition** | `:44-46`, `:52`, `:61` → `bus_irq.c:46-62`, mask never consulted | R7 (`:179-181`) — IRQ fires whenever `(HINTMSK & HINTSTS) != 0` | **DIVERGES**: we raise IRQ2 for masked interrupts, and never re-raise when a mask is written back on |
| HCLRCTL CLRINT bits 0-4, CLRPRM bit6 | `:381-383` | R8 (`:185-189`) | OK |
| **HCLRCTL: result FIFO drained on acknowledge** | `:379-403` — not drained | R8 (`:195-197`) | **DIVERGES** |
| **HCLRCTL SMADPCLR (bit5), CHPRST (bit7)** | `:379-403` — ignored | R8 (`:188-190`, `:198-200`) | **MISSING** |
| Pending command released on acknowledge | `:399-401` | R8 (`:195-197`), T6 (`:1814-1824`) | OK |
| Deferred INT re-armed on the *remaining* deadline | `:74-78`, `:92-104`, `:114-118` | T5/T6 (`:1788-1830`) | OK — this is the 2026-08-17 fix |
| **ATV0 (L→L) at 1F801802 bank 2** | `:346` `vol_ll_t` | R10 (`:227`) | OK |
| **ATV1 (L→R) at 1F801803 bank 2** | `:405` `vol_lr_t` | R10 (`:228`) | **DIVERGES** — L→R is "right output from CD left", i.e. the field the header calls `vol_rl`. Swapped with ATV3 |
| **ATV2 (R→R) at 1F801801 bank 3** | `:338` `case 3: break;` | R10 (`:229`) | **MISSING** — the write is dropped entirely |
| **ATV3 (R→L) at 1F801802 bank 3** | `:347` `vol_rl_t` | R10 (`:230`) | **DIVERGES** — swapped with ATV1 |
| **ADPCTL at 1F801803 bank 3** | `:406-416` — stored into `vol_rr_t`, commit on bit5 | R11 (`:249-255`) — bit0 ADPMUTE, bit5 CHNGATV | **DIVERGES**: the commit-on-bit5 is right (CHNGATV), the rest is not. ADPMUTE is ignored, and the ADPCTL value itself is written into the R→R volume |
| **The whole ATV matrix is dead** | `vol_ll/lr/rl/rr` are written in `cdrom.c` and **read nowhere** in `src/` | R10 (`:227-247`) — incl. saturation up to double volume | **MISSING.** Spyro's mono/stereo option and Resident Evil 2's CD fades (`:243-247`) have no effect |
| WRDATA (sound map data) | `:336` `case 1: break;` | `:257-263` | **MISSING** |
| CI (sound map coding info) | `:337` `case 2: break;` | `:265-278` | **MISSING** |
| Shell open handling | `:263-269` — sets flags only | `:854-860` — INT5 with error 08h, all bits but shell-open/error cleared, regardless of any command | **MISSING** |
| DMA word read from the armed buffer | `:446-466` | `:315-320`, `:125-129` | OK |

### 6.2 `src/core/bus.c` — CDROM address decode

| Item | Code | Doc | Verdict |
|---|---|---|---|
| Byte reads/writes routed to `cdrom_read8/write8` | `bus.c:333`, `:346` | R1-R8 | OK |
| **16-bit read** | `bus.c:334-338` — reads `addr` **and `addr+1`**, i.e. two *different* registers | R5 (`:125-129`) — a 16-bit RDDATA read must return two consecutive **data** bytes ("1024 load halfword opcodes") | **DIVERGES** — a halfword read of 1F801802h returns RDDATA in the low byte and HINTSTS/HINTMSK in the high byte |
| **32-bit read** | `bus.c:339-343` — reads offsets 0,1,2,3 | R12 (`:315-320`) — returns **HSTS four times** (auto-increment off) | **DIVERGES**, and worse than cosmetic: our version pops the result FIFO and consumes a data byte as a side effect |
| 16/32-bit write | `bus.c:345-350` — low byte only, warns | not documented | UNVERIFIED |
| DMA channel 3 word source | `bus.c:1134` | `:315-320` | OK |

### 6.3 `src/cdrom/cdrom_commands.c` — commands

| # | Item | Code | Doc | Verdict |
|---|---|---|---|---|
| C0 | **Sync (00h) answers INT3(stat)** | `:183-186` | C1 (`:380`), `:501-504` | **DIVERGES** — must be INT5(11h,40h) |
| C1a | Unknown opcodes answer INT5(stat\|1, 40h) | `:589-592` | C1 (`:411-421`), C34 (`:1606-1646`) | OK in shape; the exact stat byte (11h vs our 03h) is UNVERIFIED |
| C1b | **17h/18h answer INT3** (`SetClock`/`GetClock`) | `:462-471` | C1 (`:403`) — unused, INT5(11h,40h) | **DIVERGES** |
| C1c | Parameter FIFO cleared after an unsupported command | `:595` | `:492-496`, `:1658` — hardware does **not** clear it, the params leak into the next command | DIVERGES (we are "too clean") |
| C2 | Setfilter stores file/channel, does not affect data reads | `:329-334` | C2 (`:506-513`) | OK |
| C3a | Setmode bits 7,6,5,3,2,1,0 decoded | `:337-359` | C3 (`:515-524`) | OK |
| C3b | **Setmode bit4 read as "2328-byte sector"** | `:847-849` | C3 (`:526-533`) — bit4 makes the controller **keep the previous size**, ignore the exact Setloc position (±0..3 sectors) and set stat.bit3 in INT1 | **DIVERGES** — this is the disproven older reading |
| C3c | Speed change costs a spin-up, charged to the next seek/read | `:344-346`, `:90-92`, `:139-141` | T3 (`:1907-1921`) | OK (magnitude calibrated, not documented) |
| C4a | Init sets mode=20h, motor on, standby | `:632-650` | C4 (`:535-537`) | OK |
| C4b | **A re-issued Init answers INT3** | `:288-289` runs before the `init_already_owed` test at `:297-310` | C4 (`:538-540`) — dropped silently, neither INT3 nor INT5 | **DIVERGES** (Part 4 item 2, now with the code line) |
| C4c | Init "abort all commands" | `:287-312` — no read/seek abort in the first response | C4 (`:536`) | MISSING |
| C5 | **Reset sends a second INT2** | `:553-554`, `:652-655` | C5 (`:542-551`) — INT3 only; software must wait 1/8 s, there is no completion INT | **DIVERGES** |
| C6 | **MotorOn never fails** | `:248-254` | C6 (`:556-563`) — INT5(stat,20h) if the motor was already on, INT5(stat,80h) with no disc, and it must not pause anything | **DIVERGES** |
| C7a | Stop: bit5 clear in the first response, bit1 clear in the second | `:257-266`, `:657-662` | C7 (`:577-578`) | OK |
| C7b | Stop moves the head to the start of track 1 | `:657-662` — position untouched | C7 (`:574-576`) | MISSING |
| C8a | **Pause: first response must still have bit5 set** | `:276-279` sets `DRIVE_PAUSING` **before** `cdrom_get_stat_byte` at `:279` | C8 (`:583-585`) | **DIVERGES** |
| C8b | **Pause during a seek phase must fail INT5(stat,80h)** | `:269-284` — no state test | C8 (`:586-588`) | **MISSING** (Part 4 item 3) |
| C9a | ADPCM delivery: XA enabled + audio + realtime + filter match | `:749-765` | C9 (`:595-601`) | OK |
| C9b | ADPCM delivery must reject CD-DA and non-MODE2 sectors first | `:747-765` — neither tested | C9 (`:596-597`) | MISSING |
| C9c | Data delivery must be rejected when the filter is on and the submode is audio+realtime | `:825` (the `else` branch takes every sector the ADPCM path refused) | C9 (`:604`) | **DIVERGES** — a filtered-out ADPCM sector still raises INT1 here |
| C9d | The documented two-attempt data-delivery bug | absent | C9 (`:605-612`) | MISSING (deliberate; harmless) |
| C10 | **Setloc does not validate BCD** | `:196-203` — `cdrom_from_bcd` on any byte, no range test | C10 (`:627-628`) — ass < 60h, asect < 75h, else INT5(stat,10h) | **MISSING** (Part 4 item 5) |
| C11a | SeekL/SeekP stop a running read | `:453-459` + `begin_seeking` `:125-148` set `DRIVE_SEEKING`, so the drive loop (`:714`) no longer matches | C11/C12 (`:635`, `:646`) | OK by construction (Part 4 item 6 resolved) |
| C11b | Landing window N-8..N-0 (1x) / N-5..N+2 (2x) | `:677-678` — lands exactly on N | C11 (`:634-635`) | DIVERGES (documented imprecision we do not model; no known dependency) |
| C11c | SeekL on an audio CD must fail the second response with (stat+4, 04h) | `:675-691` | C11 (`:636-639`) | MISSING |
| C13 | **SetSession (12h) is a stub** | `:412-424` returns INT2 with 4 invented bytes; no error paths | C13 (`:653-681`) — INT2(stat) only; session 00h → INT5(03h,10h); session ≥2 on a single-session disc → INT5(06h,40h) twice; error 80h during read/play | **DIVERGES** |
| C14 | ReadN/ReadS never refuse | `:240-245` | C14 (`:686-700`) — unlicensed disc → INT5(03h,40h) with no INT3/INT1; audio sectors need CDDA mode; region mismatch → 40h unless CDDA | MISSING (we have no lock state at all) |
| C15 | ReadN and ReadS both read sequentially from Setloc | `:240-245`, `:65-98` | C15 (`:749-756`) | OK |
| C17 | Read after Pause must re-deliver the most recently received sector | `:65-72` — resumes at `current_lba`, i.e. the *next* sector | C17 (`:811-814`) | DIVERGES |
| C18a | Error 80h with no disc, for the documented command list | `:168-178` — only Setloc/Play/ReadN/ReadS/SeekL/SeekP | C18 (`:850-852`) — 02h..09h, 0Bh..0Dh, 10h..16h, 1Ah, 1Bh, 1Dh | DIVERGES (partial) |
| C18b | Shell-open INT5 with error 08h | absent | C18 (`:847-848`, `:854-856`) | MISSING |
| C19 | Only one of Play/Seek/Read set at a time; Read only after the seek | `cdrom.c:29-31` (one `drive_state`), `cdrom_commands.c:684-690` | C19 (`:862-867`) | OK — Gran Turismo 1's requirement holds (Part 4 item 7 resolved) |
| C20 | **Nop does not reset the shell-open flag** | `:189-192` | C20 (`:869-876`) | **MISSING** |
| C21 | Getparam → stat, mode, 00h, file, channel | `:363-370` | C21 (`:878-880`) | OK |
| C22a | **GetlocL answers during the seek phase and on audio tracks** | `:377` — only `last_header_valid && motor_on` | C22 (`:892-901`) — 80h while seeking, 80h on audio CDs/tracks | **MISSING** (Part 4 item 4) |
| C22b | GetlocL reports the newest sector, not the one INT1 handed over | `:840-841` latch, answered at `:377-387` | C22 (`:887-891`), B3 (`:2001-2045`) | OK — and this latch is what unhung Monsters & Co. |
| C23 | GetlocP returns 8 BCD bytes in the documented order | `:397-409` | C23 (`:903-916`) | OK |
| C24a | GetTN returns BCD first/last | `:427-432` | C24 (`:920-924`) | OK |
| C24b | **GetTD treats its parameter as binary** | `:436-441` — `tnum` compared against `first_track`/`last_track` without `cdrom_from_bcd` | C24 (`:926-930`) — the parameter is BCD | **DIVERGES** — wrong track for every track ≥ 10 |
| C24c | GetTD out-of-range / non-BCD must answer error 10h | `:436-448` — answers 00:00 | C24 (`:929-930`) | MISSING |
| C24d | GetTD returns Index 1 rounded down to the second | `:444-448` (mm/ss only) | C24 (`:931-934`) | OK |
| C25a | GetID licensed Mode2 response shape | `:611-630` | C25 (`:981`, `:989-999`) | OK |
| C25b | The rest of the GetID matrix (door open, spin-up, detect busy, audio disc, unlicensed) | `:615-616` — only "no disc", and it sends 2 bytes | C25 (`:969-983`) — no-disc is INT5(08h,40h,00,00,00,00,00,00), 8 bytes | DIVERGES |
| C26 | Mute/Demute affect CD-DA and XA | `:315-326`, `cdrom_audio.c:289`, `:329` | C26 (`:1018-1022`) — sectors are still processed internally, only the output volume is forced to zero | DIVERGES — muted CDDA samples are never pushed (`cdrom_audio.c:329`), so the FIFO starves instead of carrying silence |
| C27a | Play starts at Setloc if pending, else the current location | `:100-123` | C27 (`:1032-1036`) | OK |
| C27b | Play track parameter N+1..99h must restart the **current** track | `:102-106` — clamps to the last track | C27 (`:1037`) | DIVERGES |
| C27c | End of disc: INT4 **and motor off** | `:906-913` — INT4 and `DRIVE_IDLE`, `motor_on` untouched | C27 (`:1038-1039`) | DIVERGES |
| C28 | **Forward/Backward never fail** | `:220-237` | C28 (`:1064-1066`) — INT5(stat+1,80h) unless already playing | **DIVERGES** |
| C29 | During Play only Setmode bits 7,2,1 apply | `:337-359` applies all | C29 (`:1068-1075`) | DIVERGES (low impact: our Play path ignores the others anyway) |
| C30 | **Report packet** | `:942-956` — 9 bytes, both relative and absolute, every sector, no peak | C30 (`:1077-1094`) — 8 bytes; absolute on asect 00/20/40/60h, in-track on 10/30/50/70h; last two bytes are the peak (I4) | **DIVERGES** |
| C31 | AutoPause INT4 at end of track / stop at end of disc | `:921-935`, `:906-913` | C31 (`:1097-1102`) | PLAUSIBLE — the track-change INT4 is there, but the end-of-track position rule (`:1103-1107`) and the motor stop are not, and `prev_track` is a function-level `static` (`:922`), so it is shared and not saved in savestates |
| C32 | XA sectors raise no INT1; sector-size bit is don't-care for them | `:812-824` | C32 (`:1132-1135`) | OK |
| C33a | Test 19h,20h version bytes | `:477-499` | C33 (`:1149-1169`) | OK (vC1, 16 May 1995) |
| C33b | Test 19h,22h region string follows the console region | `:511-529` | C33 (`:1180-1195`) | OK |
| C33c | Test 19h,04h/05h SCEx counters | `:500-510` | C33 (`:1211-1234`) — a licensed disc should report 01h,01h | DIVERGES: we always return 00h,00h, and 19h,05h must return (total,success) **without** a leading stat byte |
| C33d | **Unimplemented test subfunctions answer INT3(stat)** | `:530-533` | C33 (`:450`, `:462`, `:469`, `:476`, `:478`, `:485`) — INT5(11h,10h), or (11h,20h) with a nonzero parameter count | **DIVERGES** |
| C33e | Test 19h,21h drive switches; 19h,23h-25h chip strings; 19h,03h motor off | absent | `:1171-1178`, `:1197-1209`, `:1271-1272` | MISSING |
| C1d | GetQ (1Dh) | `:558-574` — stat + 10 SubQ bytes in the **first** response, parameters ignored | `:936-967`, `:1843` — INT3(stat) then **INT2**(10 raw SubQ bytes from the lead-in for the requested adr/point, plus peak_lo) | **DIVERGES** |
| C1e | ReadTOC (1Eh) | `:576-582`, `:699-702` | `:786-797`, T1 (`:1892-1894`) | PLAUSIBLE — INT3/INT2 shape is right, the TOC itself is never re-read (nothing depends on it while we report vC1) |
| C1f | VideoCD (1Fh) answers INT5 invalid command | `:585-587` | `:1655-1663` | OK for a non-SCPH-5903 machine |
| T1 | First-response delay is a flat 25000 cycles | `cdrom.h:35` | T1 (`:1882-1891`) — Nop averages 0xc4e1 (50401), Init 0x13cce (81102) | DIVERGES (inside the min/max band for Nop, but half the average, and Init is not special-cased) |
| T2a | Pause second response 1x/2x/idle | `cdrom.h:76-78` (2168860 / 1097107 / 7000) | T2 (`:1900-1902`) — 0x21181c / 0x10bd93 / 0x1df2 | OK (idle 7000 vs 7666, inside the measured band) |
| T2b | **Stop second response** | `cdrom.h:70-71` — 2048 idle, 6773760 spinning, speed-independent | T2 (`:1903-1905`) — 0xd38aca (13863114) at 1x, **0x18a6076 (25845878) at 2x**, 0x1d7b (7547) when stopped | **DIVERGES** — half the 1x value, and hardware is *slower* at 2x |
| T2c | GetID second response 20480 | `cdrom.h:37` | T2 (`:1899`) — 0x4a00 = 18944 | PLAUSIBLE (8% high) |
| T4 | INT1 rate | `cdrom.h:32-33` — CLK/75 = 451584 and 225792 | T4 (`:1929-1932`) — SystemClock*930h/4/44100 | OK — matches the exact formula (the doc's "average" 0x6e1cd is 0.2% lower and is explicitly an average) |
| T7 | One undelivered INT1, three usable slots | `cdrom.c:143-155`, `:396-397` | T7 (`:1799-1812`) | OK in effect: we never queue a second INT1 |
| B1 | **Eight slots, oldest and newest reachable** | `:856-857` — `current_read_buffer` is set to the slot just written, so the guest always gets the newest | B1-B3 (`:1939-1956`, `:2001-2045`) — INT1 delivers the **oldest**, and after a delay jumps to the newest | **DIVERGES** — the 8-entry ring in `cdrom.h:313` exists but is not a queue; sector loss on a slow guest is not modelled |
| B4 | The drive keeps filling the buffer during Pause | `:269-284` — the drive event stops at once | B4 (`:2047-2087`) | DIVERGES |

### 6.4 `src/cdrom/cdrom_disc.c` + `include/cdrom_disc.h` — disc layer

| Item | Code | Doc | Verdict |
|---|---|---|---|
| .BIN as raw 2352-byte sectors | `cdrom_disc.h:17`, `cdrom_disc.c:222-226` | F1 (`:14875-14877`) | OK |
| CUE MSF → LBA, and Setloc's `-150`, agree | `cdrom_disc.c:24-28` + `cdrom_commands.c:202` | F1 (`:14898-14902`) | OK — both are file-relative, so they cancel |
| **PREGAP lines ignored** | `cdrom_disc.c:132-140` — only INDEX is parsed | F3 (`:14929-14933`), F1 (`:14901-14902`) | **MISSING** — every track after a PREGAP is offset by the gap |
| **TRACK datatype other than AUDIO ignored** | `cdrom_disc.c:129` | F2 (`:14917-14927`) | MISSING — a MODE1/2048 or MODE2/2336 image would be read at the wrong stride |
| INDEX 00 stored but unused | `cdrom_disc.c:139`, `:156` | Q2 (`:222-223`) | MISSING (see index/pregap rows below) |
| Malformed 1-digit CUEs parse | `cdrom_disc.c:120`, `:135` (`sscanf %u`) | F5 (`:14964-14977`) | OK |
| Total sectors from the BIN filesize | `cdrom_disc.c:160-165` | F4 (`:14903-14905`) | OK |
| Region detection from the licence text | `cdrom_disc.c:236-249` | P1 (`:995-1004`) | OK |
| SubQ control/ADR byte (41h data, 01h audio) | `cdrom_disc.c:270` | Q1 (`:185-189`) | OK |
| Absolute MSF = LBA + 150, BCD | `cdrom_disc.c:272-279` | Q2 (`:225`), Q8 (`:92-104`) | OK |
| **SubQ index is always 01h** | `cdrom_disc.c:291` | Q2 (`:222`) — 00h during pause | **MISSING** |
| **No pregap countdown** | `cdrom_disc.c:282-288` — and `rel_lba` **underflows** when `lba < start_lba` | Q2 (`:223`) — relative MSF decreases during pause | **DIVERGES** (and the underflow yields nonsense BCD) |
| **No lead-out SubQ** | `cdrom_disc.c:255-294` — returns the last track | Q3 (`:229-236`) — track AAh, index 01h, relative MSF from 00:00:00 | **MISSING** (`cdrom_commands.c:900-914` tests the lead-out by LBA instead) |
| No lead-in TOC SubQ (points A0h/A1h/A2h) | absent | Q4 (`:193-217`) | MISSING — this is what GetQ would need |
| No SubQ CRC, no bad-CRC sectors | absent | Q7 (`:452-462`), P6 (`:1782-1784`) | MISSING — **LibCrypt titles cannot work**: they need GetlocP to repeat the previous position on the deliberately corrupt sectors |
| Track lookup assumes contiguous numbering | `cdrom_disc.c:211-217`, `:259-265` | Q8 (`:74`) — tracks are 01h..99h and need not start at 1 | PLAUSIBLE — breaks if a CUE skips a number |
| Seek time model | `cdrom_disc.c:309-331` | T3 (`:1907-1921`) — "still unknown, probably quite complicated"; I5 (`:1855-1886`) | UNVERIFIED by construction — calibration, and the doc says no reference exists |
| Async reader tags its buffer with the LBA | `cdrom_disc.c:398-424`, `cdrom_disc.h:58-64` | — | OK (host-side concern, no doc rule) |

### 6.5 `include/cdrom.h`, `include/cdrom_audio.h`

| Item | Code | Doc | Verdict |
|---|---|---|---|
| FIFO sizes 16/16, 8 sector buffers | `cdrom.h:24-26` | R2 (`:98-101`), R3 (`:135`), B1 (`:1939-1941`), I1 (`:1148-1157`) | OK as declarations; the ring's *behaviour* is row B1 above |
| Stat bit names | `cdrom.h:184-191` | `:823-830` | OK |
| Error code names | `cdrom.h:194-197` | C18 (`:838-848`) | OK — 04h (seek failed) and 08h (door opened) have no constant; both are unimplemented paths |
| Interrupt enum INT1-INT5 | `cdrom.h:108-115` | R6 (`:154-162`) | OK |
| Command enum | `cdrom.h:121-155` | C1 (`:378-421`) | OK, except 17h/18h are named SetClock/GetClock — hardware has no such commands (row C1b) |
| XA state carries prev1/prev2 + both zigzag rings | `cdrom_audio.h:35-45` | S12-S15 | OK (see Part 3) |

---

## Part 7 — findings, ordered by what they would change

Everything here cites both sides; the row in Part 6 has the exact lines.

1. **The CD audio volume matrix does nothing.** ATV0-ATV3 are stored into
   `vol_ll/vol_lr/vol_rl/vol_rr` and never read by any mixer (`cdrom.c:346`,
   `:347`, `:405`, `:407`; R10 `:227-247`). On top of that ATV2 is dropped
   (`cdrom.c:338`), ATV1/ATV3 are swapped, and 1F801803h bank 3 is ADPCTL, not a
   volume register (R11 `:249-255`) — so a game applying its volumes writes the
   ADPCTL value into the R→R gain. Any game that mixes CD audio to mono, fades it,
   or pans it is unaffected by its own writes. Fix all five together.
2. **16-bit and 32-bit CDROM reads hit the wrong registers** (`bus.c:334-343`;
   R5 `:125-129`, R12 `:315-320`). A halfword RDDATA read — which the doc names as
   a normal way to read a sector — returns one data byte plus an interrupt
   register, and a word read of 1F801800h pops the result FIFO and eats a data
   byte instead of returning HSTS four times.
3. **GetlocL and Pause have no seek-phase failure, Setloc has no BCD check**
   (`cdrom_commands.c:377`, `:269-284`, `:196-203`; `:892-901`, `:586-588`,
   `:627-628`). These are the three rules the newer psx-spx text added, and the
   `GetlocL`/`Setloc`/`SeekL`/`ReadS` churn measured against DuckStation (13x
   their GetlocL, 21x Setloc, 27x SeekL per field) is exactly the shape of a guest
   retrying a request that should have been refused with 80h.
4. **A re-issued Init still answers INT3** (`cdrom_commands.c:288-289` before the
   guard at `:297`; `:538-540`). The endless-Init hang is gone, but the retry must
   produce no response at all.
5. **Reset sends an INT2 that hardware never sends** (`cdrom_commands.c:553`,
   `:652-655`; `:542-551`), and **Sync/17h/18h answer INT3 where hardware answers
   INT5(11h,40h)** (`:183-186`, `:462-471`; `:380`, `:403`, `:501-504`).
6. **The sector buffer is not a buffer** (`cdrom_commands.c:856-857`;
   `:1939-1956`, `:2001-2045`). Eight slots exist in the struct, but INT1 always
   hands over the newest sector; the documented oldest-then-jump-to-newest
   behaviour and silent sector loss are not modelled.
7. **GetTD reads its parameter as binary instead of BCD**
   (`cdrom_commands.c:436-441`; `:926-930`). Every track from 10 upwards resolves
   to the wrong track — a mixed-mode audio disc lands in the wrong song.
8. **SubQ has no index 00h, no pregap countdown, no lead-out (AAh), and
   underflows in a pregap** (`cdrom_disc.c:282-291`; `:219-236`), and **PREGAP
   lines in the CUE are ignored** (`cdrom_disc.c:132-140`; `:14929-14933`).
   Together these break CD-DA track boundaries and the AutoPause caution at
   `:1108-1115` word for word.
9. **The report packet is wrong** (`cdrom_commands.c:942-956`; `:1077-1094`):
   nine bytes instead of eight, both time bases at once instead of alternating on
   asect, no peak level, and on every sector.
10. **SetSession, GetQ, MotorOn, Forward/Backward and the GetID matrix are stubs
    or unconditional successes** (`:412-424`, `:558-574`, `:248-254`, `:220-237`,
    `:611-630`; `:653-681`, `:936-967`, `:556-563`, `:1052-1066`, `:969-999`).
11. **Setmode bit4 is implemented as a sector size** (`cdrom_commands.c:847-849`;
    `:526-533`), which the documentation explicitly says is not what it does.
12. **Stop's second-response timing is half the measured 1x value and ignores
    speed** (`cdrom.h:70-71`; `:1903-1905`), where hardware is nearly twice as slow
    at 2x. The first-response delay is likewise a flat 25000 against a 50401
    average and a distinct 81102 for Init (`cdrom.h:35`; `:1882-1891`).
13. **The IRQ line ignores HINTMSK** (`cdrom.c:44-46` → `bus_irq.c:46-62`;
    `:179-181`) — masked interrupts still raise IRQ2, and unmasking never raises
    one that was already pending.
14. **Sound map (SMEN/BFWR/WRDATA/CI/BFEMPT/BFWRDY/SMADPCLR) is entirely absent**
    (`cdrom.c:336-337`, `:365`, `:379-403`; `:103-112`, `:143-149`, `:257-278`,
    `:348-372`). No commercial game is known to need it; it is the documented way
    to test XA decoding from RAM.
15. **LibCrypt cannot work** (no SubQ CRC model at all; P6 `:1762-1815`). ~100 PAL
    titles — MediEvil, CTR, Dino Crisis, Ape Escape — need GetlocP to repeat the
    previous position on sectors whose SubQ CRC is deliberately wrong.
16. **The two XA-resampler problems from Part 3 stand** (18900 Hz tap order and
    table provenance, `cdrom_audio.c:205` vs `:213`, `:167`).

Not defects, recorded so they are not re-investigated: the INT1 rate matches the
documented formula exactly; Pause's second-response timings match the measured
values; only one of the Play/Seek/Read stat bits can be set, and Read is not set
until the seek finishes, so Gran Turismo 1's check holds; the GetlocL latch is
correct and is what let Monsters & Co. start a new game; SeekL/SeekP do stop a
running read by construction; sector data offsets and the XA subheader/coding-info
decode agree with the sector-layout rules.

---

## Part 8 — what is UNVERIFIED, and why

- **Seek timing** (`cdrom_disc.c:309-331`). The documentation states outright that
  seek timings are unknown and depend on a firmware distance table (`:1909-1921`).
  Our two-slope model is calibration against observed behaviour; it can be
  compared against a reference run, never against a document.
- **BUSYSTS semantics** (`cdrom.c:288`). Doc `:280-297` describes hardware
  behaviour when a command is sent during the busy phase as "unpredictable"; we
  drop the command with a warning (`cdrom.c:325-328`). Neither can be called
  correct without a hardware trace.
- **The exact stat byte in INT5(11h,40h)** for unused opcodes. 11h implies the
  shell-open flag is set; our stat is derived from live state (`cdrom.c:25-33`).
- **Test 19h,51h/60h/71h-76h** (decoder and HC05 RAM access, `:1362-1441`) and the
  prototype/debug subfunctions (`:1329-1358`). Unimplemented, and no game is known
  to use them; the memory map at `:1443-1561` would be the reference if one did.
- **First/second-response jitter.** The doc gives min/avg/max bands
  (`:1877-1932`); we schedule fixed values. Whether any title depends on the
  spread is untested.
- **18900 Hz XA tables** (Part 3, `cdrom_audio.c:167`) — psx-spx publishes tables
  for 37800 Hz only (`:930-935`), so this cannot be settled from the docs at all.
- **Whether a seek resets the ADPCM `old`/`older` history** (Part 3, S12
  `:849-854`) — the documentation leaves it open.
- **`cdromfileformats.md` beyond the image-format chapters** (~16000 lines of PSX
  file formats: TIM, EXE, .STR, archives). Read for drive-relevant content only;
  it belongs to the MDEC/GPU/BIOS areas, not to the drive, and nothing in it
  constrains `src/cdrom/`.
