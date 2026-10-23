# Completed ring-block retention

Historical verification checkpoint. Original local build/source identities below
remain unchanged; [DXMT provenance](../provenance/dxmt.md) maps them to
code-equivalent public commits. For the latest installation state, see
[current status](../STATUS.md).

DXMT component `bd107f63a816c76c79e1452e0b606412a7c3476d` fixes serial idle
aging in `RingBumpState`, used by upload, argument, temporary-copy and host
command heaps. It is a general allocator change, without game-name checks.

With a fixed completed upload sequence, the predecessor ages only its FIFO
front. A burst of ten standard blocks remains at ten after 301 idle cleanup
calls; call 302 removes only one, and starts the next block's grace period.
At 32 MiB per upload block this can retain a large completed high-water set.

Completed blocks now age together. Cleanup retains one completed standard warm
spare, removes expired excess and completed ad-hoc blocks, and preserves every
in-flight allocation. A list permits interior cleanup without moving surviving
blocks or invalidating their references. Reuse resets the idle grace; counters
saturate. The existing unconditional teardown sentinel is unchanged and still
requires the owner to stop all users first. Cleanup remains lazy and requires
future owner calls; this is not a global memory cap.

## Verification

The exact production template compiles against counted move-only blocks:

```sh
cd components/dxmt
python3 tests/vibewine/ring-retention/run.py --sanitize
python3 tests/vibewine/ring-retention/run.py --arch x86_64 --sanitize
python3 tests/vibewine/ring-retention/run.py \
  --source-ref a5ad261e --expect-retained-baseline --sanitize
```

The baseline command above uses public `a5ad261e`, whose tree equals the
originally tested local `6f21900c`; it works in fresh public component clones.

Native and translated ASan/UBSan fixtures passed. The predecessor reproduces
retention and fails the candidate's new bound. Tests exercise fixed/advancing
coherence, completion boundaries, pending blocks, partial-tail/front reuse,
oversized interior entries, empty/sentinel cleanup, stable surviving references
and a concurrent producer/collector. Independent review found no material
lifetime issue; the integration owner reran translated sanitized regressions.
No DS3 game was launched to verify this patch.

A separate 69-translation-unit DLL was built cleanly on the exact active
renderer predecessor source, changing only this header. The header is identical
to the committed component patch. Import/export names and ordinals and embedded
Metal helper bytes are unchanged. The sandboxed hardware copy guest passed
3,824 checks, 1,309 queries and 512 byte-exact snapshots with actual candidate
module mapping verified. Its private server exited; the game's existing Steam
and Wine-server identities remained unchanged. These checks exercise public
copy/ownership correctness, not direct pool counts or gameplay.

The full component branch is newer than that runtime chain: a separate attempted
whole-component build was rejected at link time for newer heap APIs absent from
the existing native bridge. Its partial outputs were not activated. The narrow
candidate remains unactivated too; the original generation is still selected.

## Captured stalls remain unresolved

Human gameplay with the preceding staging and dynamic retention fixes still
reproduced displayed holds of 45.616 and 96.300 seconds. Automatic stack snapshots
localize waiting around Metal submission and completion. Host swapping and
compression are confounds; neither a specific allocation pool nor a driver
fault is established as the cause. Short measured GPU execution in aggregate
records does not explain every held drawable. The CPU trace's timestamp gate
also rejects complete coverage, so no CPU-hotspot attribution is made.

The ring patch fixes a separately reproduced retention defect. It is not an
accepted game-freeze fix or FPS result. Source, standalone build, hardware
correctness, activation and gameplay acceptance remain separate stages. Private
raw traces and runtime environment data are deliberately absent from this
source repository.
