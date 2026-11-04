# AX root fixtures

`python3 run.py` compiles and runs only the predicate/merge test; no AppKit
application or window is allocated. `python3 run.py --compile-appkit-only`
compiles the optional real-AppKit harness and reports its hash without launching
it; the temporary binary is removed. No observer, launcher, bundle metadata,
private intent, profile or admission tool is exported. Supplementary fixture
sources are explicitly distinguished from the frozen production archive.

The AppKit adapter models visible content, does not link the PE provider, and
does not establish integrated Wine, CUA or game behavior. Compile success and
pure predicate checks are not live AX acceptance.
