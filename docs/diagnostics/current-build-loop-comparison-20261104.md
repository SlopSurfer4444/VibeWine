# Canonical builds and rendered loop-motion comparison — 4 November 2026

Current source is an independent recursive workspace, rather than a linked
checkout inside historical experiment outputs. Git objects, component and nested
submodule trees were checked against the predecessor; no alternates remain.
Compiler/SDK inputs were consolidated into a project-local dependency directory.
Installed games, prefixes and native Steam were not moved.

## Source and build acceptance

- DXMT `a49a51c5`: two fresh complete 164-step builds from identical source.
  Only `vibewine_constant_loop_motion` differs; cleanup is enabled in both and
  the optional ARM-worker bridge build ID is empty in both. Each closure contains
  the matched converter/static AIR library, PE libraries, native bridge and
  generated Metal helper. Neither is installed in the game.
- Wine `3773a58c`: fresh configure and all seven selected NTDLL/server/WineMac
  targets pass using current source/dependencies. This is not a complete new
  redistributable Wine engine.
- A real Darwin configure bug was found: a bare FreeType install name retained
  indentation and compatibility-version text in `SONAME_LIBFREETYPE`. The owning
  macro and its 27 generated Darwin expansions now select the exact basename.
  Fifteen shell fixtures, three other-library controls and independent review
  pass. Full Autoconf regeneration was unavailable; mechanical synchronization
  is explicitly checked. Fresh configure produces `libfreetype.dylib`; a separate
  native process successfully loads it, initializes FreeType2.14.3 and frees it.
  Wine font rendering and installed runtime activation are not established.
- Benchmark `17af8153`: opt-in `--image-proof` reads actual RGBA8 shader output
  at frame120 before Present, warmup and trace admission. The guest publishes
  exact geometry/count/FNV64; the host validates retained bytes, computes SHA256
  before ARM, and rechecks identical proof/bytes after natural exit. Forty-two
  host checks and the warning-clean Windows guest build pass. Existing defaults
  and the separate calibration/go barrier remain unchanged.

## Actual matched comparison

C0 disables loop motion; C1 enables it. All cases use a 2560×1440 backbuffer,
960×540 actual client, vsync0, two-second warmup, 240 measured frames/Present
calls and a ten-second ceiling. Each completed native module/incarnation binding,
three-Present calibration, coherent readback, generation2 observation, natural
exit and exact private-server wait. Both private cleanup holds are absent.

| Order | Variant | Scene | Actual elapsed seconds | Outside-Present wall median ms |
| --- | --- | --- | --- | --- |
| 1 | C0 | pixelheavy | 5.03571 | 0.10165 |
| 2 | C1 | pixelheavy | 6.15566 | 10.40545 |
| 3 | C1 | pixelheavy | 6.13343 | 0.29185 |
| 4 | C0 | pixelheavy | 6.16719 | 0.27375 |
| 5 | C0 | cube | 1.96901 | 0.14655 |
| 6 | C1 | cube | 1.96863 | 0.13930 |
| 7 | C1 | cube | 1.97681 | 0.15700 |
| 8 | C0 | cube | 1.96941 | 0.11625 |

All four full images match byte-for-byte within each scene, not just a sentinel:

- pixelheavy SHA256 `a71eb3ae3055df713bdaeca8534b29792c669cc3a248b7dc1e3a2520bb824525`
- cube SHA256 `81260eb2c9f35fed20734cb5816fdeeff1e4195d9b8df97ee7e8df2332b4d013`

This is exact equality at one fixed shader frame per scene, not a general shader
corpus proof. Independent recomputation verified 48 evidence files/136,521,221
bytes, every CSV row0–239, mapped-module hashes, source/options bindings and
full image equality. Curated summary SHA256:
`4b7a5c40bf919435e8b1e36d08a82b6b589a115d4183dad163f8620481c9f2c9`.

**No improvement is demonstrated; loop motion remains default off.** The heavy
reference drifts +22.47% while the battery falls10→9%. Time/order drift is
observed; thermal, power and background causes are hypotheses. R2’s 10.40545ms
is a confirmed wall-time partition, not CPU consumption. Full native presentation
coverage remains false. These numbers are neither displayed FPS nor DS3 gains.
Apple’s final compiler already doing equivalent motion remains an unproven
explanation for the absent gain.

After independent review, analyzed binary/decoded traces and duplicate images
were retired with exact hashes. CSV, run/result JSON including calibration and
analysis, build/preparation receipts, and one real image per scene remain.
No game was launched and no new compiler/renderer option was enabled in DS3.

The preceding [resolution sweep and produced-IR finding](gpu-resolution-loop-motion-20261104.md)
and [CPU ABBA negative result](hotpath-economy-20261104.md) remain distinct
experiments. The next causal GPU comparison needs stable power/order controls
and an actual execution-cost attribution rather than inferred display FPS.
