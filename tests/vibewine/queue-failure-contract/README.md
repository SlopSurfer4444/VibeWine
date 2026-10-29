# Queue failure, publication and allocation contract

Run `python3 tests/vibewine/queue-failure-contract/run.py --sanitize`,
`python3 tests/vibewine/queue-terminal/run.py`, and
`python3 tests/vibewine/reftracker-retirement/run.py --sanitize`.
The runners hash exact production bytes before extracting their executable
consumers. Native/PE/COM boundaries are mocks; these are CPU correctness and
allocation controls, not native GPU/device-loss or gameplay acceptance.

The queue retains its first operation HRESULT. Host allocation rejection gives
`E_OUTOFMEMORY`; unclassified terminal Metal/encoding failure gives
`DXGI_ERROR_DEVICE_REMOVED`. The documented DXGI-only device-removal query returns
`DXGI_ERROR_DRIVER_INTERNAL_ERROR` for either unclassified terminal cause; the
original error remains available to the operation consumers and diagnostics.
No specific NSError-to-removal/reset classification is claimed.

Cancellation has a separate shared wake epoch. CPU coherence and frame completion
remain real values, never cancellation sentinels. Map, GetData, Present, GPU-idle
retirement and fence consumers inspect failure. Query readbacks default to discard;
only verified native Completed explicitly publishes them. Timestamp publication
uses no destructor allocation. The encoder stops admitting/submitting after first
failure. Shutdown joins workers, drains actually submitted command buffers, then
destroys remaining encoders, retained resources and borrowed command/tracker storage.

Checked ring reservations cover queue command/staging/argument/temp/tracker
allocation. API recording returns checked failure without null placement writes;
encoding allocation exceptions stay inside the worker. Query Begin/End/GetData
contain replacement-wrapper OOM at COM boundaries. Nil command buffers and all
11 native encoder-creation sites reject before dependent work or publication.
A terminal queue is not resumable: retain deduplication may already be marked.

The allocation control executes the real compute-counter setup fragment and its
CPU ring consumer. On the bound predecessor `61aca2a3621386f1c6777972327149e48675883e`,
100 setups allocate 100 standalone counter objects; the checked command-ring
candidate allocates none after warming its block. Both encode the same value37
and keep the counter alive through command ownership. This is allocation economy,
not an FPS claim. Earlier completed ring-node splice behavior remains covered.

`--source-ref 61aca2a3621386f1c6777972327149e48675883e --baseline --sanitize`
replays the old destructor-publication hazard and the sanitizer-attributed real
null-placement consumer. Running unchanged candidate assertions against that
source rejects its standalone-counter allocation count. The current CpuFence is
fixture plumbing for baseline replay; baseline modes do not test it. Receipts
separately hash the generated fixture inputs, so this compatibility seam is visible.

Additional production-consumer checks:

- `queue-terminal`: real queue admission/submit section, first failure, initializer
  throw containment, backpressure, real drained shutdown/idempotence, blocked
  staging Map loop, public NULL-event fence blocking/cancel path, registered
  failure wake ownership, and linked/unlinked/partially flushed encoder disposal.
- Seven independently corrupting mutations remove active cleanup, submitted drain,
  Map failure consumption, final admission/submit rejection, a native nil guard, or safe constructor registration;
  each must fail the unchanged assertions.
- `queue-terminal/creation.py`: complete production extern-C device/core and
  device/swapchain creation bodies under low/high/swap constructor OOM; return
  HRESULT, null output/feature sentinels and partial ownership controls, with a
  bound old-source C-ABI exception-escape failure.
- `query-publication`: real query/API classes and immediate Begin/End/GetData,
  including injected replacement OOM and healthy generation/publication controls.
- `native/test_listener.m`: actual listener include with fake native events,
  pending-before-callback registration, ticket/source OOM, PE delivery, cancellation,
  duplicate/late callbacks, HANDLE reuse, close, batching, and concurrent schedules.
- `ring-checked` and `ring-retention`: unchanged checked allocation, ownership and
  reuse-frontier regressions.

Win32 event registration has a checked PE export using the same Unix104 packet
and index. The legacy void export remains. The exact all-zero event/handle/value
triple is reserved for queue-owned listener cancellation; other mixed-null inputs
are rejected. Admitted registrations return success and own exactly one delivery;
rejected registrations return failure only after synchronous wake, permitting
safe HANDLE closure. NULL fence-event callers use an owned private PE event.

`QueueFailureWake` is allocation-free intrusive runtime metadata: one node per
live swapchain, owned by that swapchain and protected by the queue failure lock.
It wakes the bounded external frame-latency semaphore credits. Unregistration
precedes HANDLE closure; nodes retain no user data and have no post-destruction
retention. This is not a persisted producer or a background cleaner.

Limits: source controls do not prove a complete native COM/Metal device-loss run.
The resource initializer has its own GPU queue; this change consumes submission
failure but does not claim all its GPU failure paths repaired. Native completion
waits/joins may still block on a hung GPU or unmet foreign GPU fence; observed
terminal cancellation releases CPU waiters but cannot cancel native execution.
No all-process OOM or all external/shared-client recovery guarantee is made.
Queue depth, VSync, upload-event ordering and installed runtime remain unchanged.

Owner: sustained T2/T7 queue branch; consumer: root campaign. Exact private receipts
live under root-selected `outputs/sustained-wave-20261029/queue`. Temporary generated
sources/binaries auto-delete. Raw logs7days/32MiB; curated results30days/8MiB;
private builds256MiB. Active integration, unresolved evidence and selected recovery
are holds. Cleanup is manual by exact owner/path after reference/hold checks.
