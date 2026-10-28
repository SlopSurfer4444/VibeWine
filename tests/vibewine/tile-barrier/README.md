# Tile-helper PSO failure propagation

Run from the DXMT source root:

```sh
python3 tests/vibewine/tile-barrier/run.py --arch arm64
python3 tests/vibewine/tile-barrier/run.py --arch x86_64
python3 tests/vibewine/failed-pipeline/run.py
```

Both tile commands use ASan and UBSan. The x86_64 fixture asserts that it is
executing through Rosetta on this Apple Silicon host. A compiler-only result is
not accepted. No full build, Metal device, Wine runtime, game, or installation is
needed or exercised.

The runner reuses the existing failed-pipeline extractor and fixture for the
real CommandList, actual scheduling lambdas, application PSO bookkeeping, and
all 14 ordinary/indexed/indirect/geometry/tessellation draw routes. It extracts
the **complete production** TileBarrierContext declaration, constructor,
getPSO, dispatch, and resolveRenderPassBarrier bodies, plus the actual two-bit
EncoderBarrierState layout. Native PSO creation,
reference ownership, render-encoder state, and command recording use explicit
CPU seams. No substitute resolver or tile helper body supplies the assertions.
Source hashes, compiler/launch commands, outputs and generated source are saved
when `--keep NEW_DIRECTORY` is supplied; no executable is retained.

The 14 routes each inject two same-key nil native PSOs, then success and a
successful cached retry. Checks require:

- no tile command, application draw, visibility update or queued GS/TS marshal
  on failure; diagnostics remain visible;
- all four pending flags survive failure, with the unsupported fourth flag
  masked for this scenario; application last_pso and pipeline_failed stay intact;
- no nil cache entry or PSO owner, followed by one owned successful cache entry;
- exact application PSO -> tile PSO -> tile dispatch -> application PSO restore
  -> other required barriers -> dependent draw sequence on success;
- zero native recreation on cache hit, key separation, reference release at
  context destruction, and no bogus restoration without an application PSO.

The counted reference seam establishes retention by the production owning map;
it does not re-test native Metal reference counting. Existing application-pipeline
failure tests remain independently runnable and use their successful resolver
mock only for their original application failure scenarios.

Negative controls are independently runnable with `--mutation ignore-failure`,
`--mutation cache-nil`, `--mutation ignore-draw-guard`, and
`--mutation early-marshal`. They must fail behavioral assertions, and each
mutation is checked for an exact source match before compilation. The first
control reproduces the original silent-consumption semantics in the resolver.

## Admission limits

This is a narrow failure-path correctness repair. It is not a valid encoder-split
implementation or proof of GPU ordering, correct pixels, game compatibility,
performance, or FPS. Required future acceptance is a matched native/offscreen
fragment-dependency readback fixture with forced tile-helper creation failure,
retry, and dependent draw observation.

CPU-PASS-06 remains an explicit correctness gate: the existing unsupported
fragment-to-vertex branch warns, clears all pending flags, and proceeds. It can
also consume pending flags from an earlier failed tile helper. This repair
preserves that separately identified branch; do not claim universal safety for
paths reaching it. GS/TS marshal tasks are also hoisted before the recorded
render command list in flushCommands. Guarding their enqueue prevents orphan
work after tile failure but does not repair that existing successful-path
ordering concern.

## Evidence lifecycle

Owner: tile correctness branch; consumer: frontier-wave integration owner.
Data class: bounded source/CPU-fixture diagnostics and receipts. Collection is
explicit per command and stops on return. Default temporary generated sources
and binaries are removed by TemporaryDirectory. `--keep` is optional and bounded
to 4 MiB per directory; preserve successful admitted receipts and required failure
witnesses during active review. Retained diagnostic directories expire 14 days
after task acceptance, with a total 32 MiB cap for this fixture's receipts;
manual removal of exact task-owned inactive directories is the cleanup route.
Active review, explicit holds, required reproduction source and the final patch
are excluded until their owner releases them. No automatic cleaner is installed.
