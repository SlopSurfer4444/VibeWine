# DXBC subroutine continuation and conditional-call regression

The runner rebuilds the actual `src/airconv/dxbc_converter_cfg.cpp` translation
unit. The existing LLVM14 support archive supplies the unchanged converter and
DXBC token parser. Nine pixel-shader token streams exercise direct, nested and
repeated calls, all four zero/nonzero conditional-call outcomes, mixed calls and
the no-call control. Each successful case checks the executable owned CFG path,
the number of subroutine bodies reached, preserved conditional gates, and the
production `convertDXBC()` LLVM module with `verifyModule()` under UBSan.

The continuation snapshot adapts Feifan He's LGPL-2.1-or-later upstream change
[`9839e55f98d4aca103957ee4132d6395a325a9ed`](https://github.com/3Shain/dxmt/commit/9839e55f98d4aca103957ee4132d6395a325a9ed).
The coupled local extension records the actual `if_true` call variant for CALLC,
after constructing it, rather than trying to extract a call from the preceding
conditional branch. The condition and false continuation are preserved. CALLC
operand order and zero/nonzero behavior follow the
[Microsoft instruction contract](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/callc--sm4---asm-).

```sh
python3 tests/vibewine/call-return-lifetime/run.py \
  --llvm /absolute/llvm14-prefix --support-build /absolute/existing-support-build
python3 tests/vibewine/call-return-lifetime/run.py \
  --llvm /absolute/llvm14-prefix --support-build /absolute/existing-support-build \
  --baseline-ref f871ff41c29c240eda3a6f962c9d48f9e6c2d1fd
```

The baseline oracle requires an exact `bad_variant_access` rejection for all
conditional-call programs. Tests enable exceptions solely to catch and identify
that wrong-variant failure; the ordinary no-exception production build would
terminate there. Direct-call stale storage can retain plausible pointer bits,
so a passing ordinary baseline is not evidence of a valid object lifetime.

A second compile injects `call->return_point = nullptr` into a temporary copy
of the production CFG TU immediately before replacing the call variant. All type
and header definitions stay identical to the retained support archive; no custom
destructor or mixed class definition is linked. The fixed snapshot must already
be independent of the overwritten continuation. The old direct/nested/repeated
paths fail the exact outside-owned-graph oracle, while the fixed paths pass.
This is a pre-replacement fault-injection check, not an ASan diagnosis or a
literal simulation of variant destruction. Ordinary unmodified production-type
runs separately check normal behavior; conditional-call failures reproduce
without fault injection. All temporary sources and binaries are removed on exit.

The support/LLVM trees are read-only. The runner never rebuilds release artifacts,
installs a renderer, selects a runtime, or launches a game. These are compiler/CPU
checks; Metal execution and game compatibility remain untested.
