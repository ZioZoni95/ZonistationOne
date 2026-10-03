# Archived probes

One-off Lua probes from investigations that are finished: the PlayStation boot logo (`ps_logo_*`),
FMV and MDEC behaviour, DMA and DICR ordering, VSync, the display window. They are kept because the
`emu.*` surface they use still exists and they show how each defect was isolated, but nothing
documents or depends on them any more.

Run one from the repository root as before:

```sh
ZS1_LUA_SCRIPT=scripts/archive/<name>.lua ./ZoniStation_One roms/bios-pal.bin --game="games/game.bin"
```

Some of them expect a particular game, savestate or frame count and say so in their first lines.
The probes that are still in use stay in `scripts/`.
