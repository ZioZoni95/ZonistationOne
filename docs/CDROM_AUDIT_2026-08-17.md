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

### Still to audit

`src/cdrom/cdrom.c` (466), `src/cdrom/cdrom_commands.c` (968),
`src/cdrom/cdrom_disc.c` (424), `include/cdrom.h` (407), `include/cdrom_disc.h`,
`include/cdrom_audio.h`. Doc files still to read: `cdromformat.md:1000-1831`
(ISO descriptors, SCEx, LibCrypt), `cdrominternalinfoonpsxcdromcontroller.md`
(2141), `cdromvideocdsvcd.md` (887), `cdromfileformats.md` (16373).

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
   step, not a conclusion.
