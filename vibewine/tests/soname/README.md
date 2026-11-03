# Darwin soname extraction

Run `python3 vibewine/tests/soname/run.py` from the Wine source checkout.
The fixture feeds synthetic `otool -L` rows through the real generated shell/sed
fragment; it does not execute configure, a compiler, a native library or Wine.
It reproduces the old bare-install-name failure and covers absolute, rpath and
loader-relative names, metadata, whitespace, rejected names and first-match
selection. Other library patterns and unchanged DLL/ELF branches are checked.

The repair is owned by `WINE_CHECK_SONAME` in `aclocal.m4`. Autoconf/autom4te
were unavailable in the repair environment. The corresponding 27 Darwin
fragments in the existing Autoconf 2.72-generated `configure` were mechanically
synchronized; no complete Autoconf regeneration is claimed. The fixture checks
every expansion against the macro and verifies all other original bytes remain
unchanged against baseline `ae1d0e73b4384130dd6db33c49c28be02999b500`.

Fresh configure/build and actual library loading remain separate checks.
