# Wave7 recorded build recipes

These explicit-input templates preserve the archived compile/link arguments while
replacing private paths. They compile source; they do not discover an installed
Wine, install, sign, launch Wine, or assemble a runtime. They have not been
exercised as a clean-room or bit-identical rebuild on another SDK.

Use Python 3 and an appropriately configured macOS clang/Wine build environment.
The Wine base archive identity is recorded in provenance. Supply matching source,
generated headers and configuration; configured Wine and SDK/toolchain inputs are
external dependencies, not synthesized by these templates.

`build-recipes/run_recipe.py TEMPLATE --input NAME=/absolute/path ...` prints
resolved argv without executing. Add `--execute` to compile sequentially into a
fresh component subdirectory of OUTPUT. Never reuse a failed output component.
No shell expansion occurs. Required placeholder names are reported if missing.

Inputs:

- PUBLIC_ROOT: this repository root; production sources are read from native/wave7
  and ntdll/wave7. Exact final snapshots are used, without prior local overrides.
- WINE_CONFIGURED_BUILD: matching configured Wine build, including generated
  headers. Commands execute with this explicit working directory.
- WINE_SOURCE_TREE: corresponding complete Wine source tree.
- NATIVE_DEPENDENCY_DIR: explicitly supplied donor ntdll.so and win32u.so; expected
  donor hashes and static ABI constraints remain in sanitized provenance.
- WINE_PREFIX: desired configure-time prefix for ntdll BINDIR/LIBDIR/DATADIR;
  SYSTEMDLLPATH stays empty. These are compile-time constants, not install actions.
- OUTPUT: a separate writable scratch root. Native outputs use OUTPUT/native,
  bridge OUTPUT/bridge, ntdll OUTPUT/ntdll. Run native before bridge with the same
  OUTPUT; bridge links the freshly built native driver and explicit donor ntdll.

Templates: native/wave7/build-native.commands.json,
native/wave7/build-bridge.commands.json, ntdll/wave7/rebuild-ntdll.commands.json.
The native template includes the original version/table/Unix-parameter layout
assertions. These do not establish all behavioral ABI compatibility. Native and
bridge retain donor dependency distinctions; do not substitute the new ntdll
merely because it exists in OUTPUT.

Renderer uses renderer/wave7/build.py with LLVM_MINGW, DXMT_RELEASE and WINEBUILD,
plus xxd and the retained donor helper library identity. Its own directory must
be a writable disposable checkout because it creates build/ next to source/.

Optional ntdll path verification is separate:
`python3 ntdll/wave7/check_runtime_paths.py --source ntdll/wave7/source
--flags OUTPUT/ntdll/resolved-path-flags.json --ntdll OUTPUT/ntdll/dlls/ntdll/ntdll.so
--engine /explicit/disposable/engine --output /fresh/path-check-output`.
This compiles and executes a small extracted path helper against caller-supplied
engine resources; it never launches Wine. Do not include it in offline unit-test
aggregation. The caller supplies a complete disposable engine layout.

Signing/relocation and runtime assembly are intentionally separate from source
compilation; original compiled and selected signed artifact hashes differ.
Historical receipts are evidence, not promises that these recipes were executed.
