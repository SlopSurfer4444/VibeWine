# Checking and building Wave7 sources

On macOS with Python 3 and Xcode Command Line Tools, run from this repository:

```sh
python3 tests/run_wave7.py
```

The runner copies source into a temporary directory and runs small source-connected
C/C++/Objective-C fixtures and synthetic trace/evidence tests. It does not build a
full runtime, install anything, start Wine or launch a game. `--mutation-check`
adds WSI negative controls; `--include-history` separately runs retained Wave6/
Wave5 checks. Generated binaries and results remain in the disposable copy.

Full component compilation needs matching external Wine configuration, generated
headers, dependency libraries, SDK and compiler. See
[build-recipes/README.md](build-recipes/README.md) for explicit inputs and commands.
Recipes print by default and require `--execute` to compile. The recorded native
build used Apple clang 21.0.0; a clean-room or bit-identical public rebuild has not
been demonstrated. Original unsigned and selected signed hashes are distinct.

The renderer build uses LLVM_MINGW, DXMT_RELEASE and WINEBUILD environment inputs;
see renderer/wave7/build.py. Run it from a writable disposable copy. The optional
GPU smoke fixture is compile-only here: set LLVM_MINGW to its bin directory, run
renderer/wave7/smoke/build.py and verify_imports.py. It is not in offline defaults.
The ntdll path probe requires an explicitly supplied disposable engine and is also
excluded from offline defaults. Never use full build recipes as an install script.

Source notices and licenses are retained. Root LICENSE, upstream notices and the
renderer component license govern their respective material. No license grant
for proprietary donor binaries is implied; those binaries are not distributed.
