# Included C fragments in makedep

`tools/makedep.c` must scan C include directives in `.inc` fragments just as it
does in `.h` and `.inl` files. Otherwise an object can depend on the fragment but
omit the fragment's generated IDL headers and nested headers. A fresh targeted
build may then select incompatible toolchain headers, and incremental builds
can retain stale objects.

From the Wine component root, with Python 3, a native `clang`, and `make`:

```sh
python3 -B vibewine/tests/makedep-inc/run.py
python3 -B vibewine/tests/makedep-inc/run.py --revision 4548da23 --expect-regression
```

The first command reports `MAKEDEP_INC_PASS checks=12 failures=0`. The predecessor
control compiles the complete `tools/makedep.c` from the named Git revision and
requires exactly nine specific regression failures; a crash, compiler failure,
scanner error, unexpected make error or different failure set is rejected.
Running the predecessor without `--expect-regression` fails normally. A different
native C compiler can be supplied with `--cc`.

The fixture compiles and executes the entire production scanner with its actual
Wine tool helper headers. Its minimal host `config.h` supplies only the required
configuration guard. No production parser body or emitted dependency rule is
replaced. The isolated mini-project exercises:

- C source to `.inc`, nested `.inc`, and ordinary header prerequisites;
- a nested reference to a missing global IDL-generated header and its real WIDL
  generation command in `make -n` output;
- unchanged-object acceptance and nested include/header/generated-header updates
  through the real `make -q` scheduler over the unmodified scanner output;
- rescanning newly added includes inside nested fragments and headers.

RPC headers are terminal input nodes in this miniature graph. Existing object
and generated-header markers are scheduling inputs, never executed artifacts.
The test does not invoke WIDL, compile a Windows module or claim SDK/header ABI
compatibility. It tests the scanner and build scheduling boundary responsible
for this failure. Bootstrap/configuration/tool targets are excluded from the
make probes with `-o`; the generated-header and object dependency edges under
test are not excluded. Ambient make flags are removed from child environments.

All generated sources, native tool binaries and mini-project state belong to
one temporary directory, removed on completion. No configured/shared Wine build,
installed runtime, game prefix, cache or Git index is changed.

After applying the scanner fix to a configured Wine tree, normal regeneration is:

```sh
make -C /absolute/path/to/configured-wine-build Makefile
```

The generated Makefile already depends on `tools/makedep`, which in turn depends
on `tools/makedep.c`. This rebuilds the scanner and regenerates dependency rules.
Check that WineMac `dllmain.o` now depends on `include/oleacc.h` and its IDL header
closure, then resume the desired module targets. `make depend` remains Wine's
ordinary explicit rescan route when adding new include directives.
