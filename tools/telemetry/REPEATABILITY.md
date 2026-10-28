# Repeated stationary windows

`repeatability_analysis.py` extends the existing fixed-window analyzer with an
ordered, source-bound A/A and A/B report. It launches nothing and changes no
settings. The CLI recomputes every window from original decoded records and
begin/end clock receipts; a supplied accepted flag cannot replace that work.
Moving routes are rejected by the maintained fixed-window analyzer.

Owner: the calling session diagnostic owner; consumer: the integration owner.
Input scope: explicit canonical regular single-link files. Each decoded input
is at most 64 MiB / 131073 rows / 8192 bytes per row; each clock envelope is at
most 8192 bytes; the manifest is at most 1 MiB / 32 ordered runs. These are
admission bounds, not additional producer quotas. Output is an explicitly named,
new file of at most 1 MiB, counted within the existing 4 MiB curated result cap
and 30-day TTL. Raw inputs retain the session's existing seven-day TTL and
aggregate byte cap. No additional raw quota or retention is created. Active,
pending-analysis, evidence and recovery holds exclude cleanup; this tool deletes
nothing. Cleanup remains with the owner after exact path/reference review.

Run:

```sh
python3 tools/telemetry/repeatability_analysis.py /absolute/comparison.json \
  --output /absolute/new-result.json
```

Exit 0 means the declared runs are comparable under these checks. Exit 3 writes
the rejection report. Invalid input exits nonzero without a result. Existing
results and immutable inputs cannot be overwritten.

The manifest is `{ "schema": 1, "runs": [...] }`. Supply 4..32 ordered runs,
at least two per condition A/B. Retain all attempts; do not select the best runs.
Each run has:

* `id`, `condition` (`A` or `B`), `processKey` binding native PID and birth;
* `normalExitEvidence` and `relaunchEvidence`, bounded references to genuine
  observed normal title/desktop exit, process absence and a fresh birth/title;
* `binding`: exact `hubRevision`, `wineRevision`, `dxmtRevision` (40 hex each),
  `buildReceiptSha256`, `installedManifestSha256` (64 hex each), plus
  `loadedModules`, a map of exact module identifiers to actual SHA256 values;
* `controls`: nonempty evidence references/values for exactly `scene`, `settings`,
  `cacheTreatment`, `power`, `background`, `observer`, `route` (use an explicitly
  observed stationary camera/scene reference for this stationary scenario);
* `inputs`: absolute `capture`, `begin`, `end` paths, as consumed by
  `fixed_window_analysis.py`;
* `windowArguments`: the keyword arguments to `analyze_fixed_window`, including
  `window_seconds`, expected swapchain/dimensions/revision, actual collector hash
  and fresh measurement UUID. The collector hash belongs to the invoked
  executable, not a guessed source binary.

Bindings must remain identical within each condition and differ between A/B.
Controls, drawable geometry and fixed duration must remain equal throughout.
Swapchain and configuration-revision serials are local to a game generation and
are checked per capture rather than equated across independent restarts. Reused
process generations, decoder identity/generation pairs or measurement IDs cannot
masquerade as restart repeats. Actual recomputed collector hashes must agree
across every run; matching observer description text cannot hide executable drift.
Missing lifecycle evidence, rejected windows, native loss and source/controls
drift suppress all comparison numbers. Native presentation loss and interval
distribution checks remain owned by the maintained fixed-window analyzer.

The report retains run order, input hashes, each interior interval distribution
(including p95/p99 and long-interval counts), per-condition median/min/max/spread
of presentations per fixed second, B-minus-A median delta and whether its
absolute magnitude exceeds the larger observed repeat spread. This is
descriptive: two repeats do not provide a statistical confidence interval, and
the spread comparison is not an acceptance threshold or causal verdict.

The owner must independently verify the actual loaded module binding, native
scene, cache treatment, normal exits and reference evidence. JSON declarations
cannot do that. A changed bundle tests that bundle; it cannot attribute a gain
to one switch. Resolve observer overhead separately using the same independent
observer in disabled, enabled-unarmed, light and detailed conditions. This tool
does not supply such an observer, physical scanout/full latency, exact route
alignment, per-shader GPU cost, or a game-performance claim.

Tests use synthetic protocol records only:

```sh
python3 -m unittest discover -s tools/telemetry -p test_repeatability.py -v
```
