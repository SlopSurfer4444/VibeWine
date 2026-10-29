# Optional Metal4 compiler bridge

The native bridge can create ordinary render/compute PSOs using the public
`MTL4Compiler`, while preserving the existing command queue, encoders and ABI.
This is an experimental compiler route, not a Metal4 command backend or a
performance claim. It is disabled unless both the build macro
`DXMT_METAL4_COMPILER=1` and process environment `DXMT_METAL4_COMPILER=1` are set.
The environment choice is frozen at first access. The integrating build owner
supplies the default-off Meson option; this slice does not edit the build graph.

## Supported recipe and fallback

SDK headers, macOS 26 availability, the Metal4 GPU family and compiler selector
are required. Existing WineMetal function factories attach immutable native
provenance (retained library, copied name and function constants). Callers keep
the ordinary function/PSO handles and error borrowing rules.

The existing legacy descriptor is the authoritative marshalled input. The
adapter transfers all eight color attachments including blend operations,
factors and masks; functions and specialization; rasterization, sample count,
alpha coverage and topology; and the compute threadgroup-width setting.
Identity color mapping and disabled alpha-to-one/ICB support are explicit.
Metal4 has no immutable-buffer hints; these optimization hints are intentionally
omitted, preserving buffer semantics. Normal D3D11 pipelines use these hints.

No-depth and Depth32Float/no-stencil recipes are admitted. Metal4 derives depth
format from the render pass; the native fixture exercises actual depth rejection
through a classic encoder. Other depth/stencil formats, patch vertex functions,
private logic operations, ICB support, binary archive lookup/serialization/miss
requests, missing provenance, and rasterization without a fragment function
use the existing legacy route. Mesh, geometry/tessellation routes and tile PSOs
are unchanged. A nil fragment is a valid legacy recipe, not a fabricated error.

Once a supported recipe selects Metal4, invalid required stages/sample count or
compiler/PSO failures return nil and the original or synthetic `NSError`.
They never retry the legacy compiler. A nonnil result paired with an error is
released. Nil vertex is prevalidated because native Metal4 may assert; nil
fragment is excluded because the driver may silently accept it.

Compilation is synchronous on the existing caller, serialized across this
native module. At most one compiler exists in the module at a time; it is
released after each call. Returned PSOs have ordinary `new` ownership. Errors
retain the existing autoreleased/borrowed ABI. No new inner autorelease pool
drains errors before returning. Provenance follows the function lifetime;
there is no global function registry, worker, new cache, disk producer or AIR
epoch change. Legacy native archives remain on the legacy compiler, so no
Metal4 data is stored in their namespace.

## Verification

On a Metal4-capable Mac with the existing Xcode SDK:

```sh
python3 tests/vibewine/metal4-compiler/run.py --probe-depth --output /absolute/owned/results
```

The runner extracts the four actual native factories without replacing their
implementations, includes a snapshot of the production adapter, and uses the
real `winemetal.h`/thunk structs. Native compilation uses Objective-C C2x, MRC
and macOS 14 deployment with unguarded availability errors. It also compiles
the x86_64 slice, default-off and forced-header-unavailable branches, and checks
runtime opt-in freezing. Forced unavailability is not an older physical SDK/OS.

The native fixture compares GPU output with independent goldens and the legacy
compiler: shader buffers/textures, copied fragment/compute constants, blending,
write masks, 4x MSAA, Depth32 testing and repeated use. A separate first test
drains the caller's function/library/descriptor scope before using retained
PSOs; it proves usability, not internal driver object deallocation. CPU receiver
tests verify descriptor mapping and failure/fallback boundaries. The runner
requires an intentionally disabled-blending mutation to fail, proving a
connected observation rather than a compile-only pass.

The main fixture above uses owned MSL. A separate translated corpus now checks
five real Wine test shaders (two VS/PS pairs and one no-op CS), extracted with
LGPL provenance from the explicitly supplied Wine source. The MRT pair reaches
the exact render factory, has two independent green/red image goldens and tests
register matching. The blend pair uses the exact production helper with an
explicit vertex-attribute layout because CLI `airconv -A` emits attributes;
ordinary production D3D11 uses vertex pulling. This distinction is printed in
every case. Blend, write mask and 4xMSAA exercise real output. All translated
render cases compare legacy bytes and retained-PSO reuse after the creation
scope has drained. The no-op CS checks exact factory/dispatch/completion and
retained PSO use, and makes no compute resource/output claim. A deliberate
write-mask mutation must fail the translated independent image golden.

Prepare without a compiler or GPU:

```sh
python3 tests/vibewine/metal4-compiler/check-harness.py --wine-source /absolute/wine/dlls/d3d11/tests/d3d11.c
python3 tests/vibewine/metal4-compiler/prepare-corpus.py --wine-source /absolute/wine/dlls/d3d11/tests/d3d11.c --output /absolute/owned/corpus
```

Then, under the campaign's compiler-slot grant, use a matched existing converter
and a fresh prepared corpus attempt. The translator stages all AIR before
publication; an OS error during publication leaves a held partial attempt for
manual reconciliation. It never overwrites prior AIR. The native runner
validates source bytes, re-extracted DXBC, converter digest, translation receipt
and every AIR digest before execution.

```sh
python3 tests/vibewine/metal4-compiler/translate-corpus.py --airconv /absolute/matched/airconv --corpus /absolute/owned/corpus
python3 tests/vibewine/metal4-compiler/run.py --build-only --output /absolute/owned/build-check
```

Only with the exclusive native-GPU grant:

```sh
python3 tests/vibewine/metal4-compiler/run.py --probe-depth --translated-dir /absolute/owned/corpus --measure-cost --output /absolute/owned/native-check
```

`--measure-cost` adds a test-only measured compiler receiver around the unchanged
production helper. It reports actual compiler setup, PSO compile, module-mutex
wait and total helper time. Its five modes compare per-call creation with
fixture-owned per-thread reuse at the same one/two caller counts, plus an
explicit experimental unlocked/reused mode. Every owned thread joins; its
compiler/cache is released before return. A shared start gate and active-call
peak observe overlap; the original locked modes must remain peak one. Three
rotated/reversed mode orders follow a common warmup. The first sample is first
use of that worker/compiler, **not** a cold driver-cache measurement. Mutex
interposition only observes production locking in parity/default modes.

The 29 October tiny warmed MRT corpus measured sub-microsecond median compiler
setup. This does not justify a production TLS cache: helper callers can exceed
the shader scheduler pool, and native-module unload/destructor lifetime is not
established. Removing the mutex also expands active compiler memory/concurrency.
The production compiler lifetime and serialization therefore remain unchanged.
Fixture counterfactuals are evidence for that decision, not installed behavior.

There is no claim of general translated shader coverage, long-duration leak
freedom, old-system execution, cold-cache cost, game compatibility, installation
or FPS improvement. Apple references: [compilation API](https://developer.apple.com/documentation/metal/using-the-metal-4-compilation-api)
and [public pipeline examples](https://github.com/apple/game-porting-toolkit/blob/main/game-porting-skills/skills/creating-metal4-shader-pipelines/SKILL.md).
Exact selectors and availability come from the installed public SDK headers.

## Artifact ownership and retention

Owner: Metal4 branch; integration consumer: root. Repository source/tests are
durable source. Test subprocess output is collected incrementally with a 1 MiB per-stream
limit and 60-second deadline (translation: 30 seconds); output excess/deadline
terminates the owned process group, including descendants. Aggregate retained
logs remain subject to the 32 MiB branch cap; temporary build files are removed by the runner.
The task-owned result directory contains diagnostic logs (7 days, total 32 MiB)
and reports/receipts/patches (30 days, total 8 MiB). Transient builds have a
256 MiB cap with active-build hold. No automatic cleanup service is installed.
Manual cleanup must verify exact task ownership, inactive producers and no
consumer/recovery hold, skip symlinks/unknown files, preserve required evidence,
and delete only expired task-owned artifacts. A live function or PSO is an
active runtime hold; its native owners release associated data on lifecycle
completion rather than imposing a timer on live GPU resources.
