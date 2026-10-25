# Headless pass-optimization image guest

`guest.cpp` reuses the local gpu-wave bounded query/readback patterns. It creates
one FL11 hardware device, procedural shaders, 64×64 RGBA8 textures and no window
or swapchain. It does not launch a game or subprocess, touch a prefix/cache,
select a runtime, or perform network operations. It prints loaded module paths.

Four sequences check five byte-exact images: draw→clear→partial draw (folded
clear); draw→clear→copy (standalone clear); draw→copy→clear→partial draw (the
reader must retain the old image); and draw→unbind/rebind→partial draw without
clear (logical pass merging and repeated VS/PS residency declarations). Every
pixel and mapped row pitch is checked. A white SRV is used by both shader stages.
The public API guest does not itself prove internal eligibility, pass merging or
declaration counts; the production-bound CPU fixture checks those algorithms.

Compile with the reviewed LLVM-MinGW toolchain, without executing the result:

```sh
"$LLVM_MINGW/x86_64-w64-mingw32-clang++" -std=c++20 -O2 -Wall -Wextra -Werror \
  -static guest.cpp -o /absolute/private/output/pass-optimizations.exe \
  -ld3d11 -ldxgi -ldxguid -ld3dcompiler_47
```

The integration owner runs baseline and candidate in disposable prefixes with
matched modules, external host deadline and network isolation, verifying module
paths/hashes and natural exit zero. Require four CASE_PASS lines, final
`PASS cases=4 images=5`, and no FAIL. The `--negative-pixel` run must first verify
the unmodified image, then reject a one-byte changed golden with
`NEGATIVE_REJECTED pixel` and natural exit 20. Earlier failure is not an accepted
negative control; unsupported behavior is failure, not a skip.
Global cooperative budget is 30 s; query/readback polls have 5 s deadlines and
the independent watchdog ends the process after 40 s (failure). Actual GPU work
may survive process termination; the parent owns isolation/cleanup.

This source is prepared for sequential GPU validation; authoring and CPU tests
do not establish execution, internal traffic reduction, FPS or game compatibility.
No capture producer is introduced. Any resulting private binaries, logs and
reports belong to the existing integration owner's test-artifact contract.

`build.py` reuses the sibling gpu-wave bounded builder on byte-identical temporary
source inputs. Supply its reviewed absolute `--compiler`, new absolute `--output`
and optional identity arguments; the builder never executes Wine or the guest.
The wrapper additionally prints the reused builder's SHA-256. To check root's
three captured isolated runs, preserving their actual natural statuses:

```sh
python3 analyze.py --baseline /absolute/baseline.stdout --baseline-exit 0 \
  --candidate /absolute/candidate.stdout --candidate-exit 0 \
  --negative /absolute/negative.stdout --negative-exit 20
```

The analyzer rejects missing, oversized, extra or out-of-order records. It is a
captured-output paired-run adapter, not an isolation boundary or module-hash
attestation; use the integration owner's admitted sandbox/runtime runner.

`run-paired.py` can invoke three sequential runs through two absolute, executable,
nonsymlink **parent-supplied isolated launcher scripts**. Each launcher receives
`guest.exe` and guest arguments, must forward the natural child status, and owns
the admitted matched runtime, disposable prefix/private server, external network
denial, private cache/temp paths and cleanup. The adapter supplies none of these
and has no installed-runtime fallback. It captures baseline, candidate and
candidate negative-control output, preserving natural statuses in `run.json`.
It caps each stream at 64 KiB (overflow fails), each process at 50 s and receipt
at 32 KiB. Timeout kills only its child process group and is failure; the parent
must reconcile any private-server/GPU work and verify actual module mappings.

```sh
python3 run-paired.py --baseline-launcher /absolute/admitted/baseline-launcher \
  --candidate-launcher /absolute/admitted/candidate-launcher \
  --guest /absolute/quarantine/guest.exe --output /absolute/new/paired-evidence
```

The integration owner retains at most three 128 KiB run logs plus the 32 KiB
receipt under the existing seven-day raw-evidence policy and active/analysis/
recovery holds. Its normal bounded cleanup method applies; this adapter installs
no cleaner or background producer. Compiled source/binary/build evidence uses
the reused builder's existing byte caps and the same parent retention contract.
