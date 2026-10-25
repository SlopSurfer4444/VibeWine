# Optional semantic blend hash for graphics PSO keys

Meson `vibewine_semantic_blend_hash` / `DXMT_SEMANTIC_BLEND_HASH` defaults false/0
and uses the common identical PE/native define map. Disabled retains the prior
pipeline hash, private interface and state payload. Enabled adds a semantic blend
fingerprint to `MTL_GRAPHICS_PIPELINE_DESC` hashing and retains its exact equality.
The existing canonical blend-state cache, immutable descriptor and `HashState`
combine mechanism are reused. No pointer/padding hash, new table, synchronization,
native call, serialized cache key or compiler compatibility epoch is added.

One `size_t` is cached in its existing immutable owner. It covers all
non-independent states (RT0 for every count, including zero), and independent
states with all eight targets. Independent/counts 0 through 7 compute only the
selected prefix directly from that owner's immutable descriptor, avoiding a
`GetDesc1` descriptor copy. Count zero hashes globals only for independent states.
Raw enable `BOOL`s are retained; blend fields participate only when blending is
enabled and logic operation only when logic is enabled. The top-level
independent/alpha flags participate separately. The owner lifetime/retention and
one-word member payload are recorded in the
[existing wave contract](../../../docs/fps-wave-20261025.md).

This adds a private virtual accessor only in enabled builds. Build all affected
translation units with the same option; the stand-in CPU interface is not an ABI
compatibility check. A matched production build remains required. The owner has
one new eight-byte member on 64-bit targets; exact complete object size/padding
needs production-class compilation rather than summing fields.

## Checked choices and tradeoffs

The existing complete blend-descriptor hash cannot directly hash PSO semantics:
independent state canonicalization compares all eight targets, whereas PSO equality
compares only `NumColorAttachments`. Distinct canonical state objects can therefore
be PSO-equal when inactive higher targets differ. Pointer or full-eight-target
hashing would violate equal-implies-equal-hash. Hashing raw bytes also includes
padding and inactive fields. None is an acceptable shortcut.

The runner compares three bounded candidates using the same actual production
PSO key, hash and equality:

| Candidate | Retained hash payload per state | Hash-time descriptor work |
| --- | ---: | --- |
| Direct `GetDesc1` field hash | none | One descriptor copy plus selected field walk |
| Selected one-word owner cache | one word | Cached non-independent/all-eight; immutable selected walk otherwise |
| Nine-prefix owner cache | nine words | One cached prefix read for every valid count |

The prefix candidate is a test adapter using the actual production target-field
helper in one pass, with all its prefixes checked against the production helper.
It is faster for partial-independent shapes in this CPU fixture, but retains
nine words instead of one in every state. The selected one-word experiment keeps
the smaller owner payload while avoiding descriptor copies. It is not a claim
that this strategy wins every workload. Both caching strategies add one-time
construction hashing (11 combines for the enabled single-target shape, 74 for
an enabled eight-target independent shape in the counted corpus), one virtual
accessor per PSO hash, and retained metadata. A one-key/default-blend workload has
no collision work to remove. Exact work counters expose that extra accessor even
when timing noise makes a small corpus appear faster.

## Focused execution

Use an existing populated DirectX header checkout read-only:

    python3 tests/vibewine/semantic-blend-hash/run.py --arch arm64 --directx /absolute/path/to/mingw-directx-headers
    python3 tests/vibewine/semantic-blend-hash/run.py --arch x86_64 --directx /absolute/path/to/mingw-directx-headers
    python3 tests/vibewine/semantic-blend-hash/run.py --benchmark --arch arm64 --directx /absolute/path/to/mingw-directx-headers

The sanitizer runner compiles actual production key/hash/equality, blend state
hash/equality and normalization statements, `GetDesc1` body, owner accessor body,
and the actual shared blend helper. It uses immutable stand-in owners and
platform types for unrelated Metal handles. Test-only helper counters are in a
generated temporary copy. It does not compile the full COM/device-child owner or
its constructor, exercise native Metal, or prove the complete default/on ABI.
The actual constructor-to-helper call and private interface declaration are
bound by source checks; full-build verification remains separate.

Coverage includes attachment counts 0..8; independent on/off; every selected
field; disabled blend/logic fields; raw target enable values 0, 1, 2, -1; global
normalization; patterned padding; same-pointer and distinct-pointer identity;
independent states with distinct canonical tails but equal active prefixes;
semantic map deduplication and rehash. Four negative controls use pointer hashing,
all-eight-target hashing, unconditional inactive blend fields, or omit SrcBlend.
Each must fail its intended semantic assertion. Historical production and
current default/off pass correctness and exactly match work counters.

The work corpus keeps other PSO fields equal and varies valid D3D enum values
in write mask/source/destination blending. It covers 1, 4, 16, 64 and 256-state
non-independent shapes, count boundaries, independent 0/1/2/4/8-target shapes,
and active-prefix equivalence collapse. It makes no GPU-validity claim for every
combination. Counts are actual equality/GetDesc1/hash/accessor invocations and
helper combines; bucket occupancy is separate. They are not every internal node
visit or total CPU cost.

On 26 October 2026 both Darwin architectures passed ASan/UBSan with
`-fno-exceptions` and warnings as errors. For 256 non-independent blend keys with
one attachment and 4,096 hits:

| Actual production-key work | Prior omitted-blend hash | Selected owner hash |
| --- | ---: | ---: |
| Equality calls on hits | 526,336 | 4,096 |
| GetDesc1 calls on hits | 1,044,480 | 0 |
| Largest bucket | 256 | 4 |
| New accessor calls on hits | 0 | 4,096 |
| Extra construction combines over 256 owners | 0 | 2,816 |

The independent/count-zero control collapses 128 canonical owners into one PSO
key. The selected hash cannot remove the required semantic equality copies there:
4,096 hits still make 8,128 `GetDesc1` calls, plus 4,096 new accessors/8,192 helper
combines. This is a visible workload with extra hashing and no collision saving.

## CPU timing scope

`--benchmark` builds separate unsanitized O2 binaries, disables counters and
reports five samples (minimum/median/maximum). The direct candidate uses the
production hash body with a direct virtual `GetDesc1` call and a no-inline boundary
so the compiler cannot replace the descriptor-copy path with owner field access.
Each hit sample executes 16,384
actual `unordered_map::find` operations with an observed checksum. Construction
samples create 4,096 adapter owners; consumption after the timed interval keeps
fingerprint construction observable. These timings include adapter descriptor
setup, allocation and construction, while excluding production COM/device-child
and dual-source setup. They measure incremental adapter choices, not the full
production constructor, Windows runtime, native Metal or frame timing. Host
contention/frequency and compiler layout apply; several baseline timing ranges
were wide. Timings are observations, not an admission promise for a real game.

The final arm64 CPU fixture observed these median nanoseconds per hit:

| Shape | Direct field hash | One-word owner | Nine-prefix owner |
| --- | ---: | ---: | ---: |
| Non-independent, 256 states, one target | 65.2 | 33.3 | 40.0 |
| Independent, 128 states, four targets | 133.0 | 114.5 | 44.4 |
| Independent, 128 states, eight targets | 255.7 | 65.4 | 74.6 |

In the same adapter run median construction nanoseconds per state were
50.3/51.0 (direct non-independent/independent), 56.8/227.6 (one-word), and
80.0/263.8 (nine-prefix). The independent/count-zero control measured 40.0 ns/hit with the prior hash
and 62.2 ns/hit with the selected owner hash; its retained equality work plus
extra hashing is deliberately visible. Construction and small-corpus timing
differences are host/compiler-specific; complete ranges are retained in receipts.
The option remains off pending matched-build and application measurements.

Fixed fixtures are durable results owned by this directory. Each runner owns
only private temporary generated source/binaries, with a 64 MiB aggregate cap,
120-second compiler and 60-second child timeout, removed on exit/errors. Hard-kill
leftovers require manual ownership/process/hold validation. Retained raw reports
follow the root wave's seven-day/64 MiB contract with active/analysis/recovery
holds; no automatic cleaner or production logger is installed. No game, cache,
runtime selection, installed binary, Git index or unrelated data is changed.
