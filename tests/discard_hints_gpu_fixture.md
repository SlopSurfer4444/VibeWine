# Offscreen discard-hint fixture

`discard_hints_gpu_fixture.cpp` reuses the local procedural shader, bounded event
query, pitched readback, and watchdog patterns from
`tests/vibewine/gpu-wave/guest.cpp` and
`tests/vibewine/pass-optimizations/gpu/guest.cpp`. It requires D3D11 feature level
11.0 and `ID3D11DeviceContext1` on immediate and deferred contexts. There is no
window, swap chain, installed-game access, runtime selection, or persistent data
producer. The fixture itself creates only bounded process/GPU resources; the
caller owns retained executable/log evidence and its retention policy.

Build with the reviewed x86_64 LLVM-MinGW compiler used by the sibling fixtures:

```sh
"$fixture_compiler" -std=c++20 -O2 -Wall -Wextra -Werror -static \
  tests/discard_hints_gpu_fixture.cpp -o "$fixture_output/discard_hints_gpu_fixture.exe" \
  -ld3d11 -ldxgi -ld3dcompiler -ldxguid
```

`fixture_compiler` and `fixture_output` must be caller-selected reviewed paths.
The root owner builds/runs against its isolated, matched baseline and S2 runtime
generations. This document does not authorize installing or selecting a runtime.
The existing `gpu-wave/build.py` can alternatively be copied to a private scratch
directory with this source named `guest.cpp`; it produces the existing bounded
compile/import receipts without executing Wine. Do not modify the shared builder
or shared sibling `guest.cpp` to select this source.

Run the resulting PE using the already-authorized isolated runner. The normal
run must exit 0 and end with exactly `PASS cases=8 images=25 control=none`.
Repeat unchanged against the default-off baseline and the explicitly enabled S2
candidate. Every `IMAGE_PASS` checks every RGBA8 pixel in the named subresource,
respecting mapped RowPitch.

Single-slice surfaces use a `TEXTURE2D` RTV so the positive cases bind the base
native view eligible for conservative S2. The mip/slice isolation surface uses
`TEXTURE2DARRAY` RTVs as explicit conservative negative cases.

The 8 case groups cover:

| Case group | Defined pixel evidence |
| --- | --- |
| `discard_resource` | Complete clear and draw overwrite after resource discard |
| `discard_view` | Complete clear and draw overwrite after view discard |
| `rectangles` | Partial rect overwrite plus untouched complement; full rect overwrite |
| `mip_slice_isolation` | One selected mip/slice overwritten; three untouched subresources |
| `depth_stencil` | Both discarded DSV aspects redefined, depth/stencil pass and fail draws, retained color, later one-aspect clears |
| `intervening_read_copy` | Copy and shader sample before discard preserve their outputs; source fully overwritten |
| `repetition_pass_boundaries` | Repeated discard, submission/completion boundary, unrelated pass, later target pass |
| `deferred_record_replay` | Recording leaves defined immediate target intact, list replayed twice, discard/overwrite split across lists, unrelated resource retained |

The discarded regions' old contents are never inspected. The partial-rect case
inspects only a defined overwrite inside the discarded rect and the defined
untouched complement. The DSV case redefines **both** aspects before a comparison;
it does not test accidental depth/stencil retention after discard. No guarantee
is asserted about whether a permitted discard actually invalidates storage.

Run each control as its own process with identical runtime selection:

```text
discard_hints_gpu_fixture.exe --omit-overwrite
discard_hints_gpu_fixture.exe --wrong-action
```

Each control must exit **20**, print `CONTROL_INJECTED` and `CONTROL_DETECTED`,
and never print the final normal `PASS` summary. An API/setup failure exits 1 and
does not count as control rejection. An undetected control also exits 1.

* `--omit-overwrite` omits the real final blue draw of the resource-discard case.
  A mandatory green clear still defines every discarded byte, so the unchanged
  blue golden fails deterministically against defined green pixels.
* `--wrong-action` issues a real black clear against the untouched mip0/slice0
  during the mip/slice case. Its unchanged red golden detects that wrong-resource
  action. This is a connected GPU-command control, not a mutated golden or a
  production optimizer injection; it establishes the checker catches this class
  of output corruption without relying on undefined discard storage.

The fixture prints loaded module paths. With Wine/DXMT, independently bind the
observed process/module mappings to the intended candidate; output text alone
does not establish the native bridge generation. Optional
`VIBEWINE_GPU_MAP_ACK` accepts the existing runner's Windows-visible acknowledgment
file path and waits at most 15 seconds after module reporting. Without that
variable there is no mapping hold. The process has a 90-second operation budget,
5-second GPU/map polling deadlines, and a 100-second watchdog (exit 124).

Limitations: image parity cannot prove an optimization fired or quantify its
performance. Pair it with production action-count/eligibility evidence. This
fixture does not cover MSAA resolve, UAV discard, 3D/cube textures, concurrent
contexts, game behavior, or FPS. Unsupported context/API/device features fail the
run rather than becoming a silent pass. Preparation of these sources does not
constitute a compiler, runtime, or GPU result.
