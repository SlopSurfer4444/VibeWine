# Graphics PSO stream-output layout identity

`MTL_GRAPHICS_PIPELINE_DESC` contains `SOLayout`, but its hash and equality used
to omit that field. `MTLCompiledGraphicsPipelineImpl` selects
`ShaderVariantVertexStreamOutput` using this layout handle
(`d3d11_pipeline.cpp:30-36`). Reusing a graphics PSO for a different layout can
therefore select the wrong compiled vertex variant.

The omission is reachable with supported emulated stream output: each layout's
`NullGeometryShader::GetManagedShader` returns null
(`d3d11_input_layout.cpp:172`), and the context separately extracts `SOLayout`
(`d3d11_context_impl.cpp:4724-4728`). Distinct layouts can have equal remaining PSO
fields. `GetGraphicsPipeline` uses the production descriptor's hash/equality in
its `pipelines_` unordered map (`d3d11_pipeline_cache.cpp:422-430`).

The fix adds the canonical `SOLayout` pointer to both hash and equality beside
`InputLayout`. It compares no descriptor padding and handles nullptr naturally.

## Identity, complete semantics, and lifetime

The existing SO cache interns `MTL_STREAM_OUTPUT_DESC` and returns an existing
object for equal descriptors (`d3d11_input_layout.cpp:268-275`). Its descriptor
hash/equality already include `RasterizedStream`, all four strides, ordered
stream-output elements, and each element's `Register`, `Component`, `OutputSlot`,
and `Offset`. Equality also explicitly checks element count. The hash need not
be collision-free to preserve correctness. Adding canonical identity to the PSO
key consequently distinguishes every current semantic SO descriptor field.

`StateObjectCache` owns layout objects in `unique_ptr`s
(`d3d11_state_object.hpp:159-167`). `ManagedDeviceChild::Release` does not delete
objects (`d3d11_device_child.hpp:111-118`); no SO-cache erase path exists here.
The layout-cache member precedes the pipeline maps and scheduler
(`d3d11_pipeline_cache.cpp:226-246`), so it outlives their destruction. This fix
uses that existing lifetime and ownership contract without introducing retention.

## Focused fixture

The runner extracts the exact production graphics descriptor, hash, equality,
SO element/descriptor declarations and hash/equality, plus their actual format,
topology and index-format enums. It includes the production `HashState` header.
Opaque shader/input/layout interfaces and the unrelated blend interface are
substituted with native fixture declarations; blend comparisons are explicitly
rejected if entered. The layout owner is a fixture unordered map with the same
production SO descriptor keys and stable unique_ptr ownership. It does not
execute the full COM state-object cache or a Metal compiler.

Five groups exercise:

- Independent equivalent SO descriptors (different vector capacity) canonicalize
  to one pointer; all 12 single-field/count/order variations canonicalize apart.
- A→B→A graphics-key lookup creates two entries and returns the first on repeat.
- Changing RasterizedStream, each of four strides, each of four element fields,
  element count in both directions, or element order separates production PSO
  cache entries. SO identity must affect the production hash, allowing collisions.
- Equivalent key equality/hash/cache reuse, all-default keys, and nullptr versus
  SO layout separation.
- Equal semantic fields with different representation padding compare/hash
  equally. Inactive color-format entries also remain irrelevant. Compile-time
  size/offset checks establish that production SO elements are four contiguous
  uint32 fields with no padding on each tested architecture.

Run from component root:

```sh
python3 tests/vibewine/pso-solayout/run.py --sanitize
python3 tests/vibewine/pso-solayout/run.py --arch x86_64 --sanitize
python3 tests/vibewine/pso-solayout/run.py --baseline-ref 96940d73ac28f57fe12df832d246d3e84965f2cd --sanitize
```

Baseline mode uses the immutable original graphics descriptor header with the
same fixture. It requires three exact cache-aliasing/equality failures and two
unaffected control groups to pass, rejecting unrelated failures or sanitizer
crashes. Generated headers/executables are private temporary files removed on
runner exit. The runner prints source hashes.

Verification on 2026-10-26: candidate native arm64 and x86_64 ASan/UBSan runs each
pass all five groups. Immutable baseline gives exactly three expected failures
and two passing controls. `git diff --check` passes.

Graphics header SHA-256:
`3dd5b9a7cc18dd726eed81feaa26c139a84354297fbd39d5fe8763d3cca03b48`.
Baseline graphics header SHA-256:
`fae87cdf35f402ad0d9e93ba257fcb2f55825c7d67a8e46be6f715be26e3598b`.

This is a correctness guard supported by source and native key/cache fixtures.
Matched production build, GPU SO-buffer readback, game compatibility and FPS were
not exercised. Canonical integration, Git index, build, GPU and installation
remain with the root owner; this isolated branch is left unstaged/uncommitted.
