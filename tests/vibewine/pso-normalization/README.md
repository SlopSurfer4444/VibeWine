# PSO normalization and concurrent shader initialization

This general CPU correctness/reuse change canonicalizes a copy of the ordinary
graphics descriptor's `IndexBufferFormat` to NONE. It explicitly excludes
geometry, hull, domain and stream-output descriptors. Shared descriptor
hash/equality, specialized maps, live draw state, index-buffer bindings and
encoded draw packets retain their existing semantics.

Pixel variants discard only UNORM bits absent from the conservative
`PixelShader.ValidRenderTargets` output-register mask. The canonicalization
runs before the variant map and in the pixel factory before the AIR digest and
compile arguments. Declared outputs (including integer outputs, for which no
per-output type proof is exported) retain their bits. Dual-source variants
retain their full original masks. Coverage reflection does not prune sample
masks; depth controls and native attachments/blend descriptors are unchanged.
Depth-only/coverage-only shaders with zero color targets can share otherwise
irrelevant UNORM bits without losing their sample-mask or depth specialization.

The AIR cache epoch stays unchanged. The factory retains the existing digest
field sequence and compiler arguments for every retained canonical digest.
The converter reads UNORM bits only in the handler for declared ordinary output
registers (`dxbc_signature.cpp`, `D3D10_SB_OPERAND_TYPE_OUTPUT`). Old entries
under now-unreachable noncanonical digests become unused; no retained key is
reinterpreted. This is a key reduction, not a compiler-output revision.

`CreateShader` now uses the production `ShaderSingleflightCache` helper keyed
by the same complete SHA1 digest. One caller initializes outside all map/entry
locks; joined callers wait on that entry. Different keys can initialize
concurrently. A successful value remains cache-owned. Null/exception failures
wake joined callers and are removed by entry identity, so future calls retry.
`CreateShader` translates exceptions to its existing failure/null result. Native
handles and error objects have RAII ownership across allocation/logging failures;
failed argument-info allocation never reaches a null native output buffer.
Debug bytecode copies are also allocation-checked and released with the shader.

`Close()` is terminal and cancels pending publication without interrupting a
native initializer. Waiters return null; a late native value is destroyed.
Successful values remain alive until the owner/retained handles release shared
state. A retained helper Handle permits calls across helper-owner destruction;
normal C++ synchronization is required while obtaining that handle. This does
not authorize destroying a D3D device/cache concurrently with its active public
COM calls: their normal lifetime contract remains required. Factories must not
recursively request their own pending key.

Focused checks (no full runtime build):

```sh
python3 tests/vibewine/pso-normalization/run.py --arch arm64
python3 tests/vibewine/pso-normalization/run.py --arch x86_64
python3 tests/vibewine/pso-normalization/run-singleflight.py --arch arm64 --sanitize address
python3 tests/vibewine/pso-normalization/run-singleflight.py --arch x86_64 --sanitize address
python3 tests/vibewine/pso-normalization/run-singleflight.py --arch arm64 --sanitize thread
python3 tests/vibewine/pso-normalization/run-createshader.py --arch arm64 --sanitize address
python3 tests/vibewine/pso-normalization/run-createshader.py --arch x86_64 --sanitize address
python3 tests/vibewine/pso-normalization/run-createshader.py --arch arm64 --sanitize thread
```

`run.py` extracts actual graphics/variant lookup bodies, complete production
descriptor and variant keys/hash/equality, and the full pixel factory. Only
the surrounding native/device/scheduler/compiler boundaries are adapters.
It checks ordinary NONE/U16/U32 reuse, all stage/SO exclusions, independent
attachment/depth/sample and remaining key fields, output0/sparse/no-color
reflection, coverage on/off, preserved used bits and dual-source controls,
direct-factory digest consistency and actual pixel compile arguments. Constant
hash controls exercise full equality rather than relying on hash uniqueness.
Baseline `8cf59a01` must fail the two requested reuse assertions. Connected
fault controls remove exclusions or factory normalization and must fail their
specific semantic assertions. Both architectures use ASan/UBSan.

`run-singleflight.py` compiles the actual production helper, with exceptions
enabled and disabled. It checks deterministic joined waiters, shared pointer
identity, distinct-key progress, full equality under forced collisions, null
and throwing failure/retry, cancellation and retained-handle lifetime.
`run-createshader.py` compiles the actual `CreateShader`, wrapper constructor,
destructor and initialized predicate with real SHA1. It tests joined concurrent
callers, a distinct-key parser progressing during the first blocked parser,
warm hits, empty/nonempty reflection, owned/null native errors, argument malloc
failure, wrapper-allocation exceptions, initializer/message/logger exceptions,
persistent logger failure and debug bytecode allocation failure. Native handle,
error and raw allocation/free counters check ownership, including Darwin where
LeakSanitizer is unavailable. The old source must fail the duplicate-initialize
assertion. Early-publication assertions cover admitted waiters while the parser
or helper factory is gated; post-parser reflection completion is source-reviewed
rather than independently gated. Receipts reject source drift during each run
and bind captured source/fixture/runner hashes. Exact invocation receipts are
retained in the wave's `pso/` evidence directory.

These are CPU/source integration checks. They do not establish a measured
duplicate-initialization rate, cold compile latency improvement, Metal output
or image parity, gameplay compatibility or FPS. Native parser/Metal integration
and a matched full runtime build remain later gates owned by the campaign.

The fixed tests and documentation are maintained source. Each runner owns a
private temporary directory and removes its sources/binaries on normal exit or
failure; compilation/execution have bounded timeouts. Retained receipt/log data
under `outputs/frontier-wave-20261028/pso` is owned by the F4 integration branch,
contains source hashes and bounded CPU test output, has a seven-day TTL and
64 MiB aggregate cap, and is manually removed only after the root integration
owner releases active analysis, review and recovery holds. No runtime data
producer, persistent shader cache schema, service or cleanup process is added.
No installed game, runtime selection, existing cache or user data is touched.
