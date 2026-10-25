# Capture validity CPU checks

From the DXMT component:

```sh
python3 tests/vibewine/capture-validity/run.py
# Native/PE bridge checks alone:
python3 tests/vibewine/capture-validity/test_native.py
```

`run.py` compiles the current production `CaptureState` implementation, includes
its actual header and `Metal.hpp`, and extracts the complete capture switch from
`CommandQueue::CommitChunkInternal`, plus the whole production
`CommandQueue::ShutdownAndJoin`. Environment and native calls are synchronous
CPU stubs. It verifies default-off values, executable matching, scheduling, failed
starts, a capture owned elsewhere, retry after failure, successful one-stop
ownership, consumed ownership, and one-shot ownership transfer. Final-frame
capture is stopped once at shutdown without requiring another frame; failed or
external starts never trigger a shutdown stop. Repeated shutdown is idempotent.
Recording worker joins and an ownership observer verify at runtime that ownership
is taken only after both worker joins, while delegating ownership to the actual
production state. Compiled mutations
must be rejected for claiming ownership after failure, ignoring queue ownership,
discarding a successful start result, omitting shutdown stop, and taking shutdown
ownership before worker joins. The host build excludes the Windows
F10 implementation; its Windows-only private-field warning is suppressed, and no
keyboard-path acceptance is claimed.

`test_native.py` extracts the whole current native capture start/stop and allocation
getter functions, plus their PE thunk functions. Recording Objective-C classes
stand in for Metal. It checks unsupported destinations, already-active capture,
a native false result, a native true result, failed Unix dispatch, stop forwarding,
actual allocated bytes above 4 GiB, zero bytes and a nil allocation. Four compiled
controls must reject a poisoned return, ignored dispatch failure, false native
success, and bypass of the existing-capture guard. The real bridge structs are included,
and both native dispatch tables must append the allocation getter at opcode 149.
It does not construct a Metal device, start a trace capture or touch an existing
capture. Foundation is linked for Objective-C object support; mock Metal class
names are isolated from system classes.

Both fixtures run the host architecture with AddressSanitizer and
UndefinedBehaviorSanitizer and compile the other Darwin architecture. Cross-arch
compilation is not cross-arch execution. Errors fail the run; mutation controls
must terminate at a fixture check, without sanitizer findings. These tests prove
only the exercised CPU state/bridge contracts. Real capture-file validity, native
GPU behavior, screenshot/readback, installed runtime behavior and gameplay remain
separate integration checks.

The adjacent encoder-marshalling `test_present.py` extracts the whole production
present method. Its undefined-default, explicit-optimized and historical-wrapper
variants additionally cover actual-drawable aspect fitting, opaque black bars,
fractional viewports, and the offset-source-equals-viewport case that requires the
scale pipeline. A 640x480 source into an 800x480 drawable must record viewport
`{80, 0, 640, 480}` and select scale. This is a command-record check, not an image
or shader execution check.

Artifact lifecycle: these test runners own only their uniquely created temporary
directories (`dxmt-capture-state-*`, `dxmt-capture-native-*`, `dxmt-present-*`). Data
class is expendable generated source/object/binary and captured test output; TTL
is one test invocation, cleanup is `TemporaryDirectory` context exit, and there
are no active/hold exclusions or persistent producers. No production capture,
cache, user data or shared build is written. A failed or externally interrupted
process may leave OS temporary files for ordinary owner-managed temp cleanup;
these runners do not purge unknown existing directories. Output is bounded by the fixed scenario/mutation sets. There is no separately
enforced byte quota, bulk collection, persistent data producer or indefinite log
stream.
