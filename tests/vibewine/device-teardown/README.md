# Device ownership and retirement regressions

Run from the component root:

```sh
python3 tests/vibewine/device-teardown/run.py --sanitize address
python3 tests/vibewine/device-teardown/run.py --sanitize thread
python3 tests/vibewine/device-teardown/run.py --suite retirement --expect-bypass --sanitize address
# Optional explicit macOS targets (Rosetta executes the x86_64 fixture on arm64):
python3 tests/vibewine/device-teardown/run.py --arch arm64 --sanitize address
python3 tests/vibewine/device-teardown/run.py --arch x86_64 --sanitize address
```

`--suite` selects `ownership`, `shutdown`, `retirement`, `integrated`, or `all`
(the default). These are CPU fixtures, not a native Windows thread-pool or GPU
test. The integration owner passed all four suites with ASan/UBSan and TSAN,
and the old-direct-delete negative control, at the checkpoint described in
[the integrated wave report](../../../docs/integrated-wave-20261023.md).

## Production code exercised

The runner reads the current component sources, or `--source-ref REF`, and
extracts production bodies into a temporary build. It does not replace those
bodies with a handwritten reference algorithm.

| Suite | Production code | Substituted boundaries and observations |
| --- | --- | --- |
| `ownership` | COM public/private counters, pointers and private-data implementation; device-child classes; immediate-context AddRef/Release; command-list retained fields, Recycle/Reset; command-list storage and the ExecuteCommandList capture | COM/platform type scaffolding, allocators, pool storage and an observed device shell. Checks public context/list parent references, ordinary weak query children, and final device release from deferred-capture cleanup on a modeled finish thread. |
| `shutdown` | `CommandQueue::~CommandQueue()` and `ShutdownAndJoin()` | Actual `CpuFence::WaitState` cancellation with counted CPU encode/finish/listener threads, native command-buffer and encoding-context seams. Checks repeated/concurrent shutdown, blocked finish/drain work, join→submitted drain→discard→capture/listener/reset order, terminal Error versus pending/Completed/nil/unsubmitted buffers, and destructor reuse. |
| `retirement` | `DeviceRetirement`, the COM destruction hook, and the outer device's `Destroy`/`Retire` methods | Counted Windows thread-pool/module APIs, gated context drain and an observed queue. Checks API/worker-origin release, a base-typed final `ReleasePrivate`, idle/pending drain, caller-held mutex, setup failures and balanced handles/module references. |
| `integrated` | The ownership suite's real private-data/capture chain together with the actual retirement helper and outer methods | The same thread-pool seam, gated context drain and counted queue shutdown. Checks that capture cleanup returns before device destruction and that one independent retirement callback completes destruction. |

The shutdown facade follows the extracted source generation. Current source uses
its actual cancellation epoch; legacy `--source-ref` bodies retain their original
ready-fence wake path. The pending-worker test observes stop publication rather
than assuming listener destruction happens before joins. No production guard or
shutdown body is removed for a normal run; optional residency-set behavior remains
disabled in this CPU seam, while the universal submitted drain is exercised.

Four source-bound negative controls are available with `--suite shutdown` and
`--mutation skip-drain`, `discard-before-drain`, `wait-unsubmitted`, or
`skip-discard`. Each changes only the temporary extracted shutdown copy and must
exit1 with its exact order/ownership diagnostic. Crashes, timeouts, sanitizer
reports, and unrelated failures are rejected. The existing `--expect-bypass`
retirement control remains separate and unchanged.

The decisive ownership chain is a deferred command list retaining a public query;
the query's actual `SetPrivateDataInterface` retains the device. After application
references are released, capture reset recycles the list and clears its queries.
The last query release can then release the last device reference. No arbitrary
user callback is needed to reach this final-release origin. Observability shells
deliberately outlive the modeled device, so the test detects recycling after
destruction without dereferencing freed memory.

The default destruction hook remains synchronous. The `ownership` suite checks
that behavior; Windows retirement is exercised separately by extracting the
Windows helper and overrides. Production `_WIN32` guards leave the legacy native
non-Windows device teardown unchanged. These fixtures do not claim that path's
teardown hazard is repaired.

## Failure and unload boundaries

The retirement fixture injects module-reference, pool, minimum-worker and work
creation failure before publication. Each failure must submit no callback and
balance the resources already acquired. It also checks destruction of an unused
reservation. Successful final release submits preallocated work once; destruction
occurs after drain and queue shutdown on the independent worker.

The module seam models a caller retaining its own DLL reference through the
`Release()` call. After that call returns, the caller may drop its reference;
the retirement reference survives until the callback returns. Unloading the
caller's module reference while its DLL call is still executing is outside this
contract. The fixture models delayed unload and callback-safe handle closure;
it does not execute the operating system's loader or thread-pool implementation.

`--expect-bypass` changes only the extracted final-private-reference hook back
to `delete this`. It requires normal exit 1 and exactly
`FAIL deferred destruction required`; a crash, timeout, sanitizer report or
unrelated assertion is not accepted as that negative control. This is a targeted
mutation of the current source, not a complete historical renderer build.

## Evidence and limits

The runner emits `receipt.json` containing source root/ref, sanitizer mode,
per-suite outcomes, architecture/host, shutdown protocol, negative-control
selection, and SHA-256 hashes of production, fixture and generated inputs. Those hashes bind the extracted source
generation, not an installed DLL or native ABI. Keep the fixture revision with
the receipt when comparing runs. A pre-change ref without the new helper cannot
run the retirement/integrated suites.

Compilation is bounded to 60 seconds per suite and execution to 15 seconds.
Temporary files are removed automatically. `--keep DIR` retains generated source,
stdout/stderr and the receipt, excluding the executable; the integration owner
owns optional evidence retention. ASan mode also enables UBSan; macOS leak
detection is disabled and is not claimed.

The integrated fixture uses a counted queue seam; the actual queue shutdown body
is exercised by its separate suite. There is no actual Metal command buffer,
raw-PSO dereference, compiler task, AppKit callback, PE calling convention or
native GPU teardown race in these tests. The caller-mutex gate tests nonblocking
final release, not the AppKit event loop. The separate matched build/offscreen
GPU checkpoint does not turn this fixture into a native GPU use-after-free
reproduction or proof. Game lifecycle, presentation and controlled performance
remain separate integration checks.
