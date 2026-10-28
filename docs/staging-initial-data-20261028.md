# Staging logical initial-data copy — 28 October 2026

Source baseline: `8cf59a015b88361ed6f6320eb8a781c68b3b36c1`.
Finding: `PRO-CPU-01-STAGING1D`, admitted in the frontier CPU review.

`CreateStagingTextureInternal` previously assigned a zero source row pitch for
1D, then used that value as its memcpy length, losing the entire initial
payload. Using aligned destination pitch instead would read beyond a caller's
tightly allocated logical row. Other dimensions also copied padding when their
source pitch exceeded logical width; the fixture reproduces a historical 2D
source overread when the last row contains only the required logical bytes.

The template now obtains an unaligned layout through the existing production
`GetLinearTextureLayout(..., false)` before allocating the subresource. It uses
logical row bytes for 1D regardless of the supplied pitch, and caps 2D/3D copy
length to that logical width while retaining the existing source-pitch cap.
Destination row/image strides and 3D source slice strides are preserved. The
logical layout also gives block-row counts for compressed textures. Null initial
data does not issue the extra layout query, and either layout failure returns
before allocation/publication/copy.

Focused verification: the production-body fixture in
`tests/vibewine/staging-initial-data/` runs 46 cases / 147 subresources under
ASan+UBSan on arm64 and x86_64/Rosetta. The historical 1D-only baseline fails
60 logical payload checks; the full baseline and a deliberate aligned-length
repair produce ASan source overreads. The fixture guards payloads, row/image
padding, source contents, row counts and both layout-failure paths. Independent
source/fixture review found no blocking issue; the suggested alignment-boundary
cases were added before the final run. `git diff --check` passes.

These are source and CPU-fixture results. Renderer integration/build is owned by
the campaign coordinator. Real D3D11 CreateTexture/Map/GPU acceptance is still
unperformed, as are installation, gameplay and performance measurements. The
fixture README records adapters, API follow-up and artifact retention limits.
No runtime, game, installation, push or Git index/commit action belongs to this
branch's delivery.
