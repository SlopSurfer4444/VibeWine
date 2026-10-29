# Staging initial-data copy regression

Run from this checkout with Python 3 and clang++:

```sh
python3 tests/vibewine/staging-initial-data/run.py --arch arm64
python3 tests/vibewine/staging-initial-data/run.py --arch x86_64
```

The x86_64 binary runs through Rosetta on an arm64 Mac. Each run uses ASan and
UBSan, compiles only the fixture, and removes its temporary source/binaries on
exit. No Wine, renderer build, Metal device, game, runtime or Git index changes.
The default baseline is `e3845197978e3fde46a07b584073ba499fd0b25b`;
`--source-dir` and `--baseline-ref` permit an explicitly bound alternate source.
The runner reports hashes of the extracted source files, fixture and runner.

The fixture extracts and executes the complete production
`CreateStagingTextureInternal` template, all three relevant `GetMipmapSize`
and `GetLinearTextureLayout` specializations, and all three relevant production
subresource enumerators. The production generator header is included directly.
Metal format lookup/alignment, COM resource publication, staging allocation and
descriptor validation are CPU adapters. `memcpy` is counted and still executes
libc memcpy under ASan. No replacement copy helper implements the fix.

The expected image is independently built from format block geometry, unique
subresource/slice/row/byte payloads and a 0xcd destination sentinel. Source
allocations end at the last logical byte, including padded rows and slices;
ASan detects reads past that bound. Expected destination padding and 32-byte
prefix/suffix guards must remain untouched. Source payload/padding must remain
unchanged.

Coverage includes:

- 1D widths 1/3/13, R8/R32/RGBA8, zero and poisoned nonzero input row pitch,
  mips/arrays, and row lengths exactly at or above the 256-byte adapter alignment.
- Tight/padded 2D rows and arrays/mips; padded 3D rows and independent slice pitch.
- BC1/BC3 odd dimensions and mips smaller than one block, with block-row counts.
- Existing undersized 2D/3D row-pitch truncation behavior, null initial data, and
  failures of either actual layout query before allocation/publication/copy.

The current candidate must pass 46 cases / 147 subresources. The historical
1D-only baseline must fail its 60 payload checks naturally (27 cases), without a
sanitizer failure. The full historical baseline also demonstrates an ASan source
overread in padded 2D initialization, after the 1D failures. A deliberately
incorrect aligned-length repair must fail with an ASan heap-buffer-overflow on
the tight 1D input. These expected negative runs are part of a passing runner.

## Evidence boundary

This verifies CPU copy behavior with production layout/copy bodies. It does not
verify a real Metal alignment/format query, allocation failure, descriptor
normalization (`MipLevels=0`), the D3D11 creation/map API, GPU readback, installed
behavior, game compatibility or FPS. BC controls exercise the supported layout
branches; they do not establish API acceptance of every descriptor combination.

A separate real API acceptance remains: create supported 1D staging textures
with `BindFlags=0`, `CPU_ACCESS_READ`, nonuniform initial data, zero/ignored
input pitches, arrays and mips; `Map(READ)` and compare each logical byte, with
matched 2D/3D/BC controls. Renderer compilation/integration and that API/GPU
acceptance belong to the campaign owner.

## Artifact lifecycle

Owner: the F1 staging branch, consumed by the frontier-wave integration owner.
The runner creates only private `dxmt-staging-initial-data-*` temporary source
and binaries, automatically removed by `TemporaryDirectory` on normal exit.
Unexpectedly retained scratch is a manual owner cleanup, never a general purge.
Console output is bounded by this fixed case matrix; no continuous producer is
installed. Campaign logs live under `outputs/frontier-wave-20261028/staging/`
with a 7-day TTL / 32 MiB cap; result/patch evidence has a 30-day TTL / 8 MiB cap.
Cleanup excludes active/held evidence and uses the campaign owner's existing
retention process; these instructions do not install a cleaner.
