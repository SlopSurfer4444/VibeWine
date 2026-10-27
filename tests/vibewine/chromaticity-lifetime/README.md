# Color-profile tag ownership

`python3 tests/vibewine/chromaticity-lifetime/run.py` compiles the complete actual
production ICC tag reader against CoreFoundation with ASan/UBSan. The copy seam
returns a real retained CFData; real retain counts detect leaked acquired references.
Seven cases exercise absent/empty/short/long/wrong-signature/valid/zero-sum input,
128 times each. Failure leaves output untouched; valid and zero-sum behavior is
unchanged. No display, ColorSync profile acquisition, renderer or game is launched.

`--baseline-ref bbf22e6a41b383fe67be9dbba80c9f682908c01a` requires exactly six
leaking paths with 128 extra references each, with all semantic checks passing.
An arbitrary crash or sanitizer failure cannot count as that expected failure.
Temporary executables are bounded by timeouts and removed by the test owner.

Adapts Feifan He / CodeWeavers' LGPL-2.1-or-later DXMT change
[`a741a3fa`](https://github.com/3Shain/dxmt/commit/a741a3fad14a4bc5fc899f85bfa35de8f933e74b).
VibeWine also releases the acquired tag on the donor's missed signature-error exit.
The surrounding display snapshot already releases its profile and fallback, so
the newer upstream display/presenter rewrite is not imported. This is a bounded
ownership repair, with no game-memory or FPS claim.
