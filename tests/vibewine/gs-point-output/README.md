# Geometry shader point outputs

Source adaptation: [3Shain/dxmt d162ea44a31fbf9089ea09977337f0beb30bd082](https://github.com/3Shain/dxmt/commit/d162ea44a31fbf9089ea09977337f0beb30bd082)
(Feifan He / CodeWeavers; DXMT LGPL-2.1-or-later). Preserves this branch's
coroutine conversion architecture.

Point-list geometry output needs `air.point_size` metadata, a size of 1.0 for
each emitted point, and primitive count equal to emitted vertices. Line and
triangle strips retain their existing cut-dependent primitive accounting.

`run.py` rebuilds the actual `src/airconv/dxbc_converter_gs.cpp` with LLVM14
and links unchanged support objects. It creates and deletes temporary build
scratch; it never rebuilds the support tree or installs/activates a runtime.
The fixture calls `convert_dxbc_geometry_shader`, verifies the real LLVM module,
checks AIR point metadata, specializes the invocation-validity branch, and runs
LLVM's mem2reg/SCCP/CFG simplification/InstCombine passes. It checks resulting
AIR primitive count operands, point sizes and point vertex/index identity.

There are five point scenarios per oracle: zero, one, four emits, four emits
with an explicit cut, and an inactive invocation. Sixteen line/triangle controls
cover zero through five emitted vertices, two explicit strips and inactive
invocations. All modules are verified before and after optimization. UBSan is
enabled for rebuilt code. The baseline oracle requires each of the three point
mechanisms to fail independently while all line/triangle controls still pass.

```sh
python3 tests/vibewine/gs-point-output/run.py \
  --llvm /absolute/path/to/llvm14 \
  --support-build /absolute/path/to/existing/dxmt-build
python3 tests/vibewine/gs-point-output/run.py \
  --llvm /absolute/path/to/llvm14 \
  --support-build /absolute/path/to/existing/dxmt-build \
  --baseline-ref bbf22e6a
```

This is compiler-output regression evidence. It does not execute Metal, test a
real game shader, or establish gameplay/performance improvement.
