# Finish-thread stall scope fixture

Run `python3 tests/vibewine/stall-finish/run.py` from the DXMT tree; add
`--arch x86_64` for the translated architecture. Both modes use ASan and UBSan.

The runner extracts the entire production `CommandQueue::WaitForFinishThread`
method and the complete production `CommandChunk::reset` method verbatim.
Counted queue/native endpoints and diagnostic boundary spies check:

- Readback release, reset, CPU-coherence fence, idle sweep, chunk counter
  decrement/notification, and three allocator frees occur exactly once in their
  existing order. Frame-latency signaling remains before resource retirement.
- Scope 110 contains reset; subscopes117/118/119 separately contain list
  destruction, ref clearing and native-buffer release, in that order. Scope108
  contains only sweep, and109 contains the three
  ring frees. Diagnostic reporting stays outside 108. Saved queue, positive
  sequence and pre-reset frame identities reach every scope and detail record.
- Idle retirement compiled on/off; scheduled wait and already terminal
  completion; error status and native logs; trace generation/detail/profile
  branches; invalid GPU timings; initial stop and stop after queue wait.
- Opt-in completion snapshots precede the native wait; completion measurement
  finishes immediately after it. Exact statuses, pre-wait queue frontiers, final
  frame and initializer target/observed values reach the card boundary before
  resource retirement. Default/unpublished reset paths use poisoned scalar
  conversions to reject reads of uninitialized chunk/frame identities while
  diagnostics are unarmed. Fast/inactive/stale/disabled and error-return paths do
  not add waits; only completed slow waits read native GPU properties.
- Independent mutants removing 109, widening 108, resetting before readback,
  moving the CPU fence ahead of resource release, or decrementing the chunk
  counter before CPU coherence are rejected by the same behavioral oracle.
  Controls also reject an extra GPU wait, completion finish before the wait, a
  missing initializer observation, reordered reset operations and a missing
  reset subscope; an unconditional unpublished-identity read is rejected too.

The fixture spies on `StallCompletionWait` and `StallScope`; it does not execute
`GetThreadTimes`, the actual trace producer, Metal, resource
destructors, pool policies, atomics, or real workers. It proves the production
method's endpoint order, scope placement and identity preservation under these
mocked paths. It cannot prove native timing, memory reclamation, race freedom,
GPU completion behavior or gameplay/FPS improvement. Existing pool fixtures and
the independent trace producer suite own those narrower implementation checks.

The source remains untouched. Generated fixtures/binaries are bounded below
64 MiB in a task-owned temporary directory, removed on runner exit; native
execution additionally denies network and file writes when sandbox-exec exists.
No session data, game, installed runtime or persistent producer is involved.
