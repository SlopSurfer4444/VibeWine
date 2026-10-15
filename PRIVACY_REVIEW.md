# Current Wave7 export scope

Wave7 adds frozen production text, portable build/fixture adapters and authored
status documentation. Original/public hashes are recorded in WAVE7_ADAPTATIONS.json;
the complete frozen source-entry disposition is in WAVE7_ARCHIVE_INVENTORY.json.
Private runtime configuration, raw logs, captures, audit conversations, owner
paths and binary runtime artifacts are excluded. Upstream source attribution is
retained. A bounded path/credential scan is required for this staged export;
older scan and test claims below apply only to their named historical snapshots.
Current acceptance limits are in WAVE7_ACCEPTANCE.md.

## Historical material retained below

# Bounded export review

Allowlist: selected Wine source/baseline/patch and notices, exact DXMT guarded/diagnostic text source and licenses, source fixtures/controllers, newly written audit documents. No machine-management harness, private profiles, credentials, game source/data/saves, runtime binaries, screenshots, raw game logs or captures are included. Upstream public author credits remain intentionally intact.

Wine driver source hashes are compared with the exact candidate2 build receipt. The retained LGPL patch is applied to the included baseline and compared against the exported production source. DXMT source trees are byte-preserved; four upstream PNG fixture assets are omitted, as recorded in provenance. Verification JSON retains its historical build-time OFFLINE state; current installed identity is separately recorded in PROVENANCE rather than rewriting historical receipts.

Only fixture paths are adapted for repository-relative use; production source is not scrubbed or rewritten. Generated oleacc ROLE/STATE constants are limited to the exact declarations used by the provider fixture. No local generated-header path is exported.

All included files must decode as UTF-8. A bounded scan checks local owner paths, private temp paths, private-key delimiters, common credential prefixes and bearer authorization. This is not a universal secret-detection guarantee. Every exported file except FILE_MANIFEST.json is hashed in that manifest.

The complete exported runner passed on macOS in a disposable copy: prior display/Retina/native AX; six startup cases; five AX deadline/refresh comparisons; provider/native composition; six display-link groups; five monitor cases; ten fullscreen exact-method checks; 5066 synthetic trace checks and 19 CLI checks. Generated binaries and results stay outside the export. These are source-fixture results, not real-game CUA, fullscreen-hardware or performance acceptance.

Reviewed optional additions: original borderless helper/tests are explicitly contributed under the repository LGPL-2.1-or-later scope; no harness source is copied. Synchronization probe producer retains DXMT MIT and third-party notices. Its matching controller/analyzer source and synthetic tests are included without raw game telemetry. Both candidates are clearly uninstalled; installed be876 source is unchanged. Only bounded discovery/exit observations are prose; discovery candidate code and game disassembly remain excluded.

## Wave6 successor

Adds exact integrated production text, portable fixtures and sanitized observations; no raw game/browser logs, identifiers, screenshots, credentials or management harness. Retained prior renderer claims are historical. Current provenance and source hashes bind Wave6 separately. Native driver source/licenses remain unchanged. New generated build products must stay in disposable directories. Final manifest regenerated after all adaptations.
