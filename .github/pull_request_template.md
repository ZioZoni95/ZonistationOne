## Summary

<!-- What changes and why, in a few lines. Link the issue it closes: "Closes #N". -->

## Kind of change

- [ ] Hardware accuracy (emulated behaviour changes)
- [ ] Bug fix (crash, hang, wrong output)
- [ ] Host side (renderer, audio device, UI, build, cluster/deploy)
- [ ] Tests or tooling only
- [ ] Documentation only

## Evidence for the behaviour

<!-- Every hardware change cites its source, so it can be checked later:
     - the psx-spx line (DOCS/ or psx-spx-docs/, file and line), or
     - a measurement: which game, which BIOS, which scenario, what you counted. -->

-

## Checks

- [ ] `make` builds with no new warnings from this project's own sources
- [ ] `make test` passes
- [ ] `make hwtest` passes (needed when the GPU, MDEC, DMA or SPU changed)
- [ ] Ran a real game when the change touches timing, the drive, audio or the renderer (name it below)

Game, BIOS and region used:

GPU and driver line from `logs/System.log` (`GL driver in use: ...`), if graphics are involved:

## Licensing

- [ ] No code copied or restructured from `duckstation_ref/` (CC-BY-NC-ND-4.0). Describing its behaviour in a comment is fine
- [ ] Anything taken from `pcsx-redux/` keeps its attribution header
- [ ] New source files carry an SPDX header (GPL-3.0-or-later) and `THIRD-PARTY.md` is updated if a dependency was added
- [ ] No BIOS image, disc image, memory card or savestate is committed

## Compatibility notes

- [ ] Savestate layout changed, and `ZS1_STATE_VERSION` in `savestate.c` was bumped and listed
- [ ] New or changed `ZS1_*` environment variable documented in `README.md` and `CLAUDE.md`
- [ ] `CHANGELOG.md` entry added

## Not verified

<!-- What you could not run or hear, and what is still open. Say it here instead of leaving it out. -->

-
