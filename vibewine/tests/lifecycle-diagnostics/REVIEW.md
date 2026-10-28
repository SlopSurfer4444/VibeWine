# Production diagnostic source review - 2026-10-28

A separate subagent reviewed the actual four-file production diff read-only,
then reviewed the final termination-target snapshot added after the first pass.
It made no production edits, builds, installation or live-process actions.
The same branch subsequently authored the separate Quit fixture; its production
review is independent of the production implementation, not independent authorship
of that fixture. Root separately reviewed the four-file production candidate.

Final review result: no actionable findings. The final target-snapshot review
checked valid, denied, invalid, current-thread pseudo-handle, channel-off and
saturated paths, raw status retention, failed-field clearing, admission before
query, and preservation of the caller's foreign handle.

The production review also checked:

- Disabled channels return before counter mutation; bounded CAS admission emits
  exactly one marker and never overflows its quota.
- Critical-section observations reserve before queries, close successful opens,
  compare returned Windows PID/TID and recheck owner without a dead-owner claim.
- Creation logs require final successful attribute processing and add no child
  TEB dereference after pthread creation.
- Termination result logging precedes the nonreturning self path. No logging
  was added to abort/exit signal cleanup.
- Quit records read request state before release and preserve reference flow,
  synchronous callbacks, refusal and current-process termination behavior.

Source bindings at final review:

- `dlls/ntdll/thread.c`: `993e627b438e8f3f4874e8a4c11d6eb6a90ac483176fb9ff1fbdcb3d67c2885e`
- `dlls/ntdll/unix/thread.c`: `870c049b0c9d995dd15158ad395763139280c7b72c9bc2589acb9b25e72f2d2b`
- `dlls/ntdll/sync.c`: `c44c989202fbe1ec66d65fc08368bbaf6540a2c0465c56142b03458dd2403382`
- `dlls/winemac.drv/dllmain.c`: `cb56a3447bb64d317da438d612bca847fff1360f82d19c044583e22b3094f47e`

Focused tests passed on native arm64 and translated x86_64 with ASan/UBSan:

```sh
python3 vibewine/tests/lifecycle-diagnostics/run.py --sanitize
python3 vibewine/tests/lifecycle-diagnostics/run.py --sanitize --arch x86_64
```

Each aggregate covers the four production admission functions under concurrent
quota contention and disabled paths, actual thread payloads and target-query
cases, six critical-owner schedules, and 60 Quit scenarios in 120 on/off runs.
The source verifier and `git diff --check` also passed. These observations do
not establish live timing neutrality, root cause, or a repaired Steam hang.
Root owns full-module build/package/install and final source admission.
