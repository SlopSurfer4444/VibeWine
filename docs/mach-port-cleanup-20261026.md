# Failed shared-texture creation: owned Mach send-right cleanup

`WMTMachPortDeallocate(mach_port_t)` releases one caller-owned send-right user
reference in the current process. Passing zero is a no-op. The caller must own
the reference being released; this is not a lookup, registry, bootstrap-unregister
operation or permission to destroy another owner's port.

The native implementation invokes `mach_port_deallocate(mach_task_self(), port)`.
The PE bridge passes a fixed four-byte port name at appended unix-call ordinal
150. Both ordinary/native and WOW64 tables preserve all preceding entries,
including the allocation-size operation at 149. The public C declaration and
existing `WINEMETAL_API` export machinery expose the function; this component has
no separate hand-maintained export list. Native builds compile the same thunk
and native table sources. A matched PE/native build is required.

The creation factory uses this operation only for a fresh shared texture whose
creation fails before the object is published. `Texture::allocate` also releases
a freshly created port if allocating or constructing its allocation wrapper
throws before the factory can own a guard. Successful creation, imported/borrowed
ports and existing bootstrap/keyed-mutex ownership are unchanged.

`tests/vibewine/mach-port-cleanup/run.py` extracts production functions and checks
private, test-owned native Mach rights and the thunk/ordinal contract. Its README
records the exact coverage and limits. Tests create no shared service, Metal
resource, installed runtime or game session. All test-owned receive rights and
remaining send references are released before exit. Matched full-library builds
and caller integration remain the root owner's verification responsibility.

Owner: transaction worker for the bridge and focused tests; root owns integration,
production texture-wrapper changes, commits, build and publication. Private test
sources, binaries and captures are temporary and removed on exit. Saved evidence
uses the existing wave's seven-day TTL and aggregate 64 MiB raw cap with active
holds and manual owner cleanup; no persistent producer or cleaner is installed.
