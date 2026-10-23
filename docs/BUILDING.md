# Build and verify

## Fetch and check source

`python3 scripts/workspace.py init` initializes the exact pinned components and their nested submodules using
Git. It requires network access if objects are missing, does not follow branch
tips and never runs downloaded build scripts. `python3 scripts/workspace.py check`
is offline and rejects missing, dirty or mismatched component and nested dependency
checkouts. Initialization preflights existing checkouts recursively, overriding
Git submodule ignore settings so local changes cannot be hidden.
`python3 scripts/workspace.py check --metadata-only` checks repository wiring
without requiring populated components. `python3 -m unittest discover -s tests/workspace`
runs the helper's local tests without downloading or building code.

## Build components using their build systems

Use a disposable development checkout and explicitly provision compiler/SDK
prerequisites. Nothing here installs tools, accepts licenses, modifies game
prefixes, signs applications or replaces an installed runtime automatically.

For Wine, read `components/wine/README` and `components/wine/configure --help`.
An out-of-tree configure/build begins with:

```sh
mkdir -p build/wine
cd build/wine
../../components/wine/configure --help
# Select target architecture and dependencies following the component docs.
# Then run configure with those options, followed by make.
```

For DXMT, read `components/dxmt/docs/DEVELOPMENT.md` and its Meson options. Use
its existing Meson cross file, matching Wine build, LLVM and Metal toolchain:

```sh
meson setup build/dxmt components/dxmt \
  --cross-file components/dxmt/build-win64.txt \
  -Dnative_llvm_path=/absolute/path/to/llvm \
  -Dwine_build_path=/absolute/path/to/configured-wine-build --buildtype release
meson compile -C build/dxmt
```

Those paths are required inputs, not bundled dependencies. Check the pinned
DXMT documentation for its exact required versions. A custom native bridge may
require additional matched components; do not substitute arbitrary release DLLs.
A full clean-room build and combined runtime acceptance remain distinct from
workspace checks. Historical [build recipes](../build-recipes/README.md) document
older builds only and are not a shortcut to assembling the new pair.

Before promoting a new pair: compile required components, run relevant regression
fixtures, verify ABI/link compatibility, then separately test launch, resolution,
fullscreen exit, repeat launch and controlled frame presentation. No CI badge here
means those interactive or native GPU checks passed.

## Evidence by source checkpoint

The pinned Wine component passed reduced-dependency module compilation for
native winemac, ntdll and win32u, and i386/x86_64 PE oleacc and winemac modules.
This is not a complete production-feature Wine runtime build.

The current selected Wine/graphics closure passed the matched builds recorded
in [status](STATUS.md) and the [24 October follow-up](diagnostics/focused-followup-20261024.md).
This covers matching PE/native graphics, static AIRCONV, the Metal helper and
selected paired Wine modules over retained engine/dependency assets. It is not
a full clean-room Wine distribution. The public equivalents preserve production
source; publication-only reference/documentation changes do not rerun a build.

The [fork review](integrations/fork-review-20261023.md) and component notes record
the imported fixes and focused source/native checks. Some fixtures need a real
WindowServer, configured Wine build, LLVM or Metal SDK; hosted checks do not
stand in for those prerequisites or live game acceptance. Existing exact runtime
source patches describe the older composite baseline. Follow their named bases
for historical reproduction, rather than applying them as a blind queue onto
the new component pins. Controlled game performance remains pending.
