# Native upload completion boundary fixture

Run `python3 tests/vibewine/initializer-inclusive-reuse/gpu/run.py` on macOS with
Metal and Xcode command line tools. This creates no window, runs no Wine or game,
uses no installed prefix and installs nothing. Compilation and execution use
private temporary files removed on exit; each run prints a source-bound receipt.
The runtime sandbox denies network access and filesystem writes outside scratch.

The builder extracts the complete current production `RingBumpState` class and
all template method definitions without changing their bodies. It also extracts
`ResourceInitializer::uploadReuseBoundary` unchanged and explicitly enables
`DXMT_INITIALIZER_INCLUSIVE_REUSE=1`. Native concrete allocators are replaced by a
small allocator backed by actual shared, untracked Metal buffers. Each standard
block is 4096 bytes to keep the test bounded. The fixture exercises the ring and
boundary with real Metal events, blits and CPU readback. It does not compile or
execute the full initializer, Winemetal bridge or texture upload path.

For both one source block and two distinct source blocks:

- A real queued command waits on a closed shared-event gate before reading known
  A bytes; completion stays at zero and destination bytes remain untouched.
- At completion zero, denying native allocation makes a second full reservation
  fail without changing output reservations or allowing pending source reuse.
- After releasing the gate, the first command completes and signals exactly one.
  Inclusive adaptation permits sequence two to reuse the exact original native
  backing identities at offset zero with native allocation still denied.
- Overwriting sources with independent B patterns and submitting another blit
  preserves all A destination bytes and returns all B destination bytes. Two
  planes use different original and reused native backings and different goldens.

The mandatory negative control intentionally passes a premature frontier of two
while completion is still zero. Reusing/overwriting the gated source then makes
the delayed A destination contain B. Expected exit20 at that exact oracle proves
the test actually observes delayed source reads and premature overwrite; another
failure or timeout does not count. Ordinary execution must exit0 with both cases.
Every GPU command has a five-second CPU deadline. RAII releases the gate on
exceptions; the process-group watchdog is30seconds. Compilation is bounded to
60seconds and output retention to64KiB per command.

A pass supports this ring/boundary lifetime ordering for shared buffers and these
bounded blits. It does not establish full production initializer/bridge acceptance,
managed-buffer behavior, hardware OOM recovery, texture/planar format correctness,
installed runtime behavior, game compatibility, FPS or a reason to enable the
experiment by default. Those integration gates remain with root.

Owner: initializer reuse fixture worker; consumer/integration owner: root.
Data class: temporary source/executable and bounded compile/run output. Scope:
this runner's private `dxmt-upload-reuse-metal-*` directory only. TTL: invocation,
cleanup by `TemporaryDirectory`; no persistent producer. Explicitly saved stdout
receipts follow the existing initializer repair contract's seven-day/64MiB raw
scope, active/recovery exclusions and manual owner cleanup. Source files remain
project artifacts. No cleanup of other directories or application data is added.
