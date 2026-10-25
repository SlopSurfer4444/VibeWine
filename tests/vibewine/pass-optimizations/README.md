# Opt-in pass optimizations: CPU production fixture

Run from the DXMT component root:

```sh
python3 tests/vibewine/pass-optimizations/run.py
python3 tests/vibewine/pass-optimizations/run.py --arch x86_64
```

The runner compiles with AddressSanitizer and UndefinedBehaviorSanitizer. On an
arm64 Mac, the x86_64 binaries execute through `/usr/bin/arch -x86_64`. It prints
JSON with source hashes, the historical baseline revision, each actual exit code,
and behavioral assertion results. Generated translation units and binaries live
in a temporary directory removed on exit; the runner does not retain captures.

## Production binding

The fixture extracts the following code verbatim from the selected source tree:

- `dxmt_pass_optimizations.hpp`, including both actual helpers;
- encoder data structures and list operation enum from `dxmt_context.hpp`;
- the prepass and complete existing optimizer loop from `flushCommands`;
- all relation, signature matching and dependency methods used by that loop;
- the guarded physical residency hook immediately before final render command
  encoding, with its position checked against `encoder.encodeCommands`;
- allocation flags, texture view descriptor and key structures.

It includes the real `Metal.hpp` / `winemetal.h` command structs,
`WMTMemoryPointer`, `FenceSet`, and `Flags`. Texture ownership/allocation storage
is a CPU adapter exposing the fields consumed by the production code. Native
texture width/height calls use a CPU map keyed by the actual mocked native handle,
so descriptor/native extent mismatches are independently testable. There is no
Metal device, native library, Wine process, installed runtime, game, or GPU work.
The extracted production methods retain their normal destruction and fence logic;
the fixture arena releases only objects the optimizer did not already destroy.

## Behavioral coverage

Five positive configurations run: undefined/default flags, explicit off, each
feature alone, and both enabled. Default/off must preserve ordinary color stores
and physical resource declarations. Enabled cases check full ordinary adjacent
Render-to-Clear overwrite, all node/fence/command identities, and survival of the
store decision when the existing optimizer subsequently folds the clear into the
next render. Original intervening Null, compute, blit, event and timestamp nodes
must prevent the store optimization.

Rejection cases include missing references/allocation/descriptor, private/shared/
heap/mapped storage, attachment count/DSV, native handle mismatch, base view and
subresource bounds, 2D/sample/mip/array/depth constraints, zero/partial/mismatched
extents, independent native extents, ordinary store/resolve variants, and aliases.

Residency cases check physical handle identity, usage and stage upgrades, a single
earlier declaration covering both masks, prohibited cross-product unions, direct
mapped collisions and overflow/eviction, zero handles/masks, all original links and
tails, exact nondeclaration command order, and deduplication after the actual
existing optimizer merges two render command lists. Safe missed duplicates after
cache collisions are expected.

The actual historical context at revision
`e11476e33117b454bac60b997532f9823e34855b` must fail the enabled behavior assertion.
Connected negative controls also disable helper effects, move the prepass after
clear folding, bypass partial/native extent and resolve exclusions, ignore usage,
stage or resource identity, and combine independent masks. Each negative must
compile and exit 1 on a behavioral assertion. A compile error, signal, sanitizer
failure, or setup error does not count as successful rejection.

These fixtures establish CPU command/pass decisions and production integration
ordering. They do not prove Metal residency semantics, native view factory
invariants, PE/native ABI compatibility, image equivalence, game stability, or FPS.
The separate `gpu/` artifacts require their own controlled execution and evidence.
