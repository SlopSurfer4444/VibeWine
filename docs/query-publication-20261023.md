# Query publication correctness, 2026-10-23

The API device mutex does not synchronize with the encoding/finish threads.
Visibility begin/end metadata, issued sequence and accumulated results now share
one per-query `dxmt::mutex`. Timestamp completion uses a release store and a
single acquire load. The query mutex is never acquired while calling a device
operation, and the finish thread never acquires the API device mutex. Final
reference decrements use acquire/release ordering before destruction.

The existing D3D11 API already creates separate `Rc` query objects when an issued
generation is replaced. Encoder closures and readbacks retain their generation.
That ownership contract is preserved; no generation token or API signature was
introduced. Direct `reset()`/`begin()` reuse of the same underlying object is only
valid after its earlier readbacks have retired. `begin()` clears the issued
sequence, and unended queries cannot appear ready through the sentinel value.

An additional deterministic defect was found: a query drawing in chunk A and
ending in chunk B with no visibility offsets could remain pending forever.
An entirely empty query spanning chunks had the same problem. Flush now retains
a visibility completion whenever pending queries exist, even at zero result
count. Its zero-count constructor does not allocate a native buffer, and integer
range iteration avoids null-pointer arithmetic. Readback retirement publishes
the ending sequence only after previous sequence retirement, preserving earlier
contributions. Same-chunk empty queries retain their existing immediate-zero
behavior. The existing finish order remains readbacks/chunk reset first, CPU
coherence signal afterward.

The focused fixture extracts exact production query/API classes and flush
selection, with native allocation replaced by an owned CPU array. Candidate
ThreadSanitizer and AddressSanitizer/UBSan checks cover publication, concurrent
polling, overlapping/multi-chunk ranges, empty ends, reset, generations and
reference lifetime. The pinned pre-change source is expected to fail with three
publication races and four named empty-chunk completion assertions. The later
[integrated checkpoint](integrated-wave-20261023.md) records a complete matched
build and a real offscreen GPU guest, including a query ended in a later empty
chunk. Those results are separate from CPU fixture coverage and do not establish
game behavior, performance or a native GPU teardown-race reproduction.

## Integrated Windows device retirement

Source inspection identified a lifetime ordering defect: D3D11 device
destruction releases `d3d11_device_` before its owning `DeviceImpl`; the D3D11
pipeline cache uniquely owns pipeline objects, while queued encoding callbacks
capture raw pipeline pointers. `DeviceImpl` later destroys the command queue,
where encoding/finish threads are joined. The scheduler joining its own compiler
workers does not establish that encoding callbacks have drained.

The source-derived ownership fixture also establishes a final-release path on
the finish thread: a deferred command list retains a query whose private-data
interface retains the device. Capture reset recycles the list, clears the query
reference and can release the device before CPU coherence is signaled. A wait
inside the device destructor would self-wait; queue destruction on the same
thread would self-join. Weak ordinary device-child references do not exclude
this private-data path.

The Windows implementation now reserves a private thread-pool worker, work item
and module reference before publishing the device. The actual final private
reference transition dispatches through a protected destruction hook; default
COM destruction stays synchronous. The Windows device override only submits the
reserved work. Its callback drains the immediate context while compilation,
pipeline cache and command-list storage remain alive, calls idempotent queue
shutdown to join its workers and reset captures, then destroys the device. The
helper transfers its handles before object destruction, closes the work/pool
and releases the module after callback return. Setup failure unwinds acquired
resources before device publication.

The caller must retain its own DLL reference until `Release()` returns. The
extra module reference protects subsequent asynchronous retirement, not a caller
unloading code during its own DLL call. Production non-Windows guards preserve
the legacy native synchronous path; that path's teardown issue remains outside
this fix.

[Device teardown fixtures](../tests/vibewine/device-teardown/README.md) exercise
the exact COM/private-data/capture bodies, retirement helper and outer methods,
and the queue shutdown body in four focused suites with explicit platform seams.
The integration owner passed all four under ASan/UBSan and TSAN; restoring the
old direct-delete hook fails the required negative control. The integrated suite
uses a counted queue seam, while the separate shutdown suite extracts its actual
production body. Neither is a native Metal use-after-free reproduction.

## Remaining native teardown discrimination

A targeted native follow-up should gate the actual queued render/compute callback
immediately before its `GetPipeline` dereference, release final device/context
ownership on another thread, and observe pipeline destruction and callback drain.
Use a native ASan debug build or an explicitly source-bound combined queue/pipeline
fixture. The current suites do not exercise that raw-PSO callback. Release it only after
observing either a drain wait or pipeline destruction. A destroyed-live-token
assertion/ASan report before callback completion distinguishes the hazard from a
safe drain. Include completed and blocked compilation, pending query readback
and actual AppKit/main-thread dependencies. Keep every gate
bounded and release it on failure; a test that merely reconstructs member order
would be source evidence, not a reproduced native failure.
