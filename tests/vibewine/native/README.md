# Native bridge regression fixtures

Run on macOS with an Apple SDK containing Metal, Foundation, Cocoa, ColorSync,
QuartzCore and MetalFX headers:

```sh
python3 tests/vibewine/native/run.py
```

If Xcode is not the selected developer directory, set `DEVELOPER_DIR` to its
`Contents/Developer` directory. The runner uses `xcrun clang`. It writes generated
sources, test executables and its mock dylib into an automatically removed
system temporary directory. It does not install a runtime, launch Wine or a game,
or change display/cursor state.

The runner syntax-checks the complete current native translation unit in both
Wine and `DXMT_NATIVE` modes, including checked layer apply, with `-Werror` and
SDK deprecation warnings disabled. Runtime fixtures use `-fblocks
-fno-objc-arc -Wall -Wextra -Werror -fsanitize=address,undefined`, linking
Foundation, Metal and CoreFoundation. Snapshot tests suppress unused declarations
in their intentional mocks. All subprocesses have a 60-second timeout.

Fixtures compile against the repository's actual extension includes and extract
snapshot request/response types from its current `winemetal.h` and
`winemetal_thunks.h`:

- Shared listener: early close/start, pre-start and late callbacks, duplicate
  starts, stop-before-run, batch draining, 300 concurrent shutdown schedules,
  signal thread affinity and disposal.
- Presented feedback: protocol/identity/clock, duplicate and abandoned callbacks,
  bounded rings and pending sessions, close-before-callback.
- Display snapshots: main/device lock inversion, cold EDR, eager Retina priming,
  metadata concurrency, isolated display IDs, topology/close refresh and disposal.

These are deterministic native mock/lifetime regressions, not a full runtime
link, GPU presentation or gameplay acceptance test. The checked layer apply path
is compilation-covered; actual AppKit/Metal application requires runtime testing.
