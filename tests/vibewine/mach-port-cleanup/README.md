# Mach send-right cleanup fixture

Run on macOS with Apple clang:

```sh
python3 tests/vibewine/mach-port-cleanup/run.py
```

The runner extracts the exact production `WMTMachPortDeallocate` PE thunk and
`_WMTMachPortDeallocate` native function, compiles them with the production wire
headers, and invokes the native kernel operation on private ports created by
the fixture. Both functions receive arm64 and x86_64 syntax checks. Execution
uses the host architecture with AddressSanitizer and UndefinedBehaviorSanitizer.
No Metal/GPU, bootstrap lookup, imported external port, Wine process, installed
runtime, or game is accessed.

The dispatch seam records ordinal 150 and the exact 32-bit port argument before
calling the extracted native function. A wire-only value `0xfedcba98` checks
unsigned width without sending that synthetic name to the kernel. Zero is
checked both at the thunk (no dispatch) and native entry (successful no-op).
`mach_port_get_refs` proves that a single send reference disappears, three send
references decrement one at a time, receive rights survive, and a second owned
port is unchanged. Calling the native operation on a receive-only owned port
checks failure status and receive-right preservation. Wrong ordinal, wrong port,
and omitted deallocation mutations must fail a safe assertion without sanitizer
errors. The dispatch seam refuses wrong arguments before any native operation.

Both production unix-call tables must contain exactly 151 slots. Slots 0–149,
including the NULL slot, are frozen by separate SHA-256 values established from
Git commit `66bae06c1cba97db49172ef555e4ea4f2cc60efc`. The normalized hash input
is each initializer name without `&`, one per line, with a trailing newline.
Slot 149 remains `_MTLAllocation_allocatedSize`; slot 150 is exactly the new
cleanup entry. A historical Git object is not required to run the test.

The C++ fixture extracts the exact one-argument `Texture::allocate` method and
the exact texture-taking `TextureAllocation` constructor. Eleven cases cover:

- Shared wrapper `operator new` failure and constructor vector-allocation failure.
- Shared success, and native nil with either failure or successful wrapper creation.
- Nonshared wrapper allocation/constructor failure and success.
- The buffer-backed branch with success and wrapper allocation failure.

The shared device stand-in creates a real private receive/send pair. Both
exceptions must release exactly its one send reference through the extracted
production bridge, preserve its receive right, rethrow, and balance the tracked
texture reference. Successful wrapping must not release the send right early.
A separate private port supplied as stale/borrowed input to `info_` must survive
all cases; allocation resets the local port before any fresh creation. An
omitted-cleanup mutation must fail. The fixture supplies minimal device,
reference, allocation-container and enum-operation stand-ins; the constructor's
real `std::vector::resize` throws via its test allocator. The buffer constructor
is a stand-in, and imported-resource factory paths are not executed.

Each run prints SHA-256 identities for the four bridge inputs and texture source
and rejects source changes during execution. The fixture proves the extracted
ownership paths and native kernel behavior. It does not prove a full matched
build, loaded PE/native dispatch, Metal allocation/failure behavior, successful
shared resource publication, GPU lifetime, installed behavior, or game results.

Observed on 2026-10-26: arm64+x86_64 syntax checks passed; host arm64 execution
passed under ASan/UBSan; all 11 texture cases and four mutation controls passed.

## Artifact contract

Owner: this test runner; consumer: parent integration reviewer. Scope: only
`tests/vibewine/mach-port-cleanup/.tmp-*`. Data class: generated source, native
objects/executables, and bounded compiler/runtime diagnostics. Raw scratch cap:
64 MiB per invocation, checked after each executable build. Runtime output is
limited to fixed fixture assertions/results; commands have bounded timeouts.
Cleanup: `TemporaryDirectory` removes the exact owned scratch directory on
success and exceptions. Native fixtures explicitly release their owned send and
receive rights on every assertion path; process teardown also reclaims rights
if interrupted. On abnormal process interruption, leftover private raw scratch
expires after seven days, excluding active invocations and explicit analysis
holds. Removal is manual by the owner after rechecking identity/holds; no cleaner
is installed. Durable test sources, this document, and minimal parent-retained
receipts are results outside that raw-data TTL. No user/game data is collected.
