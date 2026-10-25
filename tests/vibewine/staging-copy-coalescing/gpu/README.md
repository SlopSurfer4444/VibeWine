# Targeted staging/view GPU guest

Root compiled the exact `guest.cpp` plus its relative `../../gpu-wave/guest.cpp`
helper using reviewed LLVM-MinGW C++20/O2/Wall/Wextra/Werror/static flags and
D3D11/DXGI/compiler imports. Both matched eighteen-flag variants from101d5ff7
passed six mandatory stages; on rejected both connected staging/view wrong-golden
controls at their intended checks with natural exit20. These are offscreen
correctness results; no game/FPS or installed-runtime acceptance is implied.

Snapshot both sources at their exact relative layout into a fresh quarantine.
Reuse the bounded `../../gpu-wave/build.py` compile/import/hash patterns with
explicit compiler/objdump; never select installed tools/runtime implicitly.
Use the generic guest's private-prefix/network-denied/process-birth/module-hash
admission and cleanup. Its analyzer cannot be used unchanged: this guest emits
probe`staging-view-gpu`, six mandatory stage records plus three layout records.
Normal15records =start+4modules+3layouts+6stages+summary. Reject duplicate fields,
wrong order/status/counters/exits, truncation or missing layouts. Source/build/run
receipts belong to the root wave's seven-day/64MiB shared raw cap and holds.

The width64 case observes both pitches exactly256 before accepting tight-copy
coverage. Width63 requires pitches>252; observed256 retains padding guards.
Full resource copy, full-row region and padded partial region compare every
pixel and untouched padding/rows against independent CPU patterns/poison.
Typeless array views are registered low/high, descriptor epoch extended, then
sampled low→high→low; fixed UNORM/sRGB goldens check distinct slices/formats.
Dynamic typeless WRITE_DISCARD generations sample high/low after each rename.
Negative modes`staging`/`view` first verify the ordinary result then intentionally
change one golden; earlier API failure or unsupported pitch is not acceptance.
No optional stage was counted as passed in root's run.

No window, swapchain, Present, game files, network or perpetual producer.
Per-readback/query5s, cooperative90s, watchdog100s; parent process110s,
stdout64KiB/stderr4MiB. Build source512KiB/binary32MiB/log64KiB/receipt32KiB.
The shared laboratory returned to exact baseline hashes with no IPC socket.
3D/BC alias/padded-image and real allocation-failure branches remain separate
CPU/source evidence; GPU output does not independently prove command count or
that unused view wrappers were avoided. No automatic cleanup service is added.
