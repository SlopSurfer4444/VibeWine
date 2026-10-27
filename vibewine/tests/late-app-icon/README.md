# Late application icon propagation

This adapts Gabe Zimbric's
[8df085af744d252b81be8a7d96cf1f01086c156b](https://github.com/gzimbric/wine-arm64-macos/commit/8df085af744d252b81be8a7d96cf1f01086c156b),
whose pinned `cocoa_app.m` notice and `COPYING.LIB` retain LGPL-2.1-or-later.
The local base is `5d4b43e48f48d83254468596a0df8592d62e3242`.

An icon can arrive after foreground promotion has already applied the previous
icon. The existing main-thread application-icon callback now reapplies the newly
stored icon when the application is already regular. The policy is read at
execution time; accessory and prohibited applications are not promoted.
Unlike the donor's conversion-method hunk, the update covers both CGImage-array
and the retained CrossOver URL-backed routes. Existing masked-icon conversion,
fallback sizing, retained property ownership and nil/default behavior are kept.
No native/PE ABI or feature bits change.

```sh
python3 vibewine/tests/late-app-icon/run.py
python3 vibewine/tests/late-app-icon/run.py --translated
```

The runner extracts the complete production conversion method and public icon
entry, preserving the original source notice, and compiles them with ASan/UBSan.
Real AppKit/CoreGraphics images exercise masked/fallback/URL/empty routes.
The real Wine entry uses its CFRunLoop request queue. This fixture substitutes
that transport with GCD main-queue dispatch. Worker submissions and an observing
application check the extracted callback's thread and policy behavior without
creating NSApplication or a Dock icon. The complete Wine request queue is untested. Queued promotion/demotion controls check execution-time policy.
The original base must fail exactly five named propagation checks and preserve
all unaffected-path controls. The candidate passes 36 assertions.

This is a production-function and native ownership/dispatch fixture. It does
not observe the real Dock, change focus or Spaces, launch a Wine application,
establish a linked complete Wine build, or prove installed/runtime/game behavior.
The masked-icon utility is a controlled conversion seam; its own implementation
is outside this test. Temporary sources, binaries and TIFF images are task-owned
and removed when the runner exits. No persistent producer is introduced.
