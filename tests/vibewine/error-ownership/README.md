# Borrowed native error ownership

The Metal bridge passes a borrowed (+0) `NSError` to error out-parameters. The
old C++ wrappers wrote it directly into `Reference<Error>`, which releases its
handle. A failure could therefore destroy the NSError before its autorelease
pool drained. Repeated failures also overwrote an owned error without release.

All 14 local error-producing wrappers now require `Reference<Error>&`, receive
the native result in a zero-initialized local, and retain a temporary before
replacing the caller's reference. Null output resets the old reference without
a retain call. Retaining first also handles the same borrowed object being
returned while the caller holds its last reference.

The two local API adaptations preserve ownership through
`cs2_pipeline_archive.hpp::create` and use an owning error for residency-set
creation in `dxmt_command_queue.cpp`. The archive retry clears that owner with
`nullptr`; its retry protocol and conditions are unchanged. The residency test
mock follows the production owning-reference API.

## Provenance and license

Checked donor source:
[g-cqd/dxmt cbf3c18cee85195b1519d33e4980001b854f1beb](https://github.com/g-cqd/dxmt/commit/cbf3c18cee85195b1519d33e4980001b854f1beb),
`fix(winemetal): retain autoreleased errors returned to Reference<Error>`.
The donor's pinned [LICENSE](https://github.com/g-cqd/dxmt/blob/cbf3c18cee85195b1519d33e4980001b854f1beb/LICENSE)
is MIT, copyright 2023 Feifan He. Its complete notice is already preserved in
this repository's `LICENSE.OLD`; the current tree's LGPL notice remains intact.

This is an adaptation rather than a cherry-pick. The current local tree has two
additional error-producing wrappers (source-library and residency-set creation),
caller interfaces that need ownership propagation, and a same-handle edge case.
The donor's release-before-retain assignment is replaced with a retaining
reference temporary. No Metal ABI, successful shader lowering, or cache key
changes are included. Baseline: `25eff5e13003d5e7f6b7cf6e1c76e9e6f3363e27`.

## Checks

Run on macOS with Apple clang, Foundation, and Rosetta installed:

```sh
python3 tests/vibewine/error-ownership/run.py
python3 tests/vibewine/residency-sets/run.py --sanitize --negative-controls
python3 tests/vibewine/error-ownership/compile_callers.py /absolute/build-false/compile_commands.json /absolute/build-true/compile_commands.json
```

The native test includes the complete production `Metal.hpp`. It extracts the
exact native retain/release/autorelease-pool/newLibrary bodies and the matching
PE thunks into separate C/Objective-C and C++/Objective-C++ translation units,
using production wire structs. A CPU fake receives the library selector;
Foundation creates and destroys real tracked NSError/NSObject instances.
Remaining error-producing bridge functions are controlled CPU fakes.

Both arm64 and x86_64 execute with ASan/UBSan. Coverage includes both pool/ref
destruction orders, replacing distinct and identical errors, success clearing,
untouched/null outputs, and balancing +1 successful results/dispatch data. The
old baseline and omitted-retain mutation must fail a safe CHECK before pool
drain. The donor-style release-before-retain mutation must fail under sanitizer
when the returned error equals the caller's sole-owned object. No retain-count
heuristic supplies the acceptance verdict.

The compile check reuses existing cross-toolchain compile flags and generated
build headers, rebinds source/include paths to this checkout, removes all object
and dependency-output options, and runs `-fsyntax-only` from private scratch.
It checks all 13 production translation units containing Error callers,
including the header-defined clear path through dxmt_command.cpp. Existing game
build databases omit D3D12, so its five actual caller translation units use the
shared D3D11 pipeline dependency flags plus this checkout's d3d12 include path.
This is API compilation, not a full matched link or runtime acceptance.

Observed 2026-10-25 on this Mac: native arm64 and Rosetta x86_64 positives passed;
all six architecture/mutation negative runs rejected; residency suite passed
3 configurations and rejected 10 controls; all 13 actual caller translation units
passed for each of the two existing compile databases. Independent source review
found no blocker. Full matched build, installed behavior, GPU failure handling,
process teardown, game stability, and performance remain outside this test.

## Artifact contract

Owner: the error-ownership test runner; consumer: parent integration reviewer.
Scope: only `tests/vibewine/error-ownership/.tmp-*` private scratch. Data class:
sacrificial generated source, native objects/executables and compiler output;
no user/game data. Byte cap: 64 MiB per invocation; native runner checks total
scratch bytes after each control. Cleanup: `TemporaryDirectory` deletes the
exact private directory on normal exit and exceptions. If interrupted before
cleanup, residual raw scratch expires after 7 days; active runs and an explicit
analysis hold are excluded until the owner releases them. No persistent cleaner
is installed. Durable source/tests/provenance and parent-retained minimal test
receipts are results and are outside this raw scratch TTL. Compile-only checks
write no object/dependency files or shared build artifacts.
