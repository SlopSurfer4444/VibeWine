# Export checks

- Allowlisted selected Wine source directories, retained patch, license/authors/version, bounded source fixtures, and newly written audit documentation only.
- No runtime binaries, game files, saves, account profiles, screenshots, network captures, raw telemetry, machine-management source, deployment state, credential files, or build receipts were copied.
- Production driver files were byte-compared with the integrated candidate (46 exact matches).
- Patch applied cleanly to the included archive-derived base directories. Exported source is derived from that application, not a separately edited facsimile.
- Text scan found no local owner username/path, private temporary paths, private-key delimiters, common GitHub/OpenAI credential prefixes, or HTTP authorization values. This is a bounded scan and manual source selection, not a universal secret-detection guarantee. Upstream public contributor credits and emails in AUTHORS/source notices are intentionally retained.
- Every exported file is UTF-8 text; manifest provides path, size, and SHA-256, excluding the manifest itself.
- Exported macOS offline fixture suite passed: display, Retina, native AX completion regression, completion read, native transport, roles, scheduling, stability. Compiler unused-function warnings remain in test doubles. No games or Wine processes were launched by these checks.
- The provider harness is a template and is not counted as executed. No bundled runtime or proprietary graphics implementation is published.
