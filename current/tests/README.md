# Portable source fixtures

Run from a disposable copy of this repository. Requirements: Python 3, Clang and
Clang++; the ntdll ABI regression specifically compiles/runs x86_64 Darwin and
therefore needs macOS (Rosetta on Apple Silicon). The C++ fixtures use AddressSanitizer
and UndefinedBehaviorSanitizer. No Wine server or game is launched.

From the repository root:

```sh
python3 -B current/tests/ntdll/test_ntquery_directory_object.py
python3 -B current/tests/client-extent/test_client_extent.py
python3 -B current/tests/mode-limit/test_modes.py
```

The client-extent and mode-limit runners produce generated source, executables
and reports beside their scripts; keep those disposable outputs out of commits.
The ntdll runner uses a temporary directory automatically.

## What is tested

- **ntdll:** exact `NtQueryDirectoryObject` extracted from `current/ntdll/sync.c`;
  the retained wave7 baseline is an expected negative control for the poisoned
  upper register bits / BOOLEAN argument regression. Both compile from source.
- **client extent:** exact function from `current/winemac/window.c` and conversion
  functions from its headers. Candidate: 15 cases, zero failures. Retained wave7
  baseline: seven expected failures. Tests cover Retina conversion, raw client
  extent, negative origins, present rectangles and child-window behavior.
- **DXGI modes:** exact mode enumeration methods from `current/dxgi-source`, plus
  its mode-selection header and executable-name profile. 25 cases pass. The
  public 111-entry input is **synthetic**, consisting of 37 deterministic sizes
  at three pixel depths, including 1920×1080 and 2560×1440. No observed display
  archive is included. Existing buffer, grouping and selection assertions remain.

These are source-level fixtures with mocked surroundings, not a full Wine/DXGI
build, real display lifecycle test, game compatibility test, FPS measurement or
proof of correspondence to an installed binary. Baseline failure is intentional
and required for the first two runners to report overall success.

`adaptations.json` records original and exported SHA-256 values for every copied
fixture/runner and describes path or synthetic-input changes. Production source
is not rewritten by these tests. All three runners were checked in a disposable
copy; generated artifacts and run logs are not included in this export.
