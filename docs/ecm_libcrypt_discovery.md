# ECM support, and the four defects behind the Crash Bandicoot 3 disc error

2026-08-19. Supersedes the earlier version of this file, which concluded that the failure was
Sony's LibCrypt copy protection and that the emulator needed `.sbi` support. **That conclusion was
wrong.** It is kept here only as the worked example of how it was disproved, because the reasoning
that produced it was plausible and will be tempting again.

The disc used throughout is `Crash Bandicoot 3 - Warped (E) [SCES-01420]`, supplied as a
`.bin.ecm`, run against the PAL BIOS `SCPH-7502`.

---

## 1. The symptom

The BIOS booted, found `SYSTEM.CNF`, loaded `SCES_014.20` and reached `Execute !`. About five
emulated seconds later the game printed `rfs` and then looped forever:

```
DiskError: com=CdlSetloc,code=(03:10)
[CDROM] Setloc rejected: df:e7:d7 not valid BCD msf
CdRead: sector error
DiskError: com=CdlPause,code=(23:80)
```

`df:e7:d7` is not valid packed BCD, so the drive answers `INT5(stat|01h, 10h)` — which is correct
behaviour (`psx-spx-docs/docs/cdromdrive.md:627-628`). The question was never whether the refusal
was right. It was where the garbage came from.

## 2. Why LibCrypt was the wrong answer

LibCrypt stores a 16-bit key as deliberately corrupted position data in **Subchannel Q**. A
protected game reads that channel, reconstructs the key, and XOR-decrypts a table of disc
positions with it. A `.bin` or `.bin.ecm` carries no subchannel data, so without an `.sbi` the key
comes out wrong and the decrypted positions are garbage. That fits the symptom exactly, which is
why it was believed.

Three measurements killed it:

1. **The game never reads the subchannel.** Over a 34 s run the commands issued were
   `Pause` 806, `Setloc` 317, `ReadN` 309, `Setmode` 9, `SeekL` 8, `Getstat` 8, `Init` 4,
   `GetID` 3, `Test` 1, `Demute` 1. No `GetlocP` (11h), no `SeekP` (16h), no `GetQ` (1Dh), no
   `ReadTOC` (1Eh). A protection that reads SubQ cannot have run.
2. **The boot executable contains no subchannel code.** `SCES_014.20` was extracted from the
   image and scanned for immediate loads of those command numbers — the form `CdControl` calls take.
   `Setloc` appears at 39 sites, `Play` at 12, `ReadN` at 5, `GetlocL` at 2. `GetlocP`, `SeekP`,
   `GetQ` and `ReadTOC` appear at **zero**.
3. **`df e7 d7` is not on the disc.** The three bytes occur exactly once in the whole 344,713,824-byte
   image, at sector 88505 — a sector the game never reads. The value is computed in RAM, not read
   from an encrypted table.

An `.sbi` loader would have changed nothing here. It remains a legitimate feature for genuinely
protected titles; it was not this.

## 3. The ECM decoder was correct from the start

Two independent checks, both worth repeating before ever suspecting the decoder again.

**The container carries its own proof.** Corlett's ECM format appends the EDC — CRC32 with
polynomial `0xD8018001` — of the *entire decoded output* as four little-endian bytes at the end of
the file. Decoding all 146,562 sectors and running that CRC over the concatenated 2352-byte sectors
gives `6264BE2A`, and the file's trailing word is `6264BE2A`. The reconstruction is byte-identical
to the original `.bin`, MSF, EDC and ECC included.

**The A/B is decisive.** The image was decoded to a plain `.bin` and run again. It failed
identically, field for field — `rfs` at f1078, the first `DiskError` at f1083. Whatever was wrong
had nothing to do with ECM.

One real defect was found in the decoder along the way: `ecm_edc_init()` was never called, so the
three lookup tables stayed zero and every regenerated EDC/ECC came out as zeros. Latent for this
title, which reads `whole=1` sectors but never inspects their error codes — but the image was not
bit-exact until it was fixed, and the whole-file EDC check above only passes with the fix in.

## 4. Where it actually broke

With the data path exonerated, the flow was diffed against a DuckStation **Devel** build on the
same image. The two agree completely: same commands, same order, same sectors.

```
Setloc 00:02:16 · Setmode 0xA0 · ReadN   -> sector 16 (the ISO PVD)
  DMA 3 words to 0x0005F3DC   (header + subheader, whole-sector mode)
  DMA 512 words to 0x0006EC30 (2048 bytes of user data)
Pause · TTY "rfs"
Setloc 00:02:18 · ReadN                  -> sector 18 (the type-L path table)
  same two DMAs
Pause
  DuckStation: Setloc 00:02:24   <- the S0 directory, image LBA 24
  ours:        Setloc df:e7:d7   <- refused
```

The data reaching the guest is right: an XOR checksum over all 2048 bytes in RAM at `0x8006EC30`
matches the sector on disc, for both reads. So the game received identical input and computed a
different answer, which puts the defect in the CPU.

`SCES_014.20` was disassembled around the failure. The routine at `0x800324d8` walks the ISO path
table, keeps the extent LBA of every directory named `S<digit>` in a stack array, and later seeks to
each entry that is not the `-1` sentinel. The extent is a 4-byte field at **offset 2** of each
record — never word-aligned — so the compiler reads it with an unaligned pair:

```asm
80032504  lwl $t0,5($s0)
80032508  lwr $t0,2($s0)
```

Logging `$t0` after the pair, per record:

| record | expected | got |
|---|---|---|
| root | `00000016` | `bfc00016` |
| DRAGON | `000181f0` | `000181f0` |
| S0 | `00000018` | `bfc00018` |
| S1 | `00000019` | `00000019` |
| S2 | `0000001a` | `bfc0001a` |
| S3 | `0000001b` | `0000001b` |

The top 16 bits kept the stale contents of `$t0`. `op_lwl`/`op_lwr` merged against
`cpu->load_reg_idx` — the slot for the load **this** instruction issues, which
`cpu_retire_load_delay()` has just cleared — instead of `cpu->delay_load_reg`, the load the
*previous* instruction issued (`include/cpu.h:131-132`). psx-spx is explicit that the pair must
work: *"There's no delay required between lwl and lwr, so you can use them directly"*
(`psx-spx-docs/docs/cpuspecifications.md:247-252`), with an example that is exactly this pair.

Records alternate alignment because their lengths differ (root 10 bytes, `DRAGON` 14, the rest 10),
so half the pairs land as `(lwl+3, lwr+0)` — where each instruction writes the whole register and
the missing merge cannot be seen — and half as `(lwl+1, lwr+2)`, where it can. The corrupted
extents are not equal to `-1`, so the game did not skip them; it passed one to `CdIntToPos`, which
turned a negative LBA into a non-BCD MSF.

**This is why the bug survived for months.** It is data-dependent, not code-dependent: the BIOS,
Ace Combat 2 and Monsters & Co. never hit a partial-merge pair on their boot paths. Every unaligned
32-bit access in every game was affected.

## 5. Two more defects found on the way

Neither caused the disc error; both were real.

- **`make` never built anything.** `compile_commands` is defined before `all` in the Makefile, and
  make takes the first real target as the default goal. A bare `make` regenerated
  `compile_commands.json` and left the binary at whatever an earlier explicit `make all` produced —
  so a source edit appeared to have no effect because it was never compiled. Fixed with
  `.DEFAULT_GOAL := all`.
- **The no-disc BIOS shell never finished its init.** Bit 4 of the status byte is a latch:
  *"Once shell open (0=Closed, 1=Is/was Open)"* (`psx-spx-docs/docs/cdromdrive.md:826`). With no
  disc the shell has necessarily been open, and a reference run answers every `Getstat` with `10h`.
  `cdrom_reset()` forced it to `false`, so the BIOS believed a disc might be present, issued
  `GetID`, took `INT5(08h,40h)` and looped `Getstat`/`Getstat`/`GetID` forever.

The missing menu cursor turned out to be a third, separate thing — the pad booting in analog mode
rather than digital — recorded in the changelog rather than here.

## 6. What is still open on ECM

- A CUE with several `FILE` entries, one ECM per track, is implemented but untested; only the bare
  `.bin.ecm` path has been exercised.
- The sector lookup table is a flat `calloc` of `ECM_MAX_SECTORS` entries — 1.4 MB allocated
  regardless of disc size, and a hard cap at 80 minutes.
- Decoding is synchronous inside `cdrom_disc_read_sector`, so it runs on the async reader thread
  like any other image read. That is fine today because a decode is ~12 µs, but it has not been
  measured under seek pressure.
