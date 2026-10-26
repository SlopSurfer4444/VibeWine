# Initializer upload equality reuse

This candidate addresses the reviewed case where a completed32MiB initializer
upload block could not be reset at last-use==event-completion, because the generic
ring deliberately accepts a strict exclusive reuse boundary. It is independent
of trace delivery and tracker retirement.

`vibewine_initializer_inclusive_reuse` is a separate default-false Meson option,
propagated as `DXMT_INITIALIZER_INCLUSIVE_REUSE` to matched PE/native compilation.
Only the initializer's two checked reservation attempts translate its inclusive
completed sequence into the existing ring's exclusive boundary. Translation is
bounded by completed<current<UINT64_MAX; invalid/exhausted input gets boundary0.
Default/off pass the original value unchanged. The generic ring and existing
strict-contract fixture are byte-identical to cd7350da. No installed runtime,
game, runtime selection or tracker policy changes are part of this candidate.

The offline CPU suite passes default/off/on modes, including the unchanged31-case
transaction suite in each mode. In the completed-equality sequential upload case,
enabled mode uses one native backing identity rather than two and succeeds when a
second native allocation is denied. Both initializer reservation attempts are
covered, as are pending/stale protection, original distinct two-plane backing,
atomic rollback at native nil/bad_alloc/real list-node allocation, and sequence
exhaustion. Original checked-ring ASan/UBSan and initializer transaction gates
also pass separately. See the test README for exact coverage and mock limits.

The native offscreen Metal fixture is a separate GPU-lifetime gate using actual
payloads, delayed reads and readback. Its README records the tested seam and
remaining production-bridge limitations. Keep this experiment off in ordinary
builds; source and CPU evidence do not authorize game-performance claims or
installation. Root owns matched-build and full integration acceptance.

Retention uses the existing initializer repair contract: worker-owned private test
scratch is removed at exit; retained raw receipts have seven-day TTL and combined
64MiB cap with active/recovery holds and manual exact-owner cleanup. No persistent
producer is introduced.
