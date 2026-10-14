# Bounded export review

Allowlist: selected Wine source/baseline/patch and notices, exact DXMT guarded/diagnostic text source and licenses, source fixtures/controllers, newly written audit documents. No machine-management harness, private profiles, credentials, game source/data/saves, runtime binaries, screenshots, raw game logs or captures are included. Upstream public author credits remain intentionally intact.

Wine driver source hashes are compared with the exact candidate2 build receipt. The retained LGPL patch is applied to the included baseline and compared against the exported production source. DXMT source trees are byte-preserved; four upstream PNG fixture assets are omitted, as recorded in provenance. Verification JSON retains its historical build-time OFFLINE state; current installed identity is separately recorded in PROVENANCE rather than rewriting historical receipts.

Only fixture paths are adapted for repository-relative use; production source is not scrubbed or rewritten. Generated oleacc ROLE/STATE constants are limited to the exact declarations used by the provider fixture. No local generated-header path is exported.

All included files must decode as UTF-8. A bounded scan checks local owner paths, private temp paths, private-key delimiters, common credential prefixes and bearer authorization. This is not a universal secret-detection guarantee. Every exported file except FILE_MANIFEST.json is hashed in that manifest.

The complete exported runner passed on macOS in a disposable copy: prior display/Retina/native AX; six startup cases; five AX deadline/refresh comparisons; provider/native composition; six display-link groups; five monitor cases; ten fullscreen exact-method checks; 5066 synthetic trace checks and 19 CLI checks. Generated binaries and results stay outside the export. These are source-fixture results, not real-game CUA, fullscreen-hardware or performance acceptance.

Reviewed optional additions: original borderless helper/tests are explicitly contributed under the repository LGPL-2.1-or-later scope; no harness source is copied. Synchronization probe producer retains DXMT MIT and third-party notices. Its matching controller/analyzer source and synthetic tests are included without raw game telemetry. Both candidates are clearly uninstalled; installed be876 source is unchanged. Only bounded discovery/exit observations are prose; discovery candidate code and game disassembly remain excluded.
