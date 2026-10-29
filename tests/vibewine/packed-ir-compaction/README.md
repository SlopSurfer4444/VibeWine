# Parsed instruction backing compaction

The production parser shrinks eligible packed instruction backing once, after
all CALL/CALLC cloning finishes and before publishing the shader. A buffer needs
at least 256 bytes of unused capacity and at least one quarter of its requested
capacity unused. `realloc` failure preserves pointer, size, capacity and contents.
Zero occupied bytes release backing without a zero-size `realloc`; repeated
finalization is a no-op after successful shrink. This uses the existing allocator
and packed representation, with no new configuration, DXBC copy or reparse.

Run the focused template checks:

    python3 tests/vibewine/packed-ir-compaction/run.py --arch arm64
    python3 tests/vibewine/packed-ir-compaction/run.py --arch x86_64

Both packed-growth modes use the complete production template under ASan/UBSan
and production no-exceptions mode. Test-only interception forces moving shrink,
allocation failure, exact ownership/free accounting and state observation.
Coverage includes mixed payload/alignment/order, retry after failure, empty and
zero-occupied backing, repeated no-op, absolute/relative thresholds, threshold
arithmetic near SIZE_MAX, and append after successful/empty compaction. Synthetic
capacity/size injection restores valid state before destruction; it does not
pretend giant allocations are live. Temporary binaries are capped at 32 MiB,
compiler/executable timeouts 60/20 seconds, and removed on normal/error exit.

The production corpus runner requires an existing configured native support build
and the previously extracted Wine DXBC corpus. No dependencies are installed:

    python3 tests/vibewine/packed-ir-compaction/run-corpus.py \
      --support-build /absolute/retained/configured/build \
      --fixtures /absolute/retained/ir-assessment --growth 0

Repeat with `--growth 1` for the existing optional 1.5x growth policy. The manifest
pins all 13 input byte hashes and source lines. All 21 parser/converter translation
units are freshly compiled from this checkout with one consistent current packed
list definition; generated AIR headers and toolchain flags come from the support
build. No retained converter objects are linked. The baseline substitutes the
actual pre-change production CFG body at `a6637cfe`; both candidates use the same
class definition, avoiding mixed-definition ODR risk.

The runner compares actual allocator usable bytes via `malloc_size`, requested
capacity and occupied bytes; 48 output artifacts must match byte-for-byte:
reflection/arguments for all 13 inputs, seven ordinary shader outputs generated
twice and four paired geometry/tessellation outputs generated twice. Repetition
exercises retained IR for late compilation. Reused CALL/CALLC lifetime fixtures
add NOP payloads to cloned function bodies and check nine actual CFG paths plus
LLVM module verification. The pre-change simple VS/PS backing fails the same
fixed at-most-512-byte backing oracle accepted by the compacted candidate. Parser P50/P95
are diagnostics, not performance acceptance or a benchmark of game shaders.

Corpus builds use at most two compiler processes, a 128 MiB temporary-file cap,
180-second compiler/link and 90-second corpus execution timeouts, and remove all
private source copies, objects, binaries and shader outputs on exit. The corpus
is read-only and remains with its original owner/retention contract. Hard-killed
leftovers require manual exact-path owner/process/hold checks. Retained reports
belong to the memory-queue wave IR owner: 5 MiB raw/seven days, notes 30 days;
active review/recovery holds override cleanup. No collector or cleaner is added.

These checks establish backing reductions for this corpus, representation and
output preservation. They do not demonstrate process RSS/pressure, native Metal,
cache transport, concurrent real-game workloads, FPS or installation.
