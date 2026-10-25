# Actual production present-shader image oracle

The integration owner schedules execution under the existing exclusive GPU-lab
policy. This builder only compiles; it never invokes a GPU program, Wine, a game
or a window and never installs a runtime.

```sh
python3 tests/vibewine/capture-validity/gpu/build.py --output /absolute/new/directory
# To reuse a production library with separately verified provenance:
python3 tests/vibewine/capture-validity/gpu/build.py \
  --output /absolute/new/directory --library /absolute/dxmt_command.metallib
```

The default compiles the complete current production `dxmt_command.metal` and its
actual constants header into a real metallib. The builder snapshots both sources,
fixture and compiler command lines, records SHA-256 identities, and compiles the
native Objective-C++ program with warnings as errors. `--library` copies the
caller-selected existing library and records its identity. Its provenance must be
admitted by the integration owner; the current source snapshot alone cannot prove
that an externally supplied library was built from that snapshot.

Copy only the admitted `fixture`, `dxmt_command.metallib` and matching build
receipt into the existing authorized GPU-lab, then run sequentially with an
independent 30-second process-group deadline and 16 KiB caps on each output stream:

```sh
/absolute/fixture /absolute/dxmt_command.metallib
/absolute/fixture /absolute/dxmt_command.metallib --wrong-golden
```

The native program creates real private 4x3 RGBA8Unorm textures. A fixture-only
fill shader renders every source sample opaque red in the left two columns and
opaque green in the right two. Present uses the actual library's
`vs_present_quad` and `fs_present_quad`, specializing actual constants
`0x100..0x105` for matched-size, HDR PQ, HDR metadata, sRGB, multisampling and gamma.
Only matched-size and multisampling vary. Metadata is `{1,1000,1000}`. It records
native render passes, readback blits and actual completed command-buffer status,
then verifies every output pixel including alpha. The six cases are:

| Sampling | Drawable | Viewport | Expected image |
| --- | --- | --- | --- |
| Single/MSAA4, matched | 4x3 | 0,0,4,3 | red2, green2 |
| Single/MSAA4, UV path | 8x3 | 2,0,4,3 | black2, red2, green2, black2 |
| Single/MSAA4, UV path and 2x scale | 12x6 | 2,0,8,6 | black2, red4, green4, black2 |

Each row expands to both single-sample and MSAA4. Every output row has the same
golden. The offset case catches the previous MSAA absolute-raster-coordinate
error even though the source and viewport dimensions agree. Matched cases exercise
the direct-read shader specialization. Pixel values are exact 8-bit RGBA.

Acceptance requires normal natural exit 0, six passing case records with
`gpuCompleted:true`, and the passing six-case summary. The changed-golden run must
naturally exit 20 with a failed pixel record after real GPU completion/readback.
Its only change is the expected first content pixel (blue instead of red); shader,
encoding and actual image are unchanged. Unsupported device/MSAA4 or argument
errors return 2, native API/GPU errors return 30, and the in-process 25-second
watchdog exits 124. Timeouts, truncated output or any other result are
inconclusive, not negative-control success.

This verifies offscreen native production shader pixels. It does not create a
CAMetalDrawable, execute the production presenter or Wine bridge, start a GPU
trace capture, show a GUI, or establish displayed FPS, gameplay, resize handling
or frame delivery. The integration owner retains responsibility for those gates.

Owner: this source directory for fixture code; integration owner for generated
builds and runs. Scope: one caller-selected new build directory and the owner's
explicit admitted lab copy target. Data class: source snapshots, AIR/metallib,
module cache, executable, bounded compiler logs, receipts and fixed numeric pixel
records. TTL: acceptance plus seven days under the campaign policy; unresolved
failures, active execution and pinned recovery evidence are held. Caps: 64 MiB per
build/run bundle (builder rejects a larger finished build), 16 MiB per generated
binary/library/AIR, 512 KiB per source, 64 KiB per compiler log, 32 KiB build receipt,
16 KiB per runtime stream. Compiler timeout is 60 seconds per command. Cleanup is
manual removal of exact fixture-owned output/lab copies after holds clear; no
cleaner or background producer is installed. OS/vendor shader-cache retention is
outside this fixture's controls and remains under the lab owner's existing policy.
