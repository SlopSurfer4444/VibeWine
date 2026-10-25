# Demand-aware compiler scheduling, 25 October 2026

This default-off source experiment changes compiler task admission, not the
compiler output or draw behavior. `vibewine_demand_scheduler` defines
`DXMT_DEMAND_SCHEDULER`; an ordinary build keeps the FIFO/continuation-first
scheduler. No shader/cache compatibility epoch changes. Installed runtimes,
games, saves and cache databases are outside this source change.

## Source and choice

The checked primary design is [DXVK's compiler workers at
`cbd2206d72d3d9dd9941d5d868bb03a858e85f9b`](https://github.com/doitsujin/dxvk/blob/cbd2206d72d3d9dd9941d5d868bb03a858e85f9b/src/dxvk/dxvk_pipemanager.cpp),
with its [zlib/libpng license](https://github.com/doitsujin/dxvk/blob/cbd2206d72d3d9dd9941d5d868bb03a858e85f9b/LICENSE).
DXVK supplies the demand/normal/speculative queue idea. Its Vulkan pipeline
objects, worker subsets and OS priorities are unsuitable for a direct transfer.
This is a new DXMT implementation; no DXVK source or license text is imported
or relabeled. DXMT's existing file/component notices remain authoritative.
The neighboring-source audit also inspected the small
[Digger1955/dxvk-gplall async path](https://github.com/Digger1955/dxvk-gplall/blob/e9a8af02fa6412a4cd30d7abb7cc8c99c3c228b4/src/dxvk/dxvk_graphics.cpp):
returning an empty pipeline while work compiles can omit required draws and is
not used. Reusing the existing task scheduler, continuation and detached-write
protocol avoids a second executor or Vulkan-specific lifetime model.

## Admission and ownership

All four real PSO creation paths and their public shader variant lookups request
demand urgency. The four `CS2LAB` early pixel variants request speculative
urgency. A later real PSO lookup promotes an existing speculative variant;
it does not create or submit another task. Promotion is monotonic and carries
through a blocked dependency chain, including a dependency submitted after its
waiter has blocked. Generic scheduler clients retain normal urgency by default.

The opt-in path owns one scheduler record per pending task identity and at most
one ready queue node per task. One scheduler mutex protects admission, priority,
dependency edges, completion and close. Promotion only looks up identity in
that map: completed/absent tasks are a no-op and are never dereferenced or
resubmitted. New `submit` still requires a live caller-owned task; address reuse
does not grant an old pointer identity across task lifetimes.

Completion first detaches optional persistence, wakes recorded dependents and
erases indexed/queued dependency references to the completed task, then publishes ready.
Only the copied trace and independently owned persistence callable survive.
Task owners must retain returned dependencies while a task can inspect/block on
them; production pipeline-cache ownership already outlives scheduler join.
This does not add reference counting for arbitrary caller-supplied raw tasks.

Demanded work normally runs first. After eight demanded admissions, one queued
normal or speculative task is admitted. Those two lower classes alternate when
both are present, so a continuously queued lower class receives an admission
within eighteen starts under a sustained demanded backlog. This is an admission
bound, not a completion-time bound for non-preemptible compiler calls. When all
workers are actively compiling speculation, demand waits until one returns.
There is no reserved idle worker, worker-count increase, OS-priority change,
compile cancellation, substituted pipeline or omitted draw.

The worker growth option retains its original runnable/idle calculation and
maximum. The disabled path retains its original FIFO and continuation-first
code and creates no policy queue/map allocations. Existing trace events 80-89
and submission generation/serial bindings are retained; promotion does not
create a new submission generation or a logging producer.

Shutdown closes admission under the worker mutex, joins active compile workers,
and keeps queued compile tasks cancelled as before. Only after compiler join
does the existing bounded persistence owner discard optional pending writes
and join its active write. Task/pipeline/shader ownership is unchanged.

## Data and evidence lifetime

The owner of scheduler metadata is its `PipelineCache::scheduler_`. Scope is
that cache's pending task identities, ready nodes and blocked edges; data class
is borrowed in-memory execution metadata, with no bytecode copy or disk output.
There is at most one record/node per live pending task identity and one edge
per blocked task, including identities awaiting their first submit. Records
expire before ready publication or after joined shutdown; running/blocked work
is held until completion/shutdown. This introduces no independently growing
collector and no new task creation. The pre-existing cache's number of live
tasks remains the cardinality bound; there is no new global byte cap on the
pipeline/shader cache. Introducing one by rejecting demand would break draw
correctness. The disabled policy owns no allocated records.

CPU fixture scratch belongs to its temporary test runner: maximum 64 MiB,
seven-day TTL if retained for diagnosis, with active/analysis/recovery holds.
Normal cleanup removes the exact private temporary directory on exit. Curated
source/tests/decisions are durable repository results, not disposable scratch.
No runtime telemetry retention, cache writer limit or cleanup policy changes.

## Verification boundary

`tests/vibewine/demand-scheduler/run.py` compiles the production scheduler and
policy with CPU-only counted worker/platform shims. It compares saturated FIFO
admission with the gated path at the same worker capacity, then exercises
promotion, dependency propagation, fairness and lifecycle controls. The
existing `compiler-persistence/run.py` verifies the unchanged disabled scheduler
and actual shader-task detached write/error paths. Actual compiler cold/warm
cost, complete matched-build acceptance, GPU/gameplay and end-user latency
remain integration-owner checks. An ordering improvement in this fixture is
not an FPS result.

Observed on the 25 October candidate based on DXMT `c7e4ab455d7a666768ea8cd1f7e719d879bd1218`:

| Command | Observed result |
| --- | --- |
| `python3 tests/vibewine/demand-scheduler/run.py` | Native ASan/UBSan passed; repeated by the implementation owner |
| `python3 tests/vibewine/demand-scheduler/run.py --tsan` | Native TSan passed |
| `python3 tests/vibewine/demand-scheduler/run.py --arch x86_64` | Rosetta ASan/UBSan passed |
| `python3 tests/vibewine/demand-scheduler/run.py --arch x86_64 --tsan` | Rosetta TSan passed |
| `python3 tests/vibewine/compiler-persistence/run.py` | Existing scheduler/cache/shader-task ASan/UBSan controls passed |
| `python3 tests/vibewine/compiler-persistence/run.py --tsan` | Existing scheduler/cache/shader-task TSan controls passed |
| `git diff --check` | Passed |

With both backlog-growth settings, eight active speculative tasks were held on
explicit barriers. Four queued speculative tasks preceded a demand task. When
exactly one active worker was released, disabled FIFO selected speculative
index 0; the enabled policy selected demand index 4. All queued tasks then
completed exactly once; both pools stayed at eight workers. This is an observed
admission-order discriminator, not a measured native compiler time saving.

The fixture also compiled both complete `CachedSM50Shader::get_shader` overloads
extracted verbatim from the changed production source. Public lookup promoted
the exact existing speculative variant, repeated public lookups preserved one
compilation/submission identity, ordinary public creation had demand urgency,
and speculative lookup remained behind demanded work. Direct source review
confirmed all four PSO submissions request demand and `AddPixelShader` alone
uses the speculative overload for its four early variants.

An initial Rosetta mock failure exposed legal moved-from `std::function`
capture retention in the platform standard library. The fixture now explicitly
clears its moved source, matching production task detachment; all assertions
and four-second predicate bounds were retained. The final runner disables
core dumps, checks its private temporary files remain below 64 MiB, and removes
them on exit. No failing assertion was relaxed.
