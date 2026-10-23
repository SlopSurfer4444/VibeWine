# Encoder marshalling checks

Run the focused CPU checks from the DXMT component:

```sh
python3 tests/vibewine/encoder-marshalling/test_descriptor.py
python3 tests/vibewine/encoder-marshalling/test_context.py
python3 tests/vibewine/encoder-marshalling/test_present.py
```

The implementation reuses the existing synchronous command-node ABI. Native
descriptor construction keeps every setter and its value; it obtains the color
array once, each color attachment once, and each populated depth/stencil attachment
once. Render setup uses at most fourteen stack nodes (five basic, eight GS/TS,
one stream-output). Compute setup uses two. Present uses six stack nodes and keeps
metadata/viewport storage alive through the synchronous bridge call. Fence
callbacks, stage masks, resource declarations, GS/TS marshal calls, barrier order,
recorded draw lists and encoder-ending calls keep their existing order.

Checks compile source extracted from the current production files. No installed
Wine/DXMT runtime, game, window or Metal device is used:

- `test_descriptor.py` executes the whole native descriptor function against
  recording Objective-C objects. It verifies all eight slots and eleven setters,
  MRT/MSAA resolve fields, depth/stencil, visibility, sample count, array length,
  dimensions, tile guards, nil encoder/command buffer returns and descriptor
  lifetime. It executes the actual optimized and build-only reference branches:
  264 color dispatches versus 97 for the optimized path. Missing-field and
  bad-return controls must fail in both modes.
- `test_context.py` executes the production render, compute and Present switch
  arms using the real `Metal.hpp` wrappers and `FenceSet`. It covers sixteen
  GS/TS/stream-output/visibility combinations, maximum setup capacity, MRT and
  depth/stencil/MSAA fields, two-task GS/TS payloads and terminators, setup before
  marshal/draw, fence-stage precedence, nil encoder behavior, missing-config or
  drawable fallback fences, and immediate/minimum-duration presentation.
- `test_present.py` executes the whole production `Presenter::encodeCommands`
  with its metadata/configuration definitions and real `Metal.hpp`. It compares
  normalized commands with the previous six-wrapper sequence and tests matched
  and scaled dimensions, actual acquired geometry, null/non-null gamma, metadata,
  viewport/draw fields, fences, configuration application/failure/retry, missing
  drawable/texture, bad format, zero geometry and nil encoder.

All three fixtures compile the undefined default, explicit optimized (`0`) and
reference (`1`) macro variants. The undefined default and explicit optimized
selection take the same branch and produce equal observations. The reference
uses the actual historical wrappers/getter expressions, not a synthetic extra
loop over newly batched commands; both modes must satisfy the same goldens.

## Corrected build-only W4 references

The Meson combo option `vibewine_perf_reference` defaults to `none`:

| Option value | `DXMT_PERF_REF_W4_GETTERS` | `DXMT_PERF_REF_W4_BATCHING` |
| --- | --- | --- |
| `none` (default) | 0 | 0 |
| `w4` | 1 | 1 |
| `getters` | 1 | 0 |
| `batching` | 0 | 1 |

Configure fresh matched builds with `-Dvibewine_perf_reference=none` and
`-Dvibewine_perf_reference=w4`; the two partial modes support narrower
attribution. The PERF block emits the same explicit selection for host and
native C/C++, including the Objective-C native bridge. Verify these arguments
and option values in each frozen build's metadata. Do not mix a PE from one
generation with another generation's native bridge.

These controls are compile-time only: there is no new environment/configuration
switch. Both modes retain the current query/publication/retirement fixes, owned
bounded persistence and shutdown, WSI/HDR behavior, checked bridge interfaces,
node layout, shader flags and compiler cache epoch. Only W4 getter repetition
and fixed-command call grouping differ. Fence/stage masks, actual drawable
geometry, configuration and failure handling stay shared. W3 is unchanged.

The wiring fixture extracts the real option and PERF block into a temporary
minimal cross project, configures all four values plus the omitted default, and
checks four generated compile commands (host/native C/C++) for every mode:

```sh
python3 tests/vibewine/encoder-marshalling/test_reference_modes.py \
  --meson /absolute/reviewed/bin/meson \
  --cross-file /absolute/reviewed/cross-win64.ini
```

Supply `--ninja` if its reviewed path is not adjacent to Meson. This performs
temporary configuration and Meson's CPU compiler sanity checks only; it never
invokes Ninja, builds DXMT, executes PE output or touches a shared build.

All three checks use address/undefined-behavior sanitizers and warnings as errors.
The fixtures replace platform objects and command sinks; they do not prove the
native GPU interpreter, rendering correctness, performance, gameplay, asynchronous
Metal object lifetime or error handling beyond the exercised paths. Render and
compute nil-encoder tests preserve existing behavior; this change adds no new
recovery policy there. A matched PE/native build plus procedural GPU and visible
gameplay tests remain integration gates.

Expected call-count reductions are bounded: one basic render setup batch replaces
five calls (four removed); optional bindings join the same batch; compute setup
replaces two calls (one removed); Present replaces six calls (five removed). No
FPS or timing result follows from these counts. There is no descriptor cache,
attachment omission, asynchronous command storage or protocol change.

The fixture owner is this test directory. Generated source/binaries live in one
private temporary directory per invocation and are removed on completion; inputs
are fixed test cases, not collected user data. Tests print a bounded receipt and
source hashes to stdout and create no persistent producer or automatic archive.
