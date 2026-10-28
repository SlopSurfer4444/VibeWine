# Query publication regressions

Run from the component root:

```sh
python3 tests/vibewine/query-publication/run.py --sanitize thread
python3 tests/vibewine/query-publication/run.py --sanitize address
python3 tests/vibewine/query-publication/run.py --source-ref fab325e0adcd639d822dbfac87a1b840ccb2d4bf --sanitize thread --expect-races --symbolizer /path/to/llvm-symbolizer
python3 tests/vibewine/query-publication/run.py --source-ref fab325e0adcd639d822dbfac87a1b840ccb2d4bf --expect-empty-baseline
```

The runner extracts the complete production visibility/timestamp classes,
visibility readback, offset-bump state, D3D11 query API classes, and the actual
visibility flush-selection/removal statements. It uses the real `Rc` header.
The complete immediate `GetData` method and production event/disjoint class are
also extracted. Platform mutex, Metal buffer allocation, trace/queue and COM/base
type scaffolding use CPU seams. The production algorithms remain intact; two
class-local `operator new/delete` hooks count actual backend wrapper allocations
and final deletes. Receipt hashes bind all five source files, extracted headers,
fixture and runner, with the compile command and executable architecture. Metal memory is an owned CPU
array, so these checks do not establish native Metal/GPU behavior.

Candidate scenarios cover timestamp canaries and full-width values, pending and
ready queries, overlapping queries, partial completion before and after End,
serial reset/reuse after retirement, fresh API generations while previous
readbacks remain alive, concurrent encoder/finish/API polling, query removal,
same-chunk empty ranges, empty final chunks, entirely empty multi-chunk ranges,
poisoned-storage initialization and last-reference destruction on another thread.
Empty multi-chunk completion must remain pending until its final readback retires.
Empty readbacks must not allocate a native zero-length Metal buffer.

The baseline race control runs only the original single-issue concurrent reader
and writer schedule. It requires exit 66, exactly three source-attributed TSAN
reports (one timestamp and two visibility), exact success output and no unrelated
diagnostics. An LLVM symbolizer may be required in restricted macOS environments;
missing attribution fails closed. Different compiler/runtime reporting can also
fail this strict control. The deterministic empty-chunk baseline runs only the
functional scenarios, avoiding additional races in the known-broken source; it
requires exactly four named completion failures and normal exit 1.

Compilation is bounded to 60 seconds and execution to 30 seconds. Scratch source
and executables are removed automatically. `--keep DIR` preserves only generated
source, stdout/stderr and a hash/status receipt; executables/debug bundles are
excluded. The integrator owns those optional evidence files and their retention.
macOS lacks LeakSanitizer support here, so the address mode enables ASan/UBSan
without claiming leak detection. No runtime, installed caches or game process is
modified.

The device/cache teardown concern is separate. These tests do not exercise final
device release while a raw-pipeline encode callback is queued. See
`docs/query-publication-20261023.md` for the discriminating follow-up seam.

## Status-only and cached-generation regressions (2026-10-28)

```sh
python3 tests/vibewine/query-publication/run.py --source-ref 8cf59a01 --expect-frontier-baseline --sanitize address
python3 tests/vibewine/query-publication/run.py --arch x86_64 --sanitize address
python3 tests/vibewine/query-publication/run.py --negative reuse-generation --sanitize address
python3 tests/vibewine/query-publication/run.py --negative drop-empty-completion --sanitize address
```

All six implemented query kinds pass nonnull canary storage with `DataSize=0`
through the actual immediate method. Tests cover ready/pending status, null/zero,
nonzero/null, wrong nonzero size, normal result size/value/guard bytes, building
and undefined-event errors, unchanged DONOTFLUSH behavior, ordinary flush and
event-stall progress. Statistics always return ready under the existing stub.
Wrapper counters distinguish initial cached reads, first completion and 100
subsequent cached reads. The baseline requires the exact named write/allocation
failures. Candidate checks retain completed wrappers through reissue and API
release, direct `DoDeferredQuery` publication replacement, timestamp reissue and an empty same-chunk readback
retired after a fresh generation begins. Existing concurrent-publication and
empty-multichunk coverage also runs in the aggregate candidate mode.

The generation negative removes only first-success wrapper rotation, preserving
initial creation and already-issued reissue allocation. It must fail the exact
five lifetime/cached-generation assertions. The progress negative removes only
the actual flush selection for pending empty readbacks and must fail exactly
four empty-chunk completion assertions. These are controlled source mutations
in generated scratch, never production file edits. Native arm64 TSAN and
ASan/UBSan plus x86_64 execution on an arm64 host (Rosetta) were observed; this
is CPU fixture evidence, with no native Metal/GPU or installed-runtime claim.
Deferred command-list execution is not exercised; Flush uses a counted queue seam.
