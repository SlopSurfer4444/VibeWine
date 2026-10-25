# Resolve adjacency GPU guest source

This source-only D3D11 guest targets `R(S red MSAA) -> X(D green) -> Resolve S to D`
and checks that the final single-sample destination D is exactly red. An unsafe
optimizer that attaches the resolve to R across X can instead leave D green.
The source does not claim a compiled PE, matched build, GPU run or installed fix.
The root integration owner alone owns compilation, execution, mapping verification
and private-prefix/server lifecycle. No runner or production file is changed here.

`guest.cpp` includes `../../gpu-wave/guest.cpp` after renaming that file's `main`.
It calls only the shared RAII references, pipeline/shader preparation, deadline,
watchdog, escaped JSON, module-path, nonblocking mapping and report helpers. Its
own `main` runs only the adjacency workflow. This reuses an existing local LGPL
fixture rather than copying a second support subsystem. Build receipts must bind
the hashes of **both** source files, including the transitive include. Snapshotting
only this guest into an unrelated directory breaks the relative include and is
not a complete source identity. The existing `gpu-wave/build.py` and analyzer
are not configured for this guest's source, start record or stage names.

The default run checks two required paths; `--path compute` or `--path blit`
selects one. Each path precreates a 64x64 four-sample RGBA8 render target S,
single-sample RGBA8 destination D, initialized green source texture, staging
texture, raster state and event query. The compute path also precompiles and
creates a shader and typed UAV. The shared render pipeline is created before
either hazard. Each sequence poisons all samples of S blue and sets red draw
constants, then explicitly flushes setup before entering the sequence:

1. A full-screen oversized triangle writes red with alpha one into all samples
   of S. The rasterizer enables multisampling, the sample mask defaults to all
   bits, there is no scissor or depth test, and the pixel shader returns constant
   red. All samples and all pixels have the same exact red resolve golden.
   Immediately after Draw, `OMSetRenderTargets(0,nullptr,nullptr)` explicitly
   unbinds S and ends R before X, preserving recording in the same current chunk.
2. Compute dispatches 8x8 groups of 8x8 threads and stores `(0,1,0,1)` through
   `RWTexture2D<float4>` into D, or blit copies the precreated green texture to D.
   The compute UAV is unbound using a state setter after dispatch.
3. `ResolveSubresource(D,0,S,0,RGBA8)` must overwrite D red after X. Only after
   that call does the guest copy D to staging, end the precreated event, flush,
   poll, and map for exact row-pitch-aware byte checks.

There is no guest `Flush`, `GetData`, `Map`, event/query boundary, wait, resource
creation or shader creation between Rdraw, Xwrite and Resolve. The tiny sequence
is not expanded into repeated batches or mixed with setup/readback commands.
After its red check, every path repeats R/X with resolve omitted and requires
every destination byte to be green. This mandatory diagnostic verifies that X
actually affects D; an omitted/no-op X cannot satisfy both goldens.

## Source-grounded chunk adjacency and its limits

The checked D3D11 recording paths in this checkout are:

- `src/d3d11/d3d11_context_imm.cpp`: `EmitST` and `EmitOP` append directly to
  `cmd_queue.CurrentChunk()`; `Commit` clears `promote_flush` and commits the
  current chunk. `Flush1` invalidates with deferred commit and calls `Commit`
  for dirty operations or promotion. The pre-hazard blue clear ensures setup is
  dirty, so the setup Flush actually commits instead of being an empty no-op.
- `src/d3d11/d3d11_context_impl.cpp`: ordinary `Draw` uses `EmitDrawOP`, and
  `Dispatch` uses `EmitDispatchOP`; both wrappers use `EmitOP`. `PreDispatch`
  switches encoder through `InvalidateCurrentPass`, then emits compute setup.
  `CopyResource` of these DEFAULT, identical-format single-subresource textures
  reaches the on-device `CopyTextureBitcast` path and `SwitchToBlitEncoder`.
  `ResolveSubresource` invalidates the current pass and appends `resolveTexture`.
- The zero-RTV `OMSetRenderTargets` delegates to
  `OMSetRenderTargetsAndUnorderedAccessViews`; removing the bound S sets
  `should_invalidate_pass` and invokes `InvalidateCurrentPass`. It ends R by
  emitting `endPass` into the current chunk, and does not set `promote_flush`.
  The selected zero-RTV/zero-OM-UAV path has no other commit operation. S is
  explicitly unbound before X and Resolve, so acceptance does not depend on
  interpreting an active source RTV binding as valid for ResolveSubresource.
- `InvalidateCurrentPass` commits only when `promote_flush` is set and commit is
  not deferred. Neither this ordinary draw, dispatch, DEFAULT-to-DEFAULT copy,
  zero-RTV state setter, UAV state setter nor resolve sets that flag. Setup Flush clears previous
  promotion before the hazard; no staging/dynamic resource or query intervenes.
- `src/dxmt/dxmt_command_queue.hpp`: `CurrentChunk` selects the chunk from
  `ready_for_encode`; `emitcc` allocates and appends. `AllocateCommandData` uses
  the CPU command allocator without a threshold-driven `CommitCurrentChunk`.
  `src/dxmt/dxmt_command_queue.cpp` advances `ready_for_encode` on explicit commit.

Those inspected paths support recording R/X/Resolve into the same current chunk
in this source, with distinct intervening compute or blit pass X. API submission
order alone is not independent observation of actual chunk IDs, encoder topology
or optimizer choice in an executable. Shader compilation can block internally;
precreating shaders does not prove the Metal pipeline cache is warm. No implicit
recording commit was found in the selected path, but this source-only handoff
cannot rule out behavior in a different candidate, configuration, instrumentation
or driver. Root must bind the executed sources/build and use existing chunk/pass
trace evidence if observed adjacency is required for acceptance. A green result
is a correctness failure; a red result without those bindings is not proof that
the intended optimization path ran.

## Invocation and report contract for the integration owner

An already reviewed LLVM-MinGW compiler can compile this source in place with
the existing wave guest flags: C++20, static linkage, `-O2 -Wall -Wextra -Werror`,
and `-ld3d11 -ldxgi -ld3dcompiler -ldxguid`. This document does not run a compiler,
Wine, GPU command or installer, and does not automatically choose any toolchain.

Execution requires the integration owner's existing disposable isolated prefix,
private server, copied/hash-bound matched PE/native candidate, external network
isolation, bounded cache/temp locations and independent process/log deadline.
Set `VIBEWINE_GPU_MAP_ACK` to an initially absent path inside that owned sandbox.
After emitting D3D11, DXGI, WineMetal and D3DCompiler 47 module paths, the guest
requires that acknowledgement within 15 seconds, and fails if the variable is
absent or too long. Root creates it only after checking the same live process
incarnation and admitted PE/native paths and hashes. Unlike the reference guest,
this adjunct has no legacy one-second observation fallback.

The normal default acceptance is natural exit zero; one start record with probe
`resolve-adjacency`, path `both`, and negative control `none`; the four required
module records; exactly two pass stages named `compute_destination_write` then
`blit_destination_write`; and one pass summary with `stages_passed=2`,
`stages_unsupported=0`, `negative_controls_rejected=0`. A selected single path
requires only its stage and `stages_passed=1`. Every passing stage verifies both
the red resolve and green omission, each over 4096 RGBA pixels. Validate JSON
integrity, counts, stage order, natural exit and matching source/module hashes
in the root-owned harness; no parser is added in this source-only slice.

RGBA8 texture/render/resolve/four-sample capability, and typed UAV capability for
compute, are explicitly queried. Unavailable capability emits `unsupported` for
that stage, increments the unsupported count and causes summary `unsupported`
with exit 2 even if the other path passes. Hardware feature-level unsupported
also exits 2. An API failure after supported capability is a failure, not a skip.
Exit 10 is a correctness/API/ack failure; exit 64 is invalid arguments. Cooperative
budget is 90 seconds, event and map polling each have five-second deadlines, and
the shared independent watchdog terminates at 100 seconds with exit 124. Timeout
or forced termination is failure and does not prove submitted GPU work cancelled.

Root may run separate negative processes per supported path:
`--path compute --negative-control wrong-golden`,
`--path compute --negative-control omit-resolve`, and the analogous blit commands.
`wrong-golden` first verifies the actual resolved red image, then deliberately
requires green; intended rejection is detail `negative_wrong_golden`.
`omit-resolve` first verifies resolved red and omitted-resolve green, then
deliberately requires omitted-resolve output red; intended rejection is detail
`negative_omitted_resolve`. Each must naturally exit 20 with summary status
`negative_control_rejected` and `negative_controls_rejected=1`. The stage failure
record is expected only at that precise intentional check. Earlier failures,
unsupported hardware, timeouts or a different detail do not count as rejection.
Neither negative result substitutes for a normal passing run.

## Data and evidence ownership

This directory owns only these two source files. Root owns generated binaries,
module acknowledgements, bounded logs and source/build/run receipts under its
existing campaign contract. Reuse the wave fixture caps: source bundle 512 KiB,
executable 32 MiB, each build/inspection log 64 KiB, receipt 32 KiB, guest stdout
64 KiB; cap stderr externally. Source and minimal unresolved regression evidence
remain held as campaign evidence. Temporary compiler directories expire at test
completion; a successful retained build/run quarantine expires at acceptance
plus seven days with a 64 MiB cap unless held for an unresolved regression.
Cleanup belongs to root's existing scope-aware lifecycle, and no cleaner is
installed or authorized to touch unrelated user/runtime data here.

This guest does not establish game compatibility, FPS, displayed frames,
performance improvement, full resource-hazard coverage or GPU success without
an admitted completed run. It checks the stated red/green resolve-order scenario.
