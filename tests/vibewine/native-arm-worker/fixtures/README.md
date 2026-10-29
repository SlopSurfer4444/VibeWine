# Worker parity corpus

Four unchanged Wine D3D11 test DXBC arrays: ordinary VS, PS, CS and resource-heavy PS.
`manifest.json` records the exact Wine revision, original source lines, original
source hash and each fixture hash. `source-arrays.c` preserves corresponding
source arrays and their original copyright/license header; `COPYING.LIB` supplies
the LGPL 2.1 text. No game shader or unknown downloaded code is included.

Regenerate each fixture by packing its named DWORD array as little-endian uint32.
The arrays were independently compared with the retained binary before copying.
These tiny inputs prove converter parity only for this corpus; they do not
establish game compatibility, representative compiler throughput, or FPS.
