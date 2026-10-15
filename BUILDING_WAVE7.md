# Checking and building current Wave7 sources

On macOS with Python 3 and Xcode Command Line Tools, run `python3 tests/run_wave7.py` from this repository. The current focused runner checks frozen source hashes, copies the export into a disposable directory, and executes event 77, persistence, R1, B1/B2, pure AX, consumer and mask checks. `--sanitizer none|address|thread` selects a narrower mode for event 77/persistence; default `all` uses all three. R1 and clear-pipeline runners retain their own regular/sanitizer controls. No Wine/game or AppKit application is launched by the default aggregate.

The previous aggregate is preserved under history/wave7-checked-mask as historical source. Its old --mutation-check/--include-history flags are not current-runner options. Earlier component fixtures remain available separately; they are not automatically accepted against a changed production tree.

Full compilation requires explicit external compiler, matching configured Wine source/generated headers, SDK and dependency libraries. See build-recipes/README.md. Recipes print by default; --execute compiles into fresh scratch output. Public path adapters have been statically checked, not clean-room or bit-identical rebuilt. GPU guests are compile-only here and are excluded from offline defaults. ntdll path probes require a separately supplied disposable engine. No build recipe installs or activates a runtime.

Licenses and source notices remain with each component. Proprietary donor binaries, the complete Wine archive/configuration and game/runtime binaries are not redistributed.

## Current reviewed graphics successor

The current native recipe compiles all 25 driver translation units plus the ABI assertion and link. The bridge recipe preserves the recorded baseline-v6 driver and donor ntdll link inputs; it does not silently substitute the final runtime driver. Supply BASELINE_NATIVE_DIR and NATIVE_DEPENDENCY_DIR with exact hashes in WAVE7_PROVENANCE.json. No donor binaries are distributed. Renderer inputs remain LLVM_MINGW, DXMT_RELEASE and WINEBUILD.

Current focused public fixtures are documented under renderer/wave7/next-tests and native/wave7/ax-root-tests. Run them in a disposable copy; real AX observer/GUI execution is separate and is not part of offline defaults. Sanitized source/build/ABI/signing receipts are under evidence/next-graphics. They retain original hashes through WAVE7_ADAPTATIONS.json. Public path adapters are not claimed executed unless explicitly documented. Existing historical fixture suites remain available; they do not automatically validate later source changes.

The full private driver build emitted one new nonfatal declaration-after-statement warning, alongside inherited SDK warnings. This did not prevent compilation; no source change was made merely to suppress it. No complete runtime reproduction or game acceptance is established by these recipes.
