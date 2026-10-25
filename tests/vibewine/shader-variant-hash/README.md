# Shader variant hash

The production `std::hash<dxmt::ShaderVariant>` previously returned only the
alternative index. Every same-alternative key therefore shared one full hash,
although equality distinguished its fields. `CachedSM50Shader::get_shader`
(`d3d11_pipeline_cache.cpp`) inserts/looks up this per-shader map under its mutex.
The map is reached when creating a new graphics/geometry/tessellation PSO or
speculatively compiling shaders; existing outer PSO cache hits bypass it. The
existing early-compile mode requests four distinct pixel variants per shader.
Actual live key multiplicities and this mode's enabled state were not measured.

The hash now combines the alternative index and exactly the equality fields
through existing `dxmt::HashState`, already used by adjacent pipeline caches.
Pointers use `std::hash` of their identities; pointee data and object padding are
never read. The stream-output handle uses its full `uint64_t` type. Default
variants stay equivalent. The defensive valueless return preserves the previous
hash. Adding a new alternative requires explicitly defining its hashed fields.
No cache key serialization or shader compilation digest changes.

`PackedVariantList` is unrelated packed instruction storage, not this map's key.
All active fields are pointers, integers, enums or bools. In particular both
maximum tessellation factors are `uint32_t`; float NaN and signed-zero cases do
not apply to these production alternatives.

Run from the component source root:

    python3 tests/vibewine/shader-variant-hash/run.py --arch arm64
    python3 tests/vibewine/shader-variant-hash/run.py --arch x86_64

The runner extracts the actual production eight alternative declarations,
`operator==` bodies, alias, and hash specialization, and includes actual
`util_hash.hpp` and `airconv_public.h`. The only platform adapters are complete
stand-in pointee classes; they are never dereferenced by the production hash.
The complete Windows/Metal header and production cache/scheduler are not compiled
by this fixture. Full matched compiler/PE/native builds remain an integration gate.

The deterministic corpus covers all 23 equality fields independently, alternative
identity, null/distinct aligned pointer identities, changed pointee contents,
uint32/uint64 upper bits, all bool fields, valid index-format values, all eight
alternatives over independently patterned padding, duplicate insertion, successful
and unsuccessful lookup, rehash, and 256 keys in each of the seven payload
alternatives. Pointer identities come from live arrays rather than fabricated
addresses; numeric addresses and bucket placement vary with ASLR.

For each payload alternative the JSON arrays are
`[insert equality calls, hit-plus-duplicate equality calls, miss equality calls,
largest bucket]`. They count actual production equality calls from
`std::unordered_map` and bucket occupancy, not every internal node visit, elapsed
time or total CPU cost. On 2026-10-26, both Darwin architectures passed with
ASan/UBSan: each historical corpus measured `[32640,65792,65536,256]`, and each new
corpus measured `[0,512,0,3-to-5]`. Hash caching filters unequal full hashes before
equality even when they occupy the same bucket; zero calls does not mean zero
lookup work. The historical production hash passes the entire correctness corpus.
Separate negative controls restore the index-only hash (distribution rejection),
omit the sample-mask field (field sentinel rejection), or hash raw variant bytes
(patterned-padding rejection). Every control must fail its expected assertion
without sanitizer errors.

This changes the default hash algorithm rather than introducing a runtime flag,
table or cache. It establishes reduced collision/equality work in the fixture,
not game FPS, frame time, installation or live compatibility. A single/default
variant also pays extra combining/dispatch overhead; only matched application
measurements can determine the net cost for a real key mix.

This test directory owns fixed source fixtures. Each runner invocation owns only
its private temporary directory, with generated translation units and binaries,
a 64 MiB aggregate cap and exit-time deletion via `TemporaryDirectory` (including
ordinary exceptions). Hard-killed interrupted temporary directories may need
manual removal after confirming no runner remains active. No persistent producer,
user data, runtime, cache, game, installed artifact or Git index is modified.

The integrated follow-up also reuses the found iterator in both shared-lock and
unique-lock CreateShader hit paths, avoiding their second SHA-key lookup while
holding the same lock. This fixture does not benchmark that adjacent change.
Outer graphics pipeline hashing intentionally omits blend state because equality
accepts structurally equivalent distinct state objects; pointer hashing there
would be incorrect. Any future blend hash must mirror conditional equality.
