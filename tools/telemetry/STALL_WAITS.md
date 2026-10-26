# Short slow-scope capture recipe

Status: source-only recipe; no runtime is installed and no capture is launched
by these changes. Owner: existing diagnostic/session integrator. Use a producer,
controller and analyzer from the same integrated revision, retaining source and
built-library hashes in the private run summary. Source tests do not establish
which binary a game loads.

1. Through the existing session/launcher owner, bind the process PID/birth,
   candidate revision and loaded-library hashes. Before producer startup select
   `DXMT_CS2_TRACE_PROFILE=stall-waits` and the normal private trace directory.
   Profile selection lasts the process lifetime; rearming cannot change it.
   Set a finite `DXMT_CS2_TRACE_DETAIL_LIMIT` only when partial task/cache context
   or compiler stages104–107 are needed; zero/unset disables these probes.
   The maximum remains32768. No launcher or installed runtime change is implied.
2. Reuse the existing independent frame observer and explicit phase evidence
   from [SESSION_PHASES](SESSION_PHASES.md). Record the scene, route, render
   dimensions, warm/cold cache state, power and background load outside timing.
   Keep unknown phase transitions unknown. This profile excludes native/GPU
   streams and cannot measure FPS or calibrate itself against disabled tracing.
3. Inspect the exact mapping and save its device/inode, remaining lifetime
   slots and counters. Use an unused generation and explicit record/tail
   budgets. A separate short calibration in the intended scene must estimate
   actual retained slow-scope and context rate; its slots also remain spent.
   The50ms threshold does not guarantee a duration: nested scopes, many threads,
   teardown and task context can still fill the file. Never reset counters,
   recycle a generation or enlarge capacity to extend the experiment.
4. Reserve one metadata record, the finite detailed quota plus its one cap
   marker when enabled, calibrated nondetailed slow scopes and a tail reserve.
   Confirm these fit the remaining131072 lifetime slots and the experiment's
   retained-byte cap. Arm only for a short observed route:

   ```text
   cs2trace inspect ABS_FILE
   cs2trace arm ABS_FILE UNUSED_GENERATION DEV:INODE PLANNED_RECORDS TAIL_RESERVE
   ```

   Record route boundaries and a short observed completion tail. These are
   source-count admission budgets, not an automatic time limit or guarantee.
5. Stop once through the existing owner, retain its receipt, and make an
   immutable snapshot using the existing identity/hash binding procedure:

   ```text
   cs2trace stop ABS_FILE GENERATION DEV:INODE
   cs2trace decode ABS_IMMUTABLE_FILE GENERATION SNAPSHOT_DEV:INODE
   python3 tools/telemetry/analyze.py ABS_DECODED --output ABS_REPORT --expected-profile stall-waits
   ```

   Require structural completion, admitted-writer quiescence, stable counters,
   no dropped/uncommitted records and metadata95 schema1/flags4. Save both
   decoder and analyzer exit codes. Decoder policy acceptance alone does not
   establish a useful capture: metadata/context only, detail cap/stale-work
   markers, loss or missing stop evidence fail analyzer acceptance. Inspect
   boundary rejection counts separately; scopes crossing stop can remain
   missing even with admitted-writer quiescence. No absent scope is zero cost.
6. Read `stall_capture.slow_scopes` as individual **CPU wall** intervals at
   least50ms, including blocking/preemption.108/109/110 identify queue birth
   serial/chunk sequence/frame, with queue frame0 allowed. Duplicate type for
   one queue/chunk or conflicting frame/thread bindings fail acceptance while
   raw rows remain available. Observed phases must follow110 reset,108 idle
   sweep,109 ring reclamation without overlap; missing filtered phases remain
   unknown. Other payloads keep
   their existing source identity. Legacy Scope pointer values must not become
   birth serials or cross-process/frame joins. Nested scopes cannot be summed
   as CPU busy time. Detail context80–89/93–94 and compiler104–107 share the
   finite quota; the cap marker can appear before an already-admitted record
   in append order under concurrency. Missing task boundaries and fast compiler
   stages stay partial/unknown, including when one slow stage joins successfully.
   None supplies GPU busy time, complete frame attribution or FPS improvement.
7. Retain a compact result with exact file/generation/revision identities,
   source references, verification date, observed phase and uncertainty. The
   existing session integrator owns the mapping and private decoded derivatives:
   **7-day TTL /64MiB combined raw and decoded cap per experiment**, within the
   existing contract; **30-day TTL /4MiB results and minimal failure evidence**.
   Active mappings, active analysis and unresolved acceptance/recovery holds
   are excluded and need reviewed expiry. Admit no further bulk recording when
   it would exceed those caps. Cleanup remains manual through the existing
   session lifecycle after bounded exact-path inventory and preservation
   verification; this recipe creates no cleaner or additional retained store.
   Stop task-owned collection at acceptance through its owner.
