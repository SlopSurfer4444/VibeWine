# Render fence packet assessment

Disposition: **defer production fence batching**. This directory contains an
assessment-only prototype using the existing linked-list command ABI. No
production/Meson/native-interpreter change or runtime installation is included.

The source already bounds each fence set to four 64-bit parity groups, so the
union contains at most 256 fence slots. `FenceSet::forEach(prior, ...)` visits
slots in increasing parity/lane order and invokes the prior callback once for
overlaps (`src/dxmt/dxmt_deptrack.hpp:181`). Production waits give pre-raster
priority; updates give fragment priority. Each existing Metal.hpp fence wrapper
constructs one node and calls opcode 38 (`src/winemetal/Metal.hpp:489,499`). The
native interpreter consumes these nodes synchronously and forwards their exact
stage mask (`src/winemetal/unix/winemetal_unix.c:1237`).

The prototype preserves the extracted production iteration/callback ordering,
collects at most eight nodes (256 bytes), then emits through the existing thunk.
It holds one pending fence and uses the original wrapper for a singleton or
singleton tail. Nil encoders take the original path. Waits and updates remain
separate phases; this does not move fence operations across setup, commands, or
encoder boundaries.

Run from the repository root:

```sh
python3 tests/vibewine/transport-packets/fence-assessment/run.py --sanitize
python3 tests/vibewine/transport-packets/fence-assessment/run.py
```

Both runs passed 3,340 scenarios on 2026-10-26: all cardinalities 0..256 with
normal-only, prior-only and overlapping masks; both wait/update stage priorities;
nil/non-nil encoders; and 128 additional mixed mask cases. Exact event type,
fence handle, stage mask and order match the original wrappers and an independent
scalar oracle. Zero emits no calls, singleton emits one, and cardinality N emits
ceil(N/8) calls versus N for the original, with nil fallback retaining N calls.

The release CPU proxy used Apple clang 21.0.0 on arm64, `-O2 -fno-lto`. Fixture,
the exact extracted opcode-38 PE thunk, and native observer compile in three
separate translation units, preventing the observer from being inlined into the
packet builder. Seven alternating baseline/prototype samples, each 30,000
iterations, yielded these median nanoseconds per entire phase:

| Fence count | Calls original / packet | Wait original / packet ns | Update original / packet ns |
| ---: | ---: | ---: | ---: |
| 0 | 0 / 0 | 5.19 / 7.72 | 5.38 / 5.27 |
| 1 | 1 / 1 | 10.46 / 11.39 | 9.84 / 11.32 |
| 2 | 2 / 1 | 17.52 / 15.07 | 16.58 / 15.02 |
| 4 | 4 / 1 | 28.45 / 27.78 | 27.55 / 26.99 |
| 8 | 8 / 1 | 56.15 / 58.57 | 56.52 / 57.94 |
| 9 | 9 / 2 | 63.29 / 64.91 | 65.27 / 67.02 |
| 16 | 16 / 2 | 141.04 / 142.55 | 134.41 / 120.40 |
| 64 | 64 / 8 | 423.44 / 400.54 | 417.46 / 382.25 |
| 256 | 256 / 32 | 1619.62 / 1514.45 | 1832.79 / 1743.04 |

The second release run, after adding output metadata only, passed the same
3,340 semantic scenarios but varied under concurrent machine activity. For 256
fences it measured wait 3514.75 / 7384.12 ns and update 4755.29 / 7997.03 ns;
singleton wait was 15.79 / 21.79 ns. No affinity, machine-idle condition or
frequency control was established. The first timing table is a recorded sample,
not a stable speedup estimate, and the second result must be retained when
interpreting it. No further timing runs were used to select a favorable result.

These are local CPU measurements including a digest observer. They do not measure
Wine transition latency or Metal execution, and sanitized timings are not used
as performance evidence. Small-count results are mixed; zero/singleton cannot
save a crossing, while the prototype adds bookkeeping. Larger sets save many
calls but lack evidence of frequency in the target workload. Source inspection
and CPU equality do not establish a game/FPS benefit. Production admission needs
a representative fence-count distribution and a measured real transition cost.

The release run bound production context SHA-256
`b014b19bcc0df6f4c0617aaf82506a6ebdd66bc6588b0fa062ee3b55498b6363`;
the runner prints fresh hashes for all binding sources and assessment files.
Temporary generated sources/objects/binaries are scoped to a temporary directory
and removed on exit. No persistent collector or benchmark output file is created.

The final marshal-packet candidate context is
`7e59468adb1530f0dd27c65c485b7fb72c4a1723a479e2665974cbcc15ca29a9`.
Only the separate marshal feature gate changed after the assessed context above;
reconstructing its pre-W4 gate reproduces the recorded b014b19b source hash.
Both extracted production fence blocks were checked byte-identical to base
`a21579fb` and the final candidate. No additional timing run was used for this
source-binding check.
