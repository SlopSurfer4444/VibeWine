# Compute prewarm demand scheduling regressions

Run `python3 tests/vibewine/compute-prewarm/run.py`. Default ASan/UBSan,
`--tsan`, `--arch x86_64`, `--case`, `--source-root` and `--keep` are supported.
One compiler runs at a time. Scratch sources/binaries are private, capped at
64 MiB and deleted after the run; core dumps are disabled. Compilation has a
60-second bound, execution 45 seconds and each fixture wait four seconds.
No runtime, game, Metal device, native cache or database is opened.

The runner extracts these complete production sections verbatim:

- `CachedSM50Shader::get_shader` public/speculative overloads;
- `AddComputeShader` and both compute pipeline lookup methods;
- the compute PSO constructor, factory and declaration;
- the pixel variant identity type.

It includes the production scheduler, scheduling policy, persistence and pixel
normalization headers. Existing scheduler controls also run in the default
aggregate: queued/running/blocked promotion, bounded class fairness, duplicate
admission, dependency completion races, retirement, compile failure readiness,
optional I/O failure, cancelled queues and worker shutdown/join.

Platform-only owners and compilation are mocked. The fixture PSO run seam returns
its shader dependency until ready, then itself, matching the production compute
PSO protocol. A factory hook admits unrelated speculative work between shader
and PSO admission to fix a deterministic order; it is outside the extracted
production code. The shader failure seam sets a terminal failure marker: this
proves scheduler readiness and no duplicate admission, not Metal error handling.

The added acceptance matrix runs with early compilation, demand scheduling and
backlog growth independently off/on. Eight active gated compiler tasks saturate
the same pool in every case; one worker is released. Early compute shader work
must remain behind actual consumer work with demand scheduling enabled. Disabled
mode must retain FIFO. Repeated early creation preserves one shader/PSO identity.
Real cache-hit lookup must promote an existing queued PSO ahead of unrelated
speculation, retain demand after repeated prewarm, and inherit demand through
its blocked shader dependency. Repeated completed lookups keep the same identity.
A factory throw leaves a variant key retryable; successful retry admits one task.
This does not claim recovery from allocation failure inside scheduler submission.

`--source-root` binds these same acceptance tests to another source tree. The
pre-fix two-argument factory is supported only for the old-source discriminator;
production methods and constructor are not rewritten. Case `compute` fails the
old eager-compute urgency and `retry` fails the old null-entry poison. Case
`promotion` fails a targeted candidate control that removes only cache-hit
promotion; the full old tree also fails this case, but eager demand admission
confounds the precise cause. Hashes and logs in `--keep` bind each
result to its exact source and test generation. No throughput/FPS claim follows.

Retained evidence belongs to the coordinating N2 task: logs have a 7-day TTL and
32 MiB cap, results/patches a 30-day TTL and 8 MiB cap. The task owner removes
expired task-owned evidence manually after verifying no active or hold reference;
active work, explicit holds and required recovery material are excluded. The
runner has no persistent producer or automatic deletion service.
