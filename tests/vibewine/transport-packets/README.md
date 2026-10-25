# GS/TS marshal transport packets

Disposition: retain a **default-off experiment** for fixed GS/TS marshal packets.
`-Dvibewine_marshal_packets=true` sets `DXMT_MARSHAL_PACKETS=1` consistently for
PE/native compilation. The W4 batching reference bypasses these packets. No
native interpreter, export, opcode, shader argument layout, allocator, resource
ownership or queue behavior changes.

Each nonempty GS or TS stage previously emitted two individual residency calls
per task plus four individual marshal-prepare calls. The enabled path uses one
fixed read/write residency pair per task, followed by one fixed
PSO → argument-buffer bind → point draw → nil unbind packet. Both use the
existing synchronous opcode 38 and node ABI. Per stage with N>0 tasks, entries
change from `2*N+4` to `N+1`; native commands stay `2*N+4` in identical order.
A stage with zero tasks invokes neither helper. Ordinary singleton command and
fence wrappers are untouched; there is no generic batching bookkeeping on them.
A single marshal task already contains six commands, and now uses two entries.

The CPU-work basis is stronger than a command-count claim. Every consumed
command field, including each `next`, is assigned once, as in the old wrappers;
only its pointer target changes. There is no packet zero-fill, heap allocation,
extra task traversal, search or dedup. Each removed entry also removes the real
PE thunk's two-field parameter-frame construction and its native entry/walker
setup. The tested 64-bit parameter frame is 16 bytes, so N+3 complete frames are
avoided per nonempty stage. Explicit node storage is bounded at 64 bytes for a
resource pair and 152 bytes for marshal prepare, independent of task count.
This adds a larger live stack footprint than a single wrapper node and can
change cache behavior; it is not proof of a stable CPU-time or game benefit.

Task argument writes and terminal markers remain in the original loop. GS
marshal precedes TS resources/marshal; the existing buffer barrier follows both
and precedes recorded application commands. Waits remain before setup, and
updates/endEncoding remain after the application stream. Physical residency
dedup still sees only the recorded list. Visibility/query commands are not
removed or moved. The new helpers borrow the same handles; all node pointers
are consumed synchronously before their stack storage expires.

## Production-bound CPU qualification

```sh
python3 -B tests/vibewine/transport-packets/run.py
python3 -B tests/vibewine/transport-packets/run.py --arch x86_64
python3 -B tests/vibewine/transport-packets/run.py --benchmark
```

The runner extracts the complete production render emission after encoder
creation and the exact public GS/TS, resource-pair and private packet methods.
It uses real Metal.hpp/wire layouts/constants/FenceSet and the exact PE thunk.
The **complete unmodified production native render interpreter** is compiled
into another translation unit. Its Objective-C receiver records calls instead
of creating a Metal device or encoding GPU work. Fixture, thunk and receiver
compile separately with `-fno-lto`, preventing a fake boundary from disappearing
through cross-unit inlining. No Wine, GPU, native renderer edits or installation
is involved. Foundation/Metal headers and frameworks provide types and the CPU
Objective-C runtime only.

Native arm64 and x86_64 ASan/UBSan each passed 400 scenarios × 10 variants.
Cases cover GS/TS counts 0/1/2/17/129, both stages together, stream output flags,
nil/non-nil encoder and marshal pipeline handles, real stage-priority fence
iteration, application PSO/draw and visibility mode commands. The receiver also
validates all task argument fields and the final-task marker when its valid
marshal pipeline executes. Original recorded topology is checked afterward.

Default/off/on, combined render/compute fusion plus residency dedup, and W4
reference must preserve the complete native event type/payload/order sequence.
Enabled entry counts must save N+3 per nonempty stage; combined render fusion
may additionally save its ordinary setup entry. Five connected controls must
fail the command/count contract: missing unbind, wrong resource usage, wrong
marshal instance count, barrier moved before marshal, and an equal-command
bypass that loses all entry savings. Compilation/sanitizer failures are not
negative-control acceptance. Source and generated component hashes are printed.

Direct helper boundary inputs such as UINT32_MAX offsets or nil argument
buffers/resources are not exhaustively tested here; source assignment matches
the original wrappers. This does not exercise GPU GS/TS execution, real query
publication/completion, Metal error behavior or game compatibility. Those remain
matched-build/GPU checks for the integration owner.

## Count and cost observations

For GS-only whole render emission without fences (including unchanged outer
setup, barrier, application-list call and encoder end), native command counts
remain identical:

| Tasks | Opcode-38 entries off / on | Native command events |
| ---: | ---: | ---: |
| 0 | 2 / 2 | 6 |
| 1 | 9 / 5 | 19 |
| 2 | 11 / 6 | 21 |
| 8 | 23 / 12 | 33 |
| 32 | 71 / 36 | 81 |
| 129 | 265 / 133 | 275 |

The optional release CPU proxy uses `-O2`, three alternating A/B orders, seven
samples per executable/task count, 12,000 iterations per sample, and reports
median host nanoseconds per whole emission. It includes native-interpreter and
CPU receiver-selector work, not Wine transition or Metal driver/GPU latency.
No idle-machine, affinity or frequency control was established. All observed
medians from the accepted native run are retained below (off/on in ns):

| Tasks | A: off / on | B, reverse execution order: off / on | C: off / on |
| ---: | ---: | ---: | ---: |
| 0 | 83.42 / 77.23 | 101.31 / 73.28 | 103.63 / 80.14 |
| 1 | 247.72 / 227.68 | 364.33 / 240.34 | 1545.19 / 193.40 |
| 2 | 494.74 / 333.90 | 550.58 / 506.47 | 670.31 / 223.63 |
| 8 | 1739.71 / 530.62 | 1829.34 / 518.06 | 13310.70 / 374.32 |
| 32 | 1336.94 / 1789.12 | 4630.56 / 1130.84 | 14528.90 / 880.18 |
| 129 | 7119.32 / 3833.49 | 11038.60 / 16897.60 | 15542.20 / 3053.28 |

The large variance and explicit regressions at 32/129 tasks prevent a stable
speedup estimate. These timings do not promote the option into a default or an
FPS result. The candidate's credible reduction is removal of fixed transport
CPU work while preserving the command work; representative workload frequency,
real bridge timing, matched GPU validation and controlled reversal are still
needed before adoption. No favorable-only rerun was used to select a result.

## Fence-family disposition

Production fence batching is **deferred**. The independent bounded prototype
under `fence-assessment/` preserved scalar-oracle semantics for 3,340 cases and
proved count reductions, but added small-count bookkeeping and had unstable
CPU timing. In particular singleton overhead has no crossing to amortize.
It is assessment evidence only and is not connected to the production option.
Final production wait/update blocks are byte-identical to base a21579fb and to
the assessed pre-W4-gate context version. No fence wait/update is combined with
marshal, application work or encoder completion.

## Scope and data lifetime

These sources own fixed CPU fixtures. Each run uses one private temporary
source/object/binary directory removed on exit. Compile/qualification timeouts
are 90/30 seconds; each release benchmark process is capped at 45 seconds.
Scenario/task counts bound child output; only compact receipts are printed by
the runner. No persistent collector, production data producer, retained packet
queue or retention behavior is added. Root owns any saved receipts/patches under
its existing campaign contract. Full build, native combination testing, GPU,
installation, runtime selection and publication remain with root.
