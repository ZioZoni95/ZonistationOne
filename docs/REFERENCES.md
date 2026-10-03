# Reference checkouts

Two other emulators are kept next to this repository as **local, untracked checkouts**. They are
consulted for what the hardware does when the documentation is ambiguous, and are never linked,
built into the emulator, or copied from. This repository records only where to get them and which
commit was used; it contains none of their content.

| Directory | Project | Licence | Commit used |
|---|---|---|---|
| `pcsx-redux/` | [grumpycoders/pcsx-redux](https://github.com/grumpycoders/pcsx-redux) | GPL-2.0-or-later | `3e10093ad9cc2fb445668928b3d0f87abeccd29c` |
| `duckstation_ref/` | [stenzek/duckstation](https://github.com/stenzek/duckstation) | CC-BY-NC-ND-4.0 | `e39033c4480cfbb9106e32beb844b0649ad9c2db` (`0.1-11599-ge39033c44`) |

Neither is needed to build or run the emulator. Both are in `.gitignore`.

```sh
git clone https://github.com/grumpycoders/pcsx-redux.git pcsx-redux
git -C pcsx-redux checkout 3e10093ad9cc2fb445668928b3d0f87abeccd29c

git clone https://github.com/stenzek/duckstation.git duckstation_ref   # over 1 GB with a build
git -C duckstation_ref checkout e39033c4480cfbb9106e32beb844b0649ad9c2db
```

The hardware specification is a third reference, cloned the same way
(`psx-spx`, see the end of `README.md`).

## Licence rules

- **DuckStation is CC-BY-NC-ND-4.0** since 2024-09-01: no derivative works, no commercial use,
  incompatible with the GPL. **No code from `duckstation_ref/` may be copied or restructured into this
  project.** Describing a behaviour in a comment is fine.
- **PCSX-Redux is GPL-2.0-or-later**, so code from it can be used, with its attribution header kept
  intact. Do not strip those headers.
- Prefer `DOCS/` (psx-spx) over both. An implementation from a cited line carries no third-party
  copyright, and the citation is what makes that checkable.

`THIRD-PARTY.md` is the full inventory.

## Comparing against DuckStation

To compare a run field by field, use a Devel build, because a release build compiles its DMA, GPU and
SPU log channels out:

```sh
cmake -S duckstation_ref -B duckstation_ref/build-devel -DCMAKE_BUILD_TYPE=Devel
cmake --build duckstation_ref/build-devel -j"$(nproc)"
```

It then emits about 2.3M lines a minute. Count its `Now in v-blank` lines to put its log on the same
axis as ours (`[f1397 t 27.4809]`: the CRTC field count and emulated seconds). Compare in emulated
fields, never in wall-clock time.
