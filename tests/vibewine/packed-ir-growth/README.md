# Optional packed shader IR growth

Meson `-Dvibewine_packed_ir_growth=true` enables `DXMT_PACKED_IR_GROWTH=1` in the
existing common PE/native experiment map. The default is false/0. It changes
`PackedVariantList::ensure_capacity` from 1024-byte increments to
`max(required, capacity + floor(capacity/2))`, retaining the 1024-byte initial
floor. A near-SIZE_MAX addition falls back to the exact checked requirement.
The existing allocator, packed tags, offsets, payload representation and visitor
API are reused. This is an allocation/copy experiment, not a new allocator/cache.

Default and explicit 0 preserve every representable legacy allocation size.
Safe rounding replaces the linear search for that same size. Both modes now
check append tag/alignment/payload/trailing additions and allocation success
before copying, freeing, or mutating existing data. AIRCONV uses `-fno-exceptions`:
unrepresentable layout/capacity and allocation failure call `std::terminate`.
There is no recoverable OOM return or caller/exception ABI change.

The opt-in memory tradeoff is explicit: once above the initial floor, capacity
is below 1.5 times the required/occupied bytes. This can retain almost 50% extra
slack for the existing shader lifetime; it does not bound total compiler IR,
block count or process memory. While reallocating, both old and new buffers are
live, so above the small floor their combined backing is below 2.5 times the
incoming requirement. The feature does not shrink buffers or evict live IR.
The 1.5 factor was selected over doubling to reduce retained slack at the cost
of more allocation/copy work. Run with the option off if memory pressure harms
the workload; no FPS improvement follows from the source-level work reduction.
The ownership/lifetime contract is recorded in
[the existing wave contract](../../../docs/fps-wave-20261025.md).

Run both focused CPU configurations:

    python3 tests/vibewine/packed-ir-growth/run.py --arch arm64
    python3 tests/vibewine/packed-ir-growth/run.py --arch x86_64

A fresh Git worktree can have an unpopulated DirectX header submodule. Pass
`--directx /absolute/path/to/existing/mingw-directx-headers` to reuse a populated
checkout read-only. The runner never installs or initializes dependencies.

The runner compiles the complete actual production template and actual
`dxbc_instructions.hpp`/`InstructionList`, with AddressSanitizer,
UndefinedBehaviorSanitizer, warnings as errors and production `-fno-exceptions`.
Only generated temporary copies replace the template's actual malloc/memcpy/free
calls with counters and expose a small private-state adapter. There is no
production instrumentation or allocator hook. The unchanged instruction header
uses the same generated template. Every append independently checks packed size;
copy interception checks the exact occupied-byte count; visitors verify type,
order, count, alignment and payload bytes across growth. Coverage includes
12,000 mixed trivial records (alignment 1/8), a payload larger than the initial
capacity, no-allocation append, `any` early stop/miss, and 2,048 real
Nop/Emit/Cut/Sync instructions including Sync payload fields.

Eight bounded subprocess cases per default/off/on mode cover first/later OOM,
overflow in tag addition, payload alignment, payload size, trailing alignment,
growth arithmetic, and the largest representable safe growth boundary. Forced
allocator failure prevents all giant allocations. A terminate handler observes
unchanged pointer/capacity/size, no copy/free, unchanged live backing and the
original visited payloads before exiting with the expected code. Synthetic sizes
are restored only after state observation to allow safe payload readback; these
injections do not pretend to be valid live allocation sizes or recoverable OOM.
The default safe-rounding boundary and enabled near-SIZE_MAX fallback are both
exercised. Existing over-aligned-payload support and copy-assignment hazards are
outside this growth change.

The historical production header at `96940d73ac28f57fe12df832d246d3e84965f2cd`
passes the same normal payload corpus. Default/off must equal its exact allocation
sequence and work/size metrics. The historical work fails the same admission
that requires enabled allocation count below one eighth and copied bytes below
one sixteenth of baseline, so capacity alone cannot pass the optimization check.

On 26 October 2026, the sanitizer corpus measured:

| Actual production work | Default/off/historical | Enabled |
| --- | ---: | ---: |
| Occupied packed bytes | 384,000 | 384,000 |
| Allocations | 375 | 16 |
| Bytes copied during growth | 71,802,000 | 894,504 |
| Final retained capacity | 384,000 | 448,398 |
| Peak old-plus-new backing | 766,976 | 747,330 |

The enabled run retains 64,398 extra bytes in this corpus. Its lower observed
transient peak does not guarantee lower peak or retained process memory in other
workloads. These counters establish allocation/copy work and exact visitor
preservation; they do not execute LLVM/AIR conversion, compare generated shader
outputs, or establish full-build, Metal, game, wall-time or FPS behavior.

Fixed fixtures belong to this test directory. Each invocation owns a private
temporary directory, with a 64 MiB aggregate scratch cap and an 8 MiB maximum
actual intercepted allocation. Each child has a 30-second timeout and compiler
invocations a 120-second timeout. TemporaryDirectory removes scratch on normal
exit/errors; hard-killed leftovers need manual ownership/process/hold validation
before deletion. No runtime, game, installed files, cache, service or Git index
is touched. Retained raw reports follow the root wave's seven-day/64 MiB contract;
active/analysis/recovery holds take precedence. No cleaner is installed.
