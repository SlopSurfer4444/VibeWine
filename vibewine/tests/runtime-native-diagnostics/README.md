# Offline native runtime diagnostic fixtures

Run from any directory on macOS with Python 3 and the Command Line Tools SDK:

```sh
python3 /path/to/wine/vibewine/tests/runtime-native-diagnostics/run.py
```

`--arch arm64` or `--arch x86_64` selects a compile/run architecture (the host
must support executing it). `--no-sanitizers` omits the default C ASan/UBSan
variant. A nonzero exit or a sanitizer diagnostic fails the suite. The final
JSON receipt goes to stdout and contains checked source hashes and case names.
The runner resolves the current component source relative to itself; it does
not depend on historical output directories, Git state, or installed Wine.

The C fixtures include the production headers directly. They exercise the
exact opt-in value `1`, off-by-default behavior, no disabled clock/write work,
100 ms threshold, failed/backwards clocks, CPU-clock fallback, errno
preservation, 16 concurrent writers, saturating 128-record limits independently
in two translation units, and the bounded scalar metadata schema. Small scalar
addresses are intentionally invalid to dereference. Visibility guards exercise
the exact case-insensitive full Windows image path, byte length, traversal and
symlink rejection, unsafe mode rejection, directory identity after rename,
exclusive publication, size limits and preservation of an existing result.

The Foundation fixtures compile the exact current `WineMetalLayer`
implementation extracted from `d3dmetal_objc.m`, and include the current
`visibility_snapshot.inc` and `visibility_startup.inc` directly. Mock objects
exercise drawable/event delegation counts, phase accounting with/without event
posting, one-shot admission, invalid deadlines, main-queue expiry, invalid
geometry JSON fallback, worker completion delivery, delayed layer retention,
detachment, excluded images, disabled output, unsafe output directories and
startup expiry. Startup JSON has a restricted metadata schema and a maximum
32 KiB publication. The AppKit class names are mocks backed by `NSObject`.

All compiled binaries, generated extraction sources and JSON publications live
in one private temporary directory, canonicalized to avoid macOS's `/var`
symlink. The temporary directory is removed when the runner exits. The fixtures
are adapted from the retained drawable-phase and visibility-v2 offline tests;
historical runners wrote binaries/results beside sources, which this runner
does not do. The historical real `CAMetalLayer` fixture is deliberately excluded.

The suite requires exactly one worker startup cleanup
callback when serialization returns nil, throws on both primary/fallback
attempts, or returns oversized data. It also reconstructs the retained exact
collector in a temporary include directory and requires its full SHA-256
`daf3feb2ec3de6ebc800ce86f5da121d00376a18ff3eb5f22491dc083dae39f3`
before compiling three regression controls. That baseline must demonstrate
zero callbacks/cleanup, with no output publication. The current collector
must attempt publication once with nil data (rejected by the production
guard), close the startup directory once and publish no file. Failure
injection wraps the serializer class only; it does not change production
source. Publication and directory-close seams count cleanup calls.

These checks create no AppKit windows, real Metal layers/devices/drawables, Wine
processes, game processes or live controls. They do not build/install Wine,
alter the application bundle or VPN, establish actual window visibility,
verify queue-post native method behavior, or measure displayed frames/FPS.
Foundation mocks validate the exercised callback/retention seams rather than
the actual AppKit/Metal retain graph or native dispatch timing.
