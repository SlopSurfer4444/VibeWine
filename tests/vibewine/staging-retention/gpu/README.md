# Headless D3D11 copy correctness guest

`guest.cpp` uses only a hardware device, buffers, copies and event queries. It
creates no window, swapchain or shader. Compile with an explicitly selected
LLVM-MinGW toolchain; this command does not launch Wine:

```sh
"$LLVM_MINGW/x86_64-w64-mingw32-clang++" -std=c++20 -O2 -Wall -Wextra -Werror \
  -static guest.cpp -o /absolute/private/output/guest.exe -ld3d11 -ldxgi -ldxguid
```

Execution belongs in a disposable, network-isolated prefix and copied runtime.
Bind both the engine and prefix copies of D3D11/DXGI/winemetal to the intended
artifacts. Retain loaded-module paths, hashes, complete output, natural process
exit status, and scoped private-server cleanup. Do not run against a game prefix.
Require six `BURST_PASS`, three `AGED_PASS`, final `PASS`, exit zero and no `FAIL`.
The host must enforce a separate deadline and must not treat cleanup-induced
termination as success. The guest checks a 45-second budget and has a 55-second
self-watchdog; termination does not prove cancellation of already queued GPU work.

The test takes 192 byte-exact GPU snapshots while preserving partial CPU writes
across staging renames. Three additional windows each complete 320 copy/query
chunks before another Map, then check readback. API-visible buffers/versions are
bounded below 16 MiB plus about 2.1 MiB of CPU goldens; opaque runtime pools are
not measured. Query polling has a five-second limit.

The recorded candidate run completed 2937 checks and 1164 event queries, with six
bursts and three aging windows, returning zero before private-server cleanup.
The guest source SHA-256 was
`d61f5070663bd4591a438df2d5416bae5f6ff8e340ada13cc6d59a9a6dd987b2`.
This establishes copy/readback correctness in that run, not actual internal pool
counts, every possible driver interleaving, game compatibility, or an FPS gain.
