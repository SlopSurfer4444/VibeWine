# Empty-color GPU adjunct

The initial editor handoff comment in `guest.cpp` records source-only status.
Root subsequently compiled this exact source with reviewed LLVM-MinGW C++20,
O2, Wall/Wextra/Werror, static runtime and d3d11/dxgi/d3dcompiler/dxguid imports.
Both matched eighteen-flag off/on builds from `101d5ff7` passed all four stages
in the isolated authless network-denied GPU lab. The on variant rejected the
connected depth wrong-golden at its intended check with natural exit20.
This is a GPU correctness result, not FPS or installed-game acceptance.

Reuse the reviewed `../../gpu-wave/build.py` builder next to a copied exact
`guest.cpp` in a new quarantine directory, with explicitly supplied compiler,
objdump and output paths. Root execution must follow that guest's disposable
prefix, private server, module/native-hash/process-birth admission and cleanup.
The generic analyzer must be adapted, never used unchanged: probe
`empty-color-gpu-adjunct`, mandatory stages `mrt4`, `mrt8`, `sparse_mrt`,
`depth_only`; normal output10records (start,4modules,4stages,summary).
Strict JSON/duplicate-key/accounting/exit validation and source/binary/tool
hashes remain required. No automatic installed runtime or game prefix selection.

4MRT and8MRT use independently specified eight-output shader/CPU byte goldens.
Sparse slots0/3/7 preserve poison in all unbound images. Every image is64x64;
alpha0 poison differs from every alpha255 drawn golden. Occlusion must be4096.
Depth-only uses no PS/color, R32_TYPELESS/D32_FLOAT, near0.25/query4096 and
far0.75/query0 after reopen; exact raw bits0x3e800000 must survive both readbacks.
Negatives `mrt4`, `mrt8`, `sparse`, `depth` deliberately corrupt one golden after
the ordinary result passes; only depth was root-executed in this checkpoint.

Each readback/query has5s deadline, total90s and watchdog100s. Parent bounds
stdout64KiB/stderr4MiB/process110s and validates exact mappings before ACK.
Source512KiB/binary32MiB/build logs64KiB/receipt32KiB bounds apply; root raw
receipts share seven-day/64MiB wave retention with active/recovery holds.
No window, swapchain, Present, user assets/network, perpetual collector or new
retention service. Mip/array/3D attachment, native failure validation and actual
presentation remain outside this adjunct's proof.
