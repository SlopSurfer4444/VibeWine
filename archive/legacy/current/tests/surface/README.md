# Client-surface source fixture

From the repository root on macOS with Python 3, Clang++, AppKit and `patch`:

```sh
python3 -B current/tests/surface/run.py
```

The runner extracts the production surface functions from `current/winemac`,
compiles them with AddressSanitizer and UndefinedBehaviorSanitizer, and exercises
23 cases using native `NSView` objects. All generated files live in an automatically
removed temporary directory. The runner prints a JSON report and returns zero
only when the candidate passes and all required predecessor controls fail.

Coverage includes client/native allocation failures, D3DMetal bridge and array
allocation failures, caller unwinding, retained surface ownership, null native
rectangles, child composition at three DPIs with/without a parent under Retina
and non-Retina conversion, nested descendants and top-level raw extents.

The immediate predecessor is reconstructed by reversing
`current/surface-followup.patch` against a temporary copy of the current source.
This retains the earlier top-level extent fix and needs neither Git history nor
any private source tree. Its full run fails as expected; eight isolated controls
also fail, including allocation failures detected by sanitizers and child
composition failures detected by assertions.

`context/win32u_client_surface.c` contains exact `client_surface_create` and
`client_surface_release` definitions with their original Wine license header.
`context/wine/list.h` is an unchanged upstream header. `adaptations.json` records
their hashes and the fixture adaptations. Compile-only C++ casts, the recording
notification center, and allocation/array fault hooks are retained from the
original fixture adapter; production source files remain untouched.

These are source-level tests with a mocked Wine environment. They do not start
Wine or a game, build the complete driver, prove installed binary identity, or
establish real-game display behavior or performance.
