Look up PSX hardware documentation in the DOCS/ folder of this project.

DOCS/ is a local, git-ignored clone of psx-spx (the nocash specification) — it is not part of the
repository, because it carries no licence that would let it be redistributed. If it is missing:

```sh
git clone https://github.com/psx-spx/psx-spx.github.io
ln -s psx-spx.github.io/docs DOCS
```

Key files:

| Topic | File |
|-------|------|
| SPU / audio | `DOCS/soundprocessingunitspu.md` |
| GPU | `DOCS/graphicsprocessingunitgpu.md` |
| CPU / MIPS R3000A | `DOCS/cpuspecifications.md` |
| GTE | `DOCS/geometrytransformationenginegte.md` |
| DMA channels | `DOCS/dmachannels.md` |
| Interrupts / IRQ | `DOCS/interrupts.md` |
| Memory map | `DOCS/memorymap.md` |
| I/O map | `DOCS/iomap.md` |
| CDROM controller | `DOCS/cdromdrive.md` |
| CDROM internal info | `DOCS/cdrominternalinfoonpsxcdromcontroller.md` |
| CDROM file formats | `DOCS/cdromfileformats.md` |
| CDROM format | `DOCS/cdromformat.md` |
| Controllers / memory cards | `DOCS/controllersandmemorycards.md` |
| SIO / serial | `DOCS/serialinterfacessio.md` |
| Timers | `DOCS/timers.md` |
| MDEC | `DOCS/macroblockdecodermdec.md` |
| BIOS / kernel | `DOCS/kernelbios.md` |
| Memory control | `DOCS/memorycontrol.md` |
| Hardware numbers | `DOCS/hardwarenumbers.md` |
| Expansion port / PIO | `DOCS/expansionportpio.md` |
| Unpredictable things | `DOCS/unpredictablethings.md` |

## Usage

The user's query is: $ARGUMENTS

Search the relevant DOCS/ file(s) for the requested topic. Use grep or Read to locate the exact section. Quote relevant register tables, bit fields, timing values, or formulas verbatim from the docs. If multiple files are relevant, check all of them.

If $ARGUMENTS is empty, list the available topics above and ask what to look up.
