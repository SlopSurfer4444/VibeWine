# Fixed-output MetalFX CPU fixture

Run `python3 tests/vibewine/fixed-output-metalfx/run.py --baseline-ref e11476e3 --sanitizers`.
Use `--arch x86_64` for an additional translated CPU build on macOS, if supported.
`--sanitizers` enables AddressSanitizer and UndefinedBehaviorSanitizer. Every
compile uses `-Wall -Wextra -Werror -Wno-unused-parameter`; compile/run timeouts
are 60/20 seconds. Both feature macro values, zero and one, are exercised.

The runner extracts the actual current constructor configuration block,
`ResizeBuffers`, `ApplyLayerProps`, and queued Present dispatch branch into
controlled mocks and includes the actual production spatial geometry helper.
It compiles and runs in a `TemporaryDirectory`; it does not
start Wine, a Metal device, a game, or an installed runtime. `--baseline-ref`
uses read-only `git show`; `--baseline-source` accepts an already exported file.
The historical methods must compile and then fail with exactly
`FAIL: fixed output texture dimensions`.

Checks cover independent public render/output dimensions, explicit render resize,
DS3 zero-axis preservation through the resolver interface, refusal and client
query failure, invalid output pairs before allocation, transaction rollback for
input/output allocation failure, nil scaler/fence fallback without stale state,
aspect/equal/downscale/MSAA/HDR fallback, and legacy factor/failure behavior.
The extracted constructor block checks default-off configuration, explicit
opt-in, compile-time disabling, and the ordinary swapchain template. Queued
Present checks exercise nil scaler/fence fallback, HDR color-space changes,
source selection, statistics, semaphore release, and captured resource lifetime
across resize. The factory/device gate and independent native scale remain
source assertions or outside the executed fixture scope.

Eighteen mutations must compile and fail behaviorally: coupled public dimensions,
coupled layer dimensions, early publication, nil scaler retention, stale scaler
retention, aspect mismatch admission, equal-size admission, MSAA admission, HDR
colorspace admission, HDR format admission, invalid size admission, and ignored
legacy scale factor; default-on configuration, ignored template/compile gates,
missing fallback present, reading current input after resize, and ignoring HDR
changes at Present. A negative control must exit one and print `FAIL:`;
compilation failure, crash, or sanitizer failure is never an accepted control.
JSON on stdout records results and production source hashes.

Limits: Win32 sizing, resource allocation, and resource ownership are mocks; the
DS3 resolver itself is not extracted. This does not verify the full constructor
or Present1, factory/device admission, native resource retention, presenter
aspect-fit rendering, input alignment, Retina/monitor transitions, native
failures, delivered frames, gameplay, or FPS.
