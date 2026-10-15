# Focused successor source fixtures

Run `python3 renderer/wave7/next-tests/run.py` from the export root. The runner
checks frozen production hashes, copies the export to a temporary directory, and
runs event77 and persistence fixtures in regular, ASan and TSan modes, R1 in
regular and ASan/UBSan modes, clear-PSO tests, pure AX predicate/merge tests,
and the retained consumer and mask checks against current sources.
`--sanitizer none|address|thread` narrows only event77/persistence modes.

These runners extract current production code; they do not load a Wine runtime,
game, GPU or AppKit application. R1 reproduces the combined positive scenarios;
its earlier negative-control evidence is historical and is not silently rerun.
The clear-PSO owner runner retains both exact baseline and semantic mutation
controls. Persistence mutations remain explicit runner options. Its `--baseline`
mode additionally requires `--baseline-archive PATH`, pinned to the earlier
source archive in `persistence/baseline-binding.json`. No private archive is
discovered automatically. Fixture assertions and production bodies are retained.

Use these focused presenter/event77 checks for the successor. Earlier exported
presenter fixtures describe the previous snapshot and may not accept new method
signatures. The optional AX AppKit source has a separate compile-only command;
neither this aggregate nor the AX runner launches it. Public path adapters have
not acquired execution evidence merely by being exported.
