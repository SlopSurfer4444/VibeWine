# Explicit session phases and offline memory report

This is a small stdlib extension of the existing `whole-session-monitor/monitor.py`
collector and `tools/gameplay/lifecycle.mjs` evidence convention. It starts no
collector, attaches to no game, sends no inputs and polls no screenshots. It
reuses the monitor's concatenated Metal JSON gzip, process/system events and
PID/birth binding. The lifecycle's operation names (`observe`, `classify`,
`timed.run`) are not themselves scene evidence; a controller must supply its
actual classification/observation. No external code or dependency is needed.

## Write a genuine boundary

After actually observing a scene outside a timed capture, the session owner may
append one boundary. Replace every placeholder with the current exact identity
and private monitor output directory, not a parent directory or historical PID.

```sh
python3 tools/telemetry/session_phases.py mark \
  --session /absolute/private/capture \
  --pid 123 --birth 1790000000:12 \
  --phase menu --provenance controller-observation \
  --evidence 'Observed the native title menu; controller observation receipt XYZ'
```

Labels are `menu`, `loading`, `gameplay`, `exit`, `unknown`. Gameplay does not
mean combat. Provenance is `user-observation`, `controller-observation` or
`process-reconciliation`; the last can label only exit/unknown. Record concise
observable facts and an existing receipt/reference when available. Evidence
text is a supplied claim, not independent verification by this tool.

The marker records PID plus microsecond birth, wall/monotonic timestamps and
native `mach_continuous_time` in seconds, matching Metal MCT interval boundaries.
It checks the current `status.json` game binding and native owned-process
identity before and after clock capture. For `exit` with
`process-reconciliation`, the old PID must be absent or still match; PID reuse
is rejected. The CLI labels the current instant, never backdates a screenshot.
No automatic conversion from `performance.now()` or wall time is performed.

A marker declares the phase from its timestamp up to the next marker. This is
an explicit controller/user declaration, not proof of visual continuity. Place
`unknown` boundaries around unobserved transitions. Nothing before the first or
after the final marker receives a known label. Ending an observed span requires
another marker, commonly `unknown` or observed `exit`. Marking exit does not
quit or kill anything. Do not observe or call this CLI inside a timed run.

## Export a report

After the monitor finalizes, write stdout to a reviewable result:

```sh
python3 tools/telemetry/session_phases.py report \
  --session /absolute/private/capture \
  --pid 123 --birth 1790000000:12 > /absolute/owner-chosen/phase-summary.json
```

The fixed inputs are `metal.jsonstream.gz`, `events.jsonl`, `status.json` and
optional `markers.jsonl`. Without markers every interval remains unknown. The
report records input SHA256 values. It refuses an active/incomplete collector,
invalid JSON, missing metrics or truncated gzip unless `--allow-partial` is
explicit. Partial mode reports only the validated prefix and marks incomplete
status; it never resynchronizes after a malformed object. Duplicate keys,
nonfinite values, extra gzip members, overlapping/replayed intervals and foreign
PIDs cannot silently increase accepted totals. Foreign/replayed records are
counted separately. Collection status binds the raw PID-only vendor stream to
birth; the vendor schema cannot independently prove birth or defeat its attach
TOCTOU window. This does not strengthen the monitor's provenance guarantee.

Metal intervals wholly inside a declared phase are attributed to it. Intervals
crossing a marker remain `mixed`, with no proportional frame invention. Phase
coverage uses only fully attributed observed intervals; gaps and missing metrics
do not become zero FPS. Explicit observed zero frame counts remain zero. Whole
and phase rates are total reported presented count divided by total observed
seconds, across all aligned layers. They are not displayed FPS. No aggregate
interval distribution is advertised as per-frame 1% lows or frame-time variance.

Game physical footprint, RSS, wired bytes, pageins and disk counters come from
existing `process_snapshot` events. First/last/delta refer only to available
snapshots within each phase, not precise boundary values. One sample has no
delta. Repeated occurrences of one phase label suppress its aggregate delta
across disjoint spans, so gameplay between two menus is never charged to menu. Unavailable reads remain unavailable. Host `vm_stat`, `vm.swapusage` and
`memory_pressure -Q` receipts are parsed separately by command; their first/last
and deltas describe the whole host, not game allocations or Metal residency.
Snapshot events use the shared Unix wall clock against marker wall boundaries.
Marker monotonic is producer-process-local (including on the system Python used
here) and is never joined to another process's monotonic. Adjacent event wall
versus event monotonic deltas, and adjacent marker wall versus MCT deltas, must
agree within 0.1 seconds; discontinuities prevent complete memory alignment.
Wall joins remain sampled receipt-time evidence, not exact allocation times. A role identity-change
event prevents a complete report and stops later snapshot attribution.

## Shared data contract and bounds

Owner: the existing root task's temporary session diagnostic owner. Exact scope:
`markers.jsonl` inside the existing monitor capture directory and the explicitly
chosen curated summary. Data class: small supplied scene observations and
curated telemetry aggregates. Markers join the existing **seven-day raw TTL and
sampled 256 MiB aggregate session cap**; the summary joins the existing **30-day
TTL and 4 MiB curated cap**. Markers have a hard 1 MiB and 4096-record cap;
evidence is at most 4096 UTF-8 bytes. This tool neither installs a cleaner nor
reserves an additional aggregate quota. The owner must admit marker bytes to
the existing shared capture budget and update its project session record before
activation. Concurrent capture's aggregate cap remains sampled, not a filesystem
quota. No new persistent producer exists.

Reads require canonical absolute directories, no symlink traversal, private
owned 0700 session / 0600 regular single-link files, fixed filenames and stable
file metadata during each read. Marker writers use a nonblocking exclusive file
lock and fsync. Report inputs are hard bounded: 32 MiB compressed Metal, 16 MiB
events, 1 MiB status/markers, 256 MiB expanded gzip, 1 MiB per object, 64 nesting
levels and 65536 records per stream. Report serialization is capped at 4 MiB.
These are admission limits, not quotas on another process writing files.

Cleanup is manual through the existing project retention route, after checking
exact session identity/ownership/references and preserving minimal required
original evidence and recovery material. Active capture, pending analysis and
explicit evidence/recovery holds exclude deletion. Do not infer cleanup
permission from age, filenames or ignored status; this tool deletes nothing.

## Checks

```sh
python3 -m unittest discover -s tools/telemetry -p test_session_phases.py -v
```

Fixtures cover wrong birth/PID, live identity changes, genuine exit absence,
unknown and boundary coverage, weighted phase/whole totals, malformed/truncated
gzip/JSON, duplicate/foreign/overlapping intervals, missing metrics, host/game
memory deltas, sampled gaps versus observed zeros, byte/record/depth/expansion
caps, symlinks, hardlinks, clock discontinuities and permissions. These checks do not establish a real
scene, installed integration, runtime activation or game performance.
