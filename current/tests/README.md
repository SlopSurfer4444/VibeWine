# Source fixtures

Requirements: Python 3, Clang and Clang++. The ntdll ABI regression compiles/runs
x86_64 Darwin and needs macOS (Rosetta on Apple Silicon). The surface fixture
requires macOS AppKit; its views remain offscreen. C++ fixtures use AddressSanitizer
and UndefinedBehaviorSanitizer. No Wine server, visible application or game is launched.

From the repository root:

```sh
python3 -B current/tests/ntdll/test_ntquery_directory_object.py
python3 -B current/tests/client-extent/test_client_extent.py
python3 -B current/tests/mode-limit/test_modes.py
python3 -B current/tests/closest-mode/run.py
python3 -B current/tests/closest-mode/run.py --predecessor-negative-control
python3 -B current/tests/surface/run.py
```

Use a disposable copy: client-extent and mode-limit produce generated source,
executables and reports beside their scripts. Other runners use temporary outputs
by default. Keep generated products out of commits. The closest-mode runner's
optional `--output` retains a report in an explicitly chosen directory.

## What is tested

- **ntdll:** exact `NtQueryDirectoryObject` from `current/ntdll/sync.c`; the
  retained Wave7 baseline is an expected negative control for poisoned upper
  register bits in the BOOLEAN argument. Both compile from source.
- **client extent:** exact update function and coordinate conversions from
  `current/winemac`. The candidate passes 17 cases covering authoritative top-level extents,
  Retina conversion, negative origins and child geometry with either native
  parent. The retained Wave7 baseline has seven expected regression failures.
- **DXGI resolution cap:** exact enumeration methods, mode-selection header and
  executable profile from `current/dxgi-source`; 25 cases pass. The 111-entry input
  is synthetic: 37 deterministic sizes at three pixel depths, including
  1920×1080 and 2560×1440. No observed display archive is included. Buffer,
  grouping, refresh preservation, selection and default-off assertions remain.
- **DXGI closest mode:** 38 cases pass, running actual conversion, filtering,
  enumeration and closest-mode methods with synthetic catalogs and a scripted
  count/fill wrapper. Tests cover shrink/empty/growth, count/fill errors,
  bounded retry exhaustion, provider overcapacity, fractional refresh,
  validation and unchanged error-output sentinels, with the cap off/on. Reversing
  `current/dxgi-closest-mode.patch` in a temporary source copy reconstructs the
  exact predecessor: 22 of 38 cases fail as expected, including the actual
  shrink-to-empty zero-division detected by UBSan.
- **surface:** 23 cases exercise actual driver create/update/present/detach/destroy,
  Cocoa creation, D3DMetal bridge and generic win32u create/release functions.
  Allocation failures, preserved prior view, one-time cleanup and ownership are
  checked. Real offscreen AppKit views cover client-parent composition,
  Retina, DPI 96/144/171, negative/odd geometry, nested descendants and the
  retained 3420-pixel top-level extent. Reversing `current/surface-followup.patch`
  reconstructs the preceding source; eight isolated controls fail as expected.
  Generic allocation controls intentionally terminate under sanitizers.

The surface executor runs inline, so these checks do not establish scheduler
behavior, atomic construction or concurrent destruction. `CFArrayAppendValue`
has no recoverable allocation return; arbitrary process-wide OOM recovery is not
claimed. Its context directory holds only licensed Wine production excerpts and
an unchanged upstream list header, not private management/harness code.

These tests use mocked surroundings. They are not a full Wine/DXGI build, real
display lifecycle test, game compatibility test, FPS measurement or proof that a
particular installed binary was loaded. Local installed hashes are separately
recorded in `../PROVENANCE.json`; latest gameplay acceptance remains unverified.
Expected predecessor failures are required for the negative-control runs to pass.

`adaptations.json` records original/export SHA-256 values and the path, context
or synthetic-input adaptations. Production source is not rewritten by the tests.
Exported runners were checked without retaining generated artifacts or run logs.
