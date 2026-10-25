# Failed application pipeline cancellation

CPU regression fixture for the execution-thread D3D11 pipeline-resolution seam.
No Metal device, Wine runtime, game, installed binary, or live process is used.

```sh
python3 tests/vibewine/failed-pipeline/run.py
python3 tests/vibewine/failed-pipeline/run.py --source-ref 25eff5e13003d5e7f6b7cf6e1c76e9e6f3363e27 --expect-baseline-failure
python3 tests/vibewine/failed-pipeline/run.py --mutation unguarded
python3 tests/vibewine/failed-pipeline/run.py --mutation sticky
python3 tests/vibewine/failed-pipeline/run.py --mutation early-stage
```

The runner extracts byte-exact production `EmitDrawOP`, `EmitDispatchOP`,
`TrackRenderPipelineState`, four application PSO-resolution scheduling calls,
all fourteen render draw scheduling calls and both compute dispatch scheduling
calls. It extracts resource, vertex and stream-output upload scheduling calls,
and three actual `SimpleCommandContext` internal compute helper bodies. The
real `dxmt_command_list.hpp` stores, executes, replays and destroys these lambdas.
Generated `#line` directives identify production locations. Lightweight native
command/encoder/resource seams observe which work is scheduled; they do not
implement the cancellation being tested.

Checks cover successful execution of each route, first-resolution failure,
successful A then failed B (including repeated draws without resolving B again),
successful C recovery in the same encoder, and suppression of access tracking,
temporary allocations, indirect GS/TS marshal tasks, visibility and barriers.
Failed TS work is exercised with zero thread metadata under UBSan. Failed stage
resolution must not set stage-use flags or overwrite tessellation metadata, and
must preserve previous successful pass-wide stage flags and `last_pso`.
Queued upload calls remain effective during failure and their latest values
survive recovery without a shader switch or fresh encoder in the fixture.
The same command list is replayed against reused and fresh execution contexts;
render/compute/render passes and independent internal compute work are checked.

ASan and UBSan are enabled by default (`--sanitize none` disables both).
The baseline control extracts the old direct `EmitOP` calls, without inventing
new cancellation helpers, and must expose exactly stale-A render and compute
submissions. Compatibility-only mock failure fields are unused by old source.
Mutation controls remove both guards, make failure sticky across successful
resolution, or set the tessellation-use flag before the success check. Each
must trigger a named scenario failure; compilation errors are not accepted as
successful mutation detection. An unguarded TS route may additionally trigger
the expected UBSan division-by-zero diagnostic after earlier named failures.

Scope limits: the fixture executes the scheduling/execution seam, not whole
`PreDraw`/`PreDispatch`, recording-side dirty decisions, outer API zero-count
returns, shader compilation, pass merging, GPU resource residency, actual Metal
validation, or gameplay. The unchanged-shader recovery setup explicitly queues
production upload calls; it does not prove the outer recording logic selected
them. Resource encoders are mocked; the extractor rejects any new failure guard
inside their production implementations so that this seam cannot silently miss
a broader cancellation policy. Physical pass creation/reset uses mock layouts
with the real extracted `pipeline_failed = false` field initializers, rather
than compiling the full pass allocator. Focused passes establish this CPU seam
only, and do not establish installed or GPU behavior.

Data lifecycle: the test runner owns its temporary directory and removes it on
exit. Generated source, compiler output and receipts are derived test evidence,
not canonical user data. It captures no app/game data or credentials. Optional
`--keep NEW_DIRECTORY` preserves generated text, stdout/stderr and a source-hash
receipt, omits the binary, rejects existing destinations and enforces a 4 MiB
total cap. The caller owns retained evidence: retain for seven days or until
the associated review closes, whichever is later; explicitly held evidence is
excluded from deletion. Remove that exact directory manually after the hold
ends. No background producer or automatic retention cleaner is installed.
