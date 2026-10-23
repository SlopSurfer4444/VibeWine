# Offscreen integrated-wave GPU regression guest

This is a procedural D3D11 correctness guest for an explicitly admitted matched
PE/native candidate. It creates no window or swapchain, launches no child, opens
no network connection, reads no game assets/saves and changes no runtime
selection. Source preparation and PE compilation do **not** establish GPU success.
Execution and private-prefix/server ownership belong to the integration owner.

## Build and CPU report checks

Use an already reviewed LLVM-MinGW toolchain. All tool and output paths are
supplied; no installed Wine or compiler is automatically selected.

```sh
python3 tests/vibewine/gpu-wave/build.py \
  --compiler /absolute/reviewed/bin/x86_64-w64-mingw32-clang++ \
  --output /absolute/new/quarantine-directory \
  --wine-build /absolute/candidate/build-identity \
  --source-id candidate-revision
python3 tests/vibewine/gpu-wave/test_report.py
```

`--objdump` may supply the reviewed `llvm-objdump`; otherwise its adjacent path
is used. `--wine-build` records identity only and never executes that directory.
The builder snapshots source, builds a static x64 PE, inspects imports, and
records command/tool/source/binary hashes. It never runs the PE or Wine. Import
checks bind D3D11, DXGI and compiler imports; they cannot prove which DLLs a later
process actually loads. The guest separately reports bounded escaped UTF-8 paths
for D3D11, DXGI, WineMetal and D3DCompiler 47 and provides a one-second observation
window for the parent's independent native/PE mapping and hash checks.

## GPU stages and independent goldens

Stages run in this fixed order, with explicit state reset/flush between them:

1. `draw_basic`: full-screen procedural triangle into a 64x64 RGBA8 texture,
   vertex/pixel constant-buffer binding, poison clear and exact red/alpha golden
   for every pixel. There is no vertex-buffer asset or file dependency.
2. `mrt_depth`: two render targets plus D32 depth. A near green/magenta draw is
   flushed; a later farther red draw must fail depth. Both images must retain
   every expected byte, testing depth preservation across render passes.
3. `compute`: 8x8 thread groups write a 64x64 R32_UINT UAV from a separate
   constant-buffer seed and pixel coordinates. Every uint is checked against an
   independent CPU expression after UAV unbinding/copy/coherence.
4. `msaa`: query four-sample RGBA8 support. If present, a sample-frequency pixel
   shader writes sample zero red and the other three black. Resolving must yield
   quarter red (RGBA8 bytes 64,0,0,255) everywhere. Copying a single sample cannot
   pass. Unavailable four-sample support produces an explicit optional
   `unsupported` stage; it is never counted as tested.
5. `query_later_chunk`: compare an occlusion query ended in its drawing chunk
   with `Begin; Draw; Flush; End; Flush` and no further draws before `End`.
   Both must complete with exactly 4096 samples, and the image remains red.
   In the current source, the first Flush commits the dirty draw chunk and the
   later End promotes a second chunk whose visibility offset count is zero. The
   retained query still requires readback publication. This is source-grounded
   public-API coverage, not direct observation of internal sequence IDs.

Readbacks honor RowPitch. Event completion and subsequent nonblocking Map
coherence each have a five-second deadline. Cooperative total budget is 90
seconds; a guest-owned watchdog exits 124 at 100 seconds. The parent also needs
an independent process/log deadline. No readback or screenshot is part of a
gameplay FPS timed section: this guest is solely a correctness regression probe.

## Root-coordinated execution only

Before execution the parent must construct a fresh disposable prefix and private
server/IPC namespace, copy and hash-bind the matched candidate, constrain all
cache/temp writes to its quarantine, deny network externally, and verify that
no user prefix or existing server will be selected. Guest code cannot enforce
host-side isolation or prevent driver/Wine cache writes by itself. Reuse the
admitted full-bundle GPU runner's isolation policy/environment and exact-process
cleanup; never run this against a game prefix or a user's Wine server.

With those parent-owned bindings already set, the normal executable invocation is:

```sh
/usr/bin/sandbox-exec -f "$TASK_ISOLATION_POLICY" \
  "$TASK_CANDIDATE_WINE" "$TASK_QUARANTINE/guest.exe" \
  > "$TASK_RUN_DIR/stdout.jsonl" 2> "$TASK_RUN_DIR/stderr.log"
```

The environment must explicitly bind the private `WINEPREFIX`, `WINESERVER`,
`WINELOADER`, `WINEDLLPATH`, native framework path, `DXMT_SHADER_CACHE_PATH` and
temporary directory to the parent's admitted candidate/quarantine. This command
does not create that environment, sandbox policy or prefix. Preserve its natural
exit status before cleanup. Validate the captured stdout with that actual status:

```sh
python3 tests/vibewine/gpu-wave/analyze.py --stdout "$TASK_RUN_DIR/stdout.jsonl" \
  --exit-code "$TASK_GUEST_EXIT"
```

Normal acceptance requires natural exit zero, one valid `pass` summary, all four
mandatory stages passing, explicit MSAA pass/unsupported, required module paths,
valid order/counters and no malformed/truncated/extra records. Additionally the
parent must independently verify loaded PE/native mapping paths and expected
hashes. A loaded module path alone does not attest its contents. A natural
negative-control rejection is a distinct outcome, not a positive GPU run.

After a normal run, root may run separate processes with
`--negative-control pixel`, `--negative-control compute`, and
`--negative-control query`. Each first verifies its uncorrupted result, then must
reject a deliberately changed golden at the corresponding stage, emit summary
`negative_control_rejected` and naturally exit 20. Analyze with both the actual
`--exit-code 20` and matching `--negative-control` argument. An earlier failure,
timeout or different rejection is not a passing negative control. Exit 2 marks
mandatory hardware capability unsupported; exit 10 is a correctness/API failure.

## Scope, provenance and retained data

The guest reuses the local `full-bundle-20261023/shader-probe/guest.cpp` procedural
shader/readback pattern and
`tests/vibewine/staging-retention/gpu/guest.cpp` bounded query/coherence/watchdog
pattern. These are local VibeWine fixture sources, not downloaded implementations.
This extension remains under the DXMT component's LGPL-2.1-or-later terms; the
SPDX headers make that explicit. It does not import game code or assets.

The fixture owner is this directory; the parent owns generated quarantine and
run evidence. Build caps: source 512 KiB, executable 32 MiB, each build/inspection
log 64 KiB, receipt 32 KiB. Guest output is fixed-stage JSONL with bounded module
paths; the analyzer limits input to 64 KiB and rejects duplicate/unknown records.
The parent must cap stderr independently and keep run receipts, hashes, goldens
and unresolved failures under its existing campaign retention policy. Temporary
compiler/test directories expire at test completion; a retained successful
build/run quarantine expires after acceptance plus seven days with a 64 MiB cap,
unless held for an unresolved regression. No automatic cleaner is installed and
no user/runtime data is eligible for cleanup under this fixture contract.

This cannot verify displayed frames, Present batching, fullscreen/resize/HDR,
geometry/tessellation/stream-output, complete query reuse interleavings, driver
memory reclamation, DS3 gameplay, FPS or latency. The source-extracted CPU suite
under `encoder-marshalling/` covers additional setup/error paths; matched native
and visible gameplay checks remain separate integration gates.
