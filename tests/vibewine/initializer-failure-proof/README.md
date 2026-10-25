# Resource initializer: explicit known-defect diagnostic

This is a **source-only reproduction of bugs**, not a correctness test or a production repair. A successful diagnostic run means the expected defects are still present. Do not add it as a green correctness/CI gate. Running without the explicit diagnostic flag refuses before compiling or executing anything:

```sh
python3 tests/vibewine/initializer-failure-proof/run.py --expect-known-defects
```

The default source is the checkout containing this directory. To diagnose another reviewed checkout:

```sh
python3 tests/vibewine/initializer-failure-proof/run.py \
  --expect-known-defects --source-dir /path/to/dxmt
```

Requires macOS, `clang++`, Python 3, and `sandbox-exec`. There is no unsandboxed fallback. Tested against source commit `a21579fb2a03aef5fbe93d7bcba1ea882e63a192`; later source changes may intentionally make an assertion fail. Such failure must be reviewed instead of weakening the diagnostic. Once a coherent fix exists, replace the bug expectations with corrected-behavior tests and validate caller error propagation separately.

## What executes

The runner compiles the **complete production bodies** of `ResourceInitializer`, `RingBumpState`, and `AllocationRefTracking`. It uses the actual `Metal.hpp` wrappers, `winemetal.h` command ABI, enum operators, and context pointer/alignment helpers. Production dependency includes are removed to isolate these classes; method bodies are not rewritten. Private access is opened only for initializer boundary setup. Every bound input is read once into a byte snapshot. The receipt hashes those bytes, and compilation uses only the temporary snapshots. The source and fixture hashes, generated translation-unit hash, and observed Git HEAD are printed before execution; HEAD alone does not establish clean source identity.

The native Metal C API is a CPU mock that records encoded commands, immediately completes submitted sequence signals, and injects null returns from `MTLDevice_newBuffer`. Buffer/texture metadata are test proxies. Format helpers cover the selected RGBA8 and D32S8 inputs. Native handles and native retain/release are mocks. Heap poisoning makes incomplete command publication observable without interpreting arbitrary payloads as real Metal resources. No native Metal decoder, GPU, installed runtime, game, or user data is used or changed.

The diagnostic uses ASan and UBSan with exceptions disabled for this fixture. That compiler flag is not a claim about the production DXMT build policy. The fixture process runs under a dynamic sandbox denying network and file writes outside its temporary directory. Compilation has a 60-second deadline; execution has a 15-second deadline. Each captured stdout/stderr file has a 4 MiB hard size limit. Temporary source, executable, and captures are removed when the runner exits, including on failure.

## Sixteen observations

| Observation | Expected known-bad result |
|---|---|
| Natural 1 MiB CPU arena exhaustion | 18,454 requested fills produce 18,453 encoded fills |
| CPU arena full: buffer fill | Request lost after flush |
| CPU arena full: depth/stencil clear | Request lost after flush |
| CPU arena full: render-target clear | Request lost after flush |
| CPU arena full: texture zero | Request lost after flush |
| CPU arena full: ordinary texture upload | Request lost after flush |
| Reference tracking spill storage full | Request lost after flush |
| One-shot ordinary upload allocation failure | One incomplete command reaches encoding |
| Next operation after that one-shot failure | Failed ring block reused; no second backend allocation |
| Depth/stencil second command cannot fit | First incomplete command reaches encoding |
| Depth upload allocation fails | Two incomplete commands reach encoding |
| Stencil upload fails after depth reserve | Two incomplete commands reach encoding |
| Cached zero buffer grows | Requested copy lost, with two extra commits |
| Zero-buffer allocation fails | Fill with a null buffer encoded; nonzero size cached |
| Next zero request after failed allocation | No fresh backend allocation |
| Success control across all six paths | Two fills, four copies, two clears; no invalid command or null handle |

Natural exhaustion and zero growth use ordinary production operations, not forced arena offsets. The success control uses the same poison and proxies, falsifying poison alone as the cause. One-shot failure followed by no fresh backend call distinguishes invalid-block reuse from persistent backend OOM. The depth/stencil success control distinguishes the failure from a generally broken two-plane proxy.

## Why there is no production patch here

The `ALLOC_*` and `RETAIN` macros use `continue` inside `do { ... } while (0)`, which exits the operation after flushing. `allocateBlit` links commands before later reservations can fail. A loop-only fix does not solve partial publication, failed ring blocks, or null zero-buffer caching.

A coherent repair needs finite reserve → fill → publish semantics. Reserve all commands and upload regions before publishing either depth/stencil plane, flush only valid prior work, retry at most a bounded number of times, and return a checked error without inventing sequence sentinels. Existing initialization callers ignore the returned sequence; resource factories must propagate failure before publishing COM objects. `flushToWait` consumers treat every nonzero result as an actual GPU event target, so substituting a positive error sentinel is unsafe and substituting zero hides failure.

Source anchors in the tested baseline:

- `src/dxmt/dxmt_resource_initializer.cpp`: macros 28–61; upload reservations 278–280 and 330–334; zero-buffer replacement 469–496.
- `src/dxmt/dxmt_resource_initializer.hpp`: early command linking 112–123.
- `src/dxmt/dxmt_ring_bump_allocator.hpp`: tail reuse 173–180; unchecked block insertion 249–256.
- `src/d3d11/d3d11_buffer.cpp:110` and `src/d3d11/d3d11_texture_device.cpp:380,385`: unchecked initialization results.

## Limits and artifact lifetime

This proves CPU-side control flow and command publication under the selected fault seams. It does **not** prove actual GPU execution, native object lifetime, asynchronous completion, host malloc/list-allocation failure handling, queue/event/command-buffer creation failure handling, a fixed caller HRESULT contract, game compatibility, performance, or FPS. Observed malformed command types are recorded by the mock; a real native decoder may assert, ignore, or interpret stale payloads differently.

These small public fixture sources and this README are permanent minimal reproducer material. The runner creates no durable raw collection by default. If output is retained under the existing wave evidence contract, its invoking task owner must keep aggregate raw captures below 64 MiB with a seven-day TTL, retain active evidence until review, and retire exact task-owned captures manually after the hold ends. Source/build artifacts remain in their existing contract class; this diagnostic does not install a cleaner or authorize unrelated deletion.
