# Bounded D3D11 residency-set experiment

`DXMT_RESIDENCY_SETS` defaults to `0`. The experiment reuses the existing
D3D12 `WMT::ResidencySet` and command-queue wrappers. It is a source candidate;
CPU fixtures and syntax checks do not establish GPU correctness or FPS benefit.

## Admission and lifetime

The context hook runs only inside the existing raw `Buffer*` residency overload,
after `CheckResourceResidency`. It preserves the earlier access/hazard/Retain
operations. Only standalone owned GPU-read-only allocations and read requests
are eligible. D3D11 immutable buffers provide the read-only flag. Managed storage
is eligible: `GpuManaged` is a storage mode and those buffers are still created
with untracked Metal hazards. Counters use a separate overload and never enter
this hook despite the `GpuReadonly`/`NoTracking` flag alias. Textures, buffer
texture views, native-only resources, heap/CPU-placed/command-list-owned backing,
write requests, unsupported OS versions and failed set creation retain the
existing declarations.

The same exact native backing must appear in two distinct encoding chunks before
coverage can suppress a declaration. One chunk's repeated bindings cannot admit
a resource. Candidates retain their `BufferAllocation` identity and native
backing, and count against **all** limits: 256 held entries, 64 MiB total native
`allocatedSize` bytes and 16 MiB per entry. Zero, failed or excessive size queries
fall back; subtraction prevents integer overflow. Overflow never evicts an
inflight member to admit a new one. The metadata vector reserves 256 entries.
The native size getter runs only for newly observed backing. An exact retained
source owner/native-handle pair reuses its immutable charge; retirement and
drained clear erase that charge. A handle or owner match alone cannot reuse it.
This is a native backing allocation cap with at most 256 source owners. The
owners also retain CPU fence metadata and cached texture-view objects, so it is
not a 64 MiB process/RSS cap or a global GPU memory cap.

The encoding thread exclusively changes set membership. New membership is
committed before coverage returns and before the command buffer commits. Every
existing `CommandQueue::Retain` touches an admitted/candidate owner's last use,
including view/copy routes that keep ordinary declarations. Retaining the C++
allocation prevents pointer reuse from confusing that identity. Observation
sequence and last-use sequence are separate so access before coverage does not
defeat the stability filter.

Retirement runs at the beginning of a subsequent encoded chunk, after at least
32 completed idle sequences. Removal and set commit precede reference release;
GPU-incomplete backing is retained. This is a sequence-based TTL, **not** elapsed
time; a dormant queue can keep its bounded cache until further work or shutdown.
Opt-in shutdown waits outstanding attached command buffers after worker joins
before clearing set members. The default-off shutdown path adds no waits.

The bridge's membership calls have no failure result, so a successful set
creation does not provide a per-add NSError receipt. The fixture's injected add
failure checks the policy's fallback seam; it does not establish native error
detection that the bridge cannot expose. There is no new telemetry writer,
capture, file retention, service, background worker or global all-live resource
registry. Owner is `CommandQueue`; scope is its bounded candidate/member cache;
data class is transient allocation references and saturating aggregate counters;
cleanup is completion-safe retirement or drained shutdown; active/incomplete
members are held within the same cap.

## Checks

```sh
python3 -B tests/vibewine/residency-sets/run.py --sanitize --negative-controls
python3 -B tests/vibewine/residency-sets/compile.py --compile-db /path/to/compile_commands.json
```

`run.py` includes the actual production policy header and extracts the queue's
admission method, native-reference wrappers, membership backend and shutdown drain
and OS/set-creation initialization verbatim. Native allocation/queue calls are
observable CPU doubles. Tests cover
same-chunk stability, managed immutable admission, rename identity, native-size
and count caps independently, maximum-integer byte accounting, stale sequences,
owner mismatch, alias/copy retirement, incomplete work, failure retry, source
identity/native lifetime, writable/heap/placed/command-list exclusion, unsupported
set fallback, removal commit order and drained shutdown. Default-off calls perform
no native size/membership operation. Negative controls deliberately remove these
protections and must fail assertions. Temporary compiled outputs are removed.

`compile.py` reuses a retained Meson compilation database, reroots its source
paths to this checkout and checks actual queue/context translation units in all
four residency/idle-retirement flag combinations. It produces no object files.
This is syntax evidence, not linking or matched PE/Unix/native ABI acceptance.

Remaining acceptance: matched bridge/PE build; actual Metal buffer data goldens
under raw/view/copy alternation, renames and in-flight shutdown; supported-OS and
set-creation fallback; capture/validation of retained explicit hazards; controlled
baseline/candidate/reversal CPU, GPU, frame-time, memory and latency comparison.
Reject if churn, bookkeeping or paging increases, rendering changes, or the
critical interval does not improve beyond baseline variation.

## Checked reuse

The same tree's `src/d3d12/d3d12_device.cpp` membership wrappers and
`d3d12_command_queue.cpp` queue attachment avoid a second bridge implementation.
[Apple's set guidance](https://developer.apple.com/documentation/metal/simplifying-gpu-resource-management-with-residency-sets)
confirms queue sets attach when a command buffer commits; sets provide residency,
while access synchronization remains the application's responsibility.
[slang-rhi's Metal device](https://github.com/shader-slang/slang-rhi/blob/main/src/metal/metal-device.cpp)
uses queue attachment and a useResource fallback switch, but does not provide this
bounded D3D11 stability/retirement policy.
[utmapp/d3dmetal-native](https://github.com/utmapp/d3dmetal-native) was also checked
as a smaller DirectX/Metal implementation; replacing DXMT's resource ownership
and bridge with its separate D3DMetal-based pipeline would add integration work.
No external code was copied; the small policy is local LGPL-2.1-or-later code.
