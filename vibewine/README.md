# VibeWine — complete Wine component source

This branch is the complete vendor Wine source tree, with VibeWine changes applied in place. The top-level Wine README and build system remain intact. It is a development source branch, not a downloaded binary runtime, a verified fresh whole-engine build, or a claim of FPS improvement.

## Provenance and history

`vendor/wine11-import` imports all 11,094 regular files from the corresponding-source archive, SHA-256 `7be5819017b34f09670293f2be7ed9f4476734b8f42dab121a8b74e6619c92a8`. No files are reconstructed from binaries. The archive's packaged `RUNTIME-BUILD.md` identifies its source as CodeWeavers public CrossOver 26.3 / Wine 11.0, with earlier vendor modifications (Steam CEF single-process, exception handling, generated-code cache). That statement is package provenance, not an independently verified exact Git revision. The upstream Git ancestor and original download URL of this local archive are **unknown**.

The import is deliberately an independent Git root. Do not present it as upstream Wine 11.0, fabricate parent commits, or merge unrelated upstream histories. [Wine upstream](https://gitlab.winehq.org/wine/wine) and its [GitHub mirror](https://github.com/wine-mirror/wine) are references for reviewed backports; a future rebase needs explicit vendor-delta reconstruction.

`source-overlay.json` records the public audit commit and exact overlay paths/hashes in application order. Later entries supersede earlier entries at the same destination. Wine native-driver, MSAA and ntdll changes are integrated into their normal `dlls/` paths. DXMT and its native winemetal bridge are separate components, not silently substituted here. No game executable or private game profile is included.

## License

Original AUTHORS, LICENSE, LICENSE.OLD, COPYING.LIB and all per-file notices are retained. Wine is LGPL-2.1-or-later; bundled libraries and assets retain their own licenses. The archive includes ordinary source-resource binaries (fonts, NLS tables, test images), not installed Wine executables or proprietary game libraries. VibeWine additions retain their source notices. Audit the complete corresponding source and dependency licensing before distributing compiled packages.

## Building

Use the top-level README and configure help for Wine requirements. The matched historical macOS build used Apple clang for x86_64, LLVM-MinGW 20260826-ucrt for PE, Bison 3.8.2 and gettext, with `--host=x86_64-apple-darwin --build=x86_64-apple-darwin --enable-archs=i386,x86_64 --with-mingw=llvm-mingw --disable-tests --without-x --without-gstreamer --without-vulkan`. FreeType/GnuTLS require their matching external headers and libraries. A reduced module compile may disable both, but that is not equivalent to the production feature set.

Build outside the source tree. This branch contains no downloaded toolchains, donor binaries, private deployment harness, account data or game saves. A distributable engine still requires dependency packaging, paired PE/Unix ntdll verification, signing and runtime acceptance. Do not overwrite working game environments with an unverified full build.

## Contribution scope

General native-window, accessibility, ABI and synchronization changes belong here with focused regression tests. Executable-specific policy belongs in an opt-in profile in the appropriate component, not in universal behavior. Submit PRs against `component/wine`; the main VibeWine project pins a reviewed component commit. Source checks, builds, installation and observed gameplay are distinct claims.

## Source integration verification (2026-10-18)

A clean out-of-tree configure and compile of this integrated tree succeeded for x86_64 Unix `ntdll.so`, `winemac.so` (including the `win32u.so` dependency), and i386/x86_64 PE `oleacc.dll` and `winemac.drv`. The compile used the reduced dependency flags above (`--without-freetype --without-gnutls`). Bison 3.8.2 must be on PATH for both configure and make; the system Bison was too old in an initial invocation. This is a module build check, not a whole Wine distribution build or a live game/ABI/runtime acceptance. No produced binaries are tracked or installed.

Run `python3 vibewine/verify_source.py` to check 11,105 source files against the archive manifest and ordered current overlays. Historical vendor whitespace and one existing extra blank line in `ax_window_identity.inc` are retained byte-for-byte rather than mixed into the integration delta.
