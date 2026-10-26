# Resource initializer transaction repair

The allocation-failure proof in `tests/vibewine/initializer-failure-proof` is a
historical diagnostic. This repair adds a separate correctness gate in
`tests/vibewine/initializer-transaction`; the historical expectations are not
weakened or reinterpreted as passing behavior.

Initialization now returns `InitResult { status, sequence }`. `Success` means the
complete operation has been published into the current initializer generation;
only then is `sequence` meaningful. `OutOfMemory` and `SubmissionFailed` carry no
GPU wait value. Consumers must check the result before publishing a D3D resource.

Each operation checks CPU capacity for every command and a worst-case 256-byte
allocation-tracker spill. Capacity flushes submit only the valid existing prefix.
Upload paths reserve their one or two staging spans with the checked ring batch
API before retaining destinations or reserving command nodes. Both depth/stencil
commands are filled and linked as one group. A failed staging reservation leaves
no partial ring allocation or encoder-reachable command. One additional attempt
is permitted only after submitting a valid prefix or observing newer completion.
There are no retry loops or hidden flushes inside an unpublished reservation.

A zero-buffer replacement is a local native candidate until its fill and texture
copy are complete. Failure preserves the cached handle, size and pending commands.
Before replacing a nonempty cache, pending work is successfully encoded/submitted
while the old buffer is still owned. The replacement fill and copy are published
together. This relies on the existing native command buffer's retained-reference
semantics for submitted resources; CPU fixture evidence alone does not establish
real GPU lifetime.

Null initial CPU storage, command queue, or event leaves initialization safely
unavailable and returns `OutOfMemory`. Nil command-buffer/render/blit-encoder
creation preserves the pending prefix, references, staging storage and sequence.
Internal submission reports `SubmissionFailed`; explicit `flushToWait()` throws
`MTLD3DError` before publishing a queue/event wait. No failed attempt creates an
empty successful submission or a sentinel wait. Destination-size/source-pitch
arithmetic and CPU alignment arithmetic are checked before reservation.

`CommandQueue::CommitCurrentChunk()` completes this fallible initializer preflight
before advancing the consumer event sequence or changing unpublished chunk
metadata. A failure leaves those values, publication counters and statistics
unchanged; a successful retry uses the next unconsumed event. The complete-method
`initializer-queue` fixture checks both asynchronous/synchronous encoding paths
with debug enabled/disabled and rejects the old ordering.

The focused fixture compiles the actual initializer, tracker, ring and Metal C++
wrapper with ASan/UBSan against a mocked native C boundary. Its README describes
coverage and seams. This establishes CPU transaction behavior, not installed
runtime behavior, actual GPU completion, game compatibility or FPS. Asynchronous
Metal device/command-buffer failure after a successful commit is a separate
liveness problem; this change does not invent a device-lost protocol.

Owner: initializer transaction worker; integration, index, build, native GPU
validation and publication owner: root. No installed runtime, game or VPN changes
are part of this patch. No persistent producer or new application retention is
introduced. Test scratch is private and removed on exit; explicitly retained
receipts follow the existing wave contract: seven-day TTL, combined 64 MiB raw
cap, active evidence holds and manual owner cleanup. Existing source/build holds,
40 GB build cap and 20 GiB free-space reserve remain controlled by root.
