# Ring recycle economy and held queue failure contract

The production change replaces `push_back(move(front)); pop_front()` with
`splice(end, fifo, begin)` when recycling a completed standard ring block.
FIFO order, strict `< coherent_id` reuse and metadata updates remain the same.
Recycling preserves the `Block` address and performs no list-node allocation.
This is a ring bookkeeping improvement; no upload, flush, GPU or FPS gain is
claimed.

Run `python3 tests/vibewine/queue-failure-contract/run.py --sanitize`.
For the old-fail control use `--source-ref 8d0cef8d --baseline --sanitize`.
The exact production ring body measures 100 list allocations for 100 completed
block recycles before the change, and zero afterward. The candidate also writes
through the retained block reference after all recycles under ASan/UBSan.
Existing `ring-retention`, `ring-checked` and `reftracker-retirement` sanitizer
fixtures cover grace boundaries, pending lifetimes, checked allocation and
actual finish/reset/Retain behavior.

## CPU18 remains held

This test intentionally requires the existing failure behavior to remain
detectable. A green runner means the negative finding reproduced; it does not
mean allocation or device failure was repaired.

The runner snapshots and hashes production sources before extraction. Complete
production `AllocateCommandData`, `moveonly_list`, host ring and host block
allocator bodies execute with a controlled malloc-null seam, small mock queue
fields and a 64-byte block. The child must fail with a sanitizer diagnostic
attributed specifically to null placement construction in the production
consumer. This is not host OOM or a full production CommandQueue/COM execution.

Complete production `TimestampReadbackCBuf` and `TimestampQuery` bodies also
execute. A synthetic command buffer returns timestamp 123. After constructing
the readback, a controlled `bad_alloc` unwinds it before any GPU commit; the
query becomes ready with 123. A normal destructor control publishes the same
value. This proves the destructor-publication hazard, not an end-to-end
`flushCommands` allocation failure or native Metal failure.

## Required complete failure contract before implementation

Source binding: base `8d0cef8dbcc8d1efbd0db97d4a0aee9e86d592d0`; runner receipts
bind the exact inspected production bytes. Queue and query sources are unchanged
by the ring patch.

1. **Terminal, first-failure queue state.** Keep failure/cancellation separate
   from the genuine completion watermark. Stop admitting and retrying commands
   after allocation, encoding, nil-command-buffer or native Error failure.
   The queue's allocation methods currently promise usable memory, and their
   callers immediately construct/write objects. Empty tuples/null returns are
   insufficient. Throwing out of COM void methods or destructor/retirement
   callers is also insufficient.
2. **Cancelled waits return failure.** CPU coherence is an allocation-reuse and
   Map authorization frontier, so freeze it at the last successfully completed
   contiguous sequence. Never signal the failed sequence or a sentinel to wake
   sleepers. Separate cancellation predicates must change the exact atomic
   observed by blocked admission/backpressure, CPU-fence and frame-latency
   waiters. `notify_all()` on an unchanged atomic is insufficient.
3. **Consumers must consume that failure.** Adapt staging Map's retry loop
   (`d3d11_context_imm.cpp:267-309`), `WaitUntilGPUIdle` (`:629-638`) and its
   noexcept retirement/destructor consumers (`d3d11_device.cpp:1316-1322`,
   `d3d11_swapchain.cpp:252`), Present's returned HRESULT
   (`d3d11_swapchain.cpp:1110-1112`), GetData polling/Flush (`context_imm.cpp:515-520`)
   and hardcoded `GetDeviceRemovedReason` (`d3d11_device.cpp:695-697`). Define
   device-loss reporting for shared fence/event callers as well; registered
   external Win32 events cannot be silently abandoned.
4. **Publication only after verified Completed.** The finish path currently
   logs native Error then follows ordinary readback/reset/coherence publication
   (`dxmt_command_queue.cpp:320-349`). Readbacks must default to nonpublishing
   and have an explicit successful-completion publication operation. Discard
   releases ownership without issuing query values. Changing only chunk reset
   misses local readbacks in `flushCommands`: those exist at
   `dxmt_context.cpp:948-957` before later argument allocations (`:1103`, `:1134`)
   can unwind. Visibility and timestamp destructors currently issue results
   unconditionally (`dxmt_occlusion_query.hpp:192-195`, `:291-305`).
5. **Lifetime follows real retirement.** Keep the native command buffer alive
   through timestamp-readback destruction (its buffer reference is borrowed).
   Stop the encoding owner before discarding unsubmitted work. Drain actually
   submitted buffers before releasing resource references, command/tracker
   borrowed storage and backing rings. Current shutdown drains only the opt-in
   residency-set branch (`dxmt_command_queue.cpp:120-135`); this must become a
   universal failure-lifetime rule. A failed queue is not resumable without
   repairing retain deduplication: `retainAllocation` marks the sequence before
   `Retain` can fail while allocating tracker spill (`dxmt_context.cpp:464-466`).
6. **State the native-wait limit.** Existing `waitUntilCompleted()` and shutdown
   joins are blocking, and this audit found no native cancellation mechanism.
   Bounded cancellation can cover CPU waiters after an observed failure; it
   cannot promise recovery from an arbitrary GPU hang or unsatisfied foreign
   GPU fence without further native design/proof.

Acceptance for a future full implementation requires actual consumer tests for
nil command buffers, native terminal Error, allocation/encoding failure before
and after local readback construction, blocked Map/Present/backpressure and
query/fence callers, frozen true coherence, no retry after retain failure, and
correct reference/storage retirement. Normal completion controls must still
publish and recycle exactly once. No new failure path is introduced here.

## Other economy hypotheses

`ResourceInitializer::flushToWait` already returns zero after observing upload
completion. Repeated nonzero upload-event waits across FIFO command buffers
may be redundant, but removing them needs public Metal ordering/visibility
evidence and an owned native discriminator. No such proof was produced here,
so no wait/upload/flush behavior was changed.

## Ownership and retention

Owner: N6 queue branch, integration consumer: root campaign owner. Scope: this
test and the root-selected `outputs/next-wave-20261028/queue` receipts. Data:
source hashes, bounded test logs and curated findings; no live capture or
background producer. Temporary compiler sources/binaries auto-delete on runner
exit. Retained raw logs: 7 days, 32 MiB; curated results: 30 days, 8 MiB.
Cleanup is manual by exact owner/path after checking active integration,
unresolved findings, selected recovery and explicit holds; these holds are
excluded. This documentation installs no cleaner or enforced quota.
