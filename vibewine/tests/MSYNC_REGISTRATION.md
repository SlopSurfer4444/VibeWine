# MSync registration regression check

Run from any directory:

```sh
python3 vibewine/tests/msync_registration.py --source-dir . --baseline-ref 7469fcf53c4f537dfe305e961bffb0495d7b750d
```

`--source-dir` is the current candidate component. `--baseline-ref` is read with `git show`; no checkout or source mutation occurs. Clang with AddressSanitizer and UndefinedBehaviorSanitizer is required. Compilation is capped at 60 seconds and each fixture at 20 seconds. Generated sources and binaries live in TemporaryDirectory and are removed on exit.

The runner compiles actual production client/server function bodies extracted from both revisions. The known baseline must compile and fail exactly the six named assertions; build failure, timeout, unexpected crash, extra sanitizer diagnostic or any other result is a test failure. The current candidate must pass all sixteen assertions. No network or Wine process is started.

Checks cover successful registration followed by early contention, balancing ordinary and alert waiter counts, registration/removal ordering, immediately ready server object at index one, redundant unregister, 1000 repeated lost races, normal wait, ulock timeout, expired deadline and preservation of an unrelated waiter. The queued work-signal message and its actual signal_all_internal body are included.

Mach transport, pool allocation/free, wake delivery and ulock results are deterministic seams. This does not establish kernel scheduling coverage, runtime installation, gameplay or performance. The warning-once pool-exhaustion hunk is not tested.

Patch origin: Gauthier Piarrette, [highball-engine commit 84ff7c76141bad87d5025c801fe92a0771e31e2a](https://github.com/gauthierpiarrette/highball-engine/commit/84ff7c76141bad87d5025c801fe92a0771e31e2a). Exact [source patch](https://github.com/gauthierpiarrette/highball-engine/blob/84ff7c76141bad87d5025c801fe92a0771e31e2a/patches/0014-msync-unregister-early-return.patch), 4177 bytes, SHA-256 `09c46f062f7e936bb331274192ce3436dbd18242a1b1618ca4944b6193158715`.

Wine and these patches are LGPL-2.1-or-later. Retain original MSync copyrights for Zebediah Figura (2018) and Marc-Aurel Zent (2023), Gauthier Piarrette attribution, and the existing source/license notices. The generated fixtures copy the production license header.
