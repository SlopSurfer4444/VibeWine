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
Only platform mutex, Metal buffer allocation and COM/base type scaffolding are
substituted; no production algorithms are rewritten. Class extraction is exact
and receipt hashes bind all three source files. Metal memory is an owned CPU
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
