# Occupied BindingSet replacement hazard membership

`BindingSet::bind` used to update the hazard bit only for an empty slot. A
nonredundant occupied replacement stored the new element and set dirty/replacement
while retaining the predecessor's hazard classification. Move the hazard update
onto the accepted replacement path, after the storage assignment. The redundant
early return continues to leave storage, dirty, hazard and the caller's
`replacement` flag untouched.

Production reachability (base `96940d73ac28f57fe12df832d246d3e84965f2cd`):

- `d3d11_context_impl.cpp:3674` and `:3820` supply `!hazardsFree()` when
  binding SRVs and vertex buffers. Their production redundancy traits in
  `d3d11_context_state.hpp:69` and `:89` compare raw object identity.
- A distinct resource can have a different hazard classification. `false` to
  `true` must add the occupied slot to hazard iteration. Otherwise
  `ResolveSRVHazard` (`:2822`) and `ResolveIAHazard` (`:2856`) miss that new
  resource when resolving a later conflicting output. The reverse transition
  leaves unnecessary hazard scans.
- The classification derives from resource creation flags
  (`d3d11_resource.hpp:153-155,198,404`, `d3d11_view.hpp:68-70`). Passing a
  contradictory flag with the same object is not a reachable production use
  established here. This fix preserves the existing redundant-bind no-op.

## Focused source fixture

The runner includes the actual `BindingSet`, `util_bit.hpp`, and production
`Com` implementation. It extracts the exact production SRV/VB struct and trait
sections, failing if their section shapes change. Only native COM declarations
and counted resource endpoints are substituted; QueryInterface is not exercised.
It follows the actual caller pattern: pass raw identity, then initialize the
retained reference and VB metadata only when replacement is accepted.

The 16 individually runnable scenarios cover both occupied classification
transitions for SRV/VB, both same-classification replacements, contradictory
redundant input without moving or dirtying storage, incoming/private COM reference
ownership, sparse bound/hazard iteration at slots 63/64/127, production dirty and
bound mask queries, unbind during iteration, absent-unbind no-op, rebinding, move
construction/assignment, and default hazard membership. BindingSet has no masked
hazard iterator API; its actual bound/hazard iterators and mask queries are tested.
The moved-from set is only destroyed: its validity for further iteration is not
part of this change.

Run from the component root:

```sh
python3 tests/vibewine/binding-hazard/run.py --sanitize
python3 tests/vibewine/binding-hazard/run.py --arch x86_64 --sanitize
python3 tests/vibewine/binding-hazard/run.py --baseline-ref 96940d73ac28f57fe12df832d246d3e84965f2cd --sanitize
```

The baseline mode uses the immutable old BindingSet header with the same fixture.
It requires all seven transition/boundary scenarios to fail specifically on
hazard iteration assertions and the nine unaffected scenarios to pass. Sanitizer
crashes and unrelated assertion failures are rejected. The runner prints hashes
of the relevant production sources. Temporary generated headers and executables
are task-owned, automatically removed on runner exit, and never installed.

Verification on 2026-10-26: final candidate native arm64 and x86_64 runs each pass
16/16 with AddressSanitizer and UndefinedBehaviorSanitizer. Immutable baseline
rejects all seven expected hazard assertions and passes the nine controls.

Candidate header SHA-256:
`c3925d25f92c25b3440c0192523c7fcef5c3f069f3e5063ab924c12456cde581`.
Baseline header SHA-256:
`c4a19fa0610450aaaf6fb339846c45e6508899239e9bdcaf05e04c1193a26cfd`.

This is source-level correctness evidence. The fixture does not execute the full
D3D11 context, Metal, a matched production build, or a game; it establishes no
FPS or game-compatibility result. Installed runtime selection remains unchanged.
